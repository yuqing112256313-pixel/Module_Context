# Task Flow E2E 实机测试说明

本包用于一主五从 Windows 端到端测试。主机预生成并复用模拟相机图片，把图片保存在进程内存中，只通过 RabbitMQ 发布控制消息；每台从机用一个 task consumer 接入 `mc.task.queue`，`prefetch` 等于本机任务线程数，收到任务后通过 HTTP/1.1 分块下载图片到本机内存，再通过 `semiplugin_manager` 加载的算法插件处理，最后把结果发回 RabbitMQ。默认不使用 SMB、RamDisk、ImDisk，也不落地图片文件；如果内网真实算法接口必须接收文件路径，把 `Worker.Algorithm.PersistInputImages` 改成 `$true`。

## 机器与路径

实机 IP、端口、工作目录、账号和测试负载统一放在：

```text
config\TaskFlowFieldConfig.psd1
```

更换主机 IP、端口或 worker 节点时，只改这个配置文件。脚本会自动从这里读取
RabbitMQ AMQP、RabbitMQ 管理 API 和 HTTP 图片流地址。

## 部署准备

主机执行：

```powershell
$config = Import-PowerShellDataFile .\config\TaskFlowFieldConfig.psd1
Set-Location $config.Master.InstallRoot
.\scripts\Install-MasterPrereqs.ps1
.\scripts\Start-RabbitMq.ps1
```

五台从机分别执行：

```powershell
$config = Import-PowerShellDataFile .\config\TaskFlowFieldConfig.psd1
Set-Location $config.Worker.InstallRoot
.\scripts\Install-WorkerPrereqs.ps1
```

## 启动测试

先在主机启动测试。主机会创建 RabbitMQ 拓扑，启动 HTTP 图片流服务，然后等待 5 台 worker 的 task consumer：

```powershell
$config = Import-PowerShellDataFile .\config\TaskFlowFieldConfig.psd1
Set-Location $config.Master.InstallRoot
.\scripts\Start-MasterTest.ps1 -Restart
```

再在五台从机分别启动 worker：

```powershell
$config = Import-PowerShellDataFile .\config\TaskFlowFieldConfig.psd1
Set-Location $config.Worker.InstallRoot
.\scripts\Start-Worker.ps1 -Restart
```

默认会等待 `ExpectedWorkerCount` 个 task consumer。本包默认是 5 个；每台 worker 的 AMQP `prefetch` 等于 `WorkerTaskThreads`，默认 64。
等待期间 RabbitMQ UI 看不到 master 连接是正常的，因为 master host 会在 worker consumer 就绪后才启动；如果 UI 里连 worker 连接也没有，优先检查从机输出的 `RabbitMQ AMQP` 地址是否能从从机访问。
主机脚本默认用 `RabbitMq.ManagementHost` 访问管理 API，本包默认是 `127.0.0.1:15672`；AMQP 连接仍使用 `Master.Host:5672`，本包默认是 `10.18.18.101:5672`。

## 默认负载

- Worker 数量：5
- 默认 profile：`read-256k-os`，每轮 100 张
- 发布速率：50 张/秒，持续 2 秒
- 单图大小：20 MiB，确定性二进制模拟图片
- 模拟算法耗时：10 ms
- 算法调用：默认启用 `semiplugin_manager.dll`，加载 `tgv_etching_semiplugin.dll`
- Worker 任务线程：每节点 64
- 主机内存图片容量上限：4 GiB
- RabbitMQ 发布：fire-and-forget，发送后立即继续
- 图片数据面：HTTP 分块下载，默认 64 个 server 线程、8 MiB 主机出块、256 KiB worker 读缓冲、OS 默认 socket buffer

`config\TaskFlowFieldConfig.psd1` 里的 `Http.SweepProfiles` 保留了对比测试结构。默认只有 `read-256k-os` 的 `Enabled = $true`；需要重新对比时，把其他 profile 的 `Enabled` 改成 `$true` 即可。

## 算法插件替换

当前包里的 `tgv_etching_semiplugin.dll` 是示例插件，10 ms 模拟耗时已经在插件内部完成，worker 不再自己 sleep。进入内网合入真实算法时，优先只改这些配置和文件：

```powershell
Worker = @{
    Algorithm = @{
        Enabled = $true
        PluginName = 'tgv_etching'
        PluginPath = 'D:\path\to\real_algorithm.dll' # 留空则用包内 tgv_etching_semiplugin.dll
        CreateFunc = 'CreatePluginEtching'
        DestroyFunc = 'DestroyPluginEtching'
        PersistInputImages = $false
        InputDir = ''
    }
}
```

如果真实接口仍然像当前临时接口一样只接收 BMP 路径，把 `PersistInputImages` 改成 `$true`；worker 会把 HTTP 拉到的图片写入本机运行目录下的 `algorithm_input`，再把该路径交给插件。若真实接口能接收内存对象或三方 SDK 图像对象，则保持 `$false`，只替换 `third_party\SEMIPLUGIN\api\include` 下的抽象接口和插件实现里的适配代码。

## 验收点

主机输出目录：

```text
config\TaskFlowFieldConfig.psd1 -> Master.OutputRoot
```

重点检查：

- `sweep_summary.html`：所有 HTTP 参数组的横向对比，直接看“每秒多少张”和等效 Gbps。
- `sweep_summary.tsv`：同一份对比数据，方便再做表格分析。
- `profiles\<参数组>\master\master_summary.txt` 中 `sent_count=100`、`completed_count=100`、`success_count=100`、`failure_count=0`、`cleanup_failure_count=0`。
- `profiles\<参数组>\master\master_summary.txt` 中 `image_store_remaining_count=0`。
- `profiles\<参数组>\master\task_metrics.tsv` 不包含 `image_path`、`master_write_path` 等落地路径字段。
- `profiles\<参数组>\master\task_metrics.tsv` 每行 `image_store_status=deleted`。
- `profiles\<参数组>\master\task_metrics.html` 自动生成，可直接查看总体吞吐、节点吞吐、阶段 P95/最大耗时和最慢任务。
- RabbitMQ 的 `mc.task.queue` 和 `mc.result.queue` 在结束后为空。

## 停止清理

从机：

```powershell
$config = Import-PowerShellDataFile .\config\TaskFlowFieldConfig.psd1
Set-Location $config.Worker.InstallRoot
.\scripts\Stop-Worker.ps1
```

主机：

```powershell
$config = Import-PowerShellDataFile .\config\TaskFlowFieldConfig.psd1
Set-Location $config.Master.InstallRoot
.\scripts\Stop-TestEnv.ps1
```

`Stop-TestEnv.ps1` 返回后，`Master.InstallRoot` 应可删除或替换，无需手动任务管理器清理。
