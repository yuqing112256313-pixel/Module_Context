# Task Flow Qt Master Demo

这是主机端桌面演示版。它不替换 worker，也不改 RabbitMQ/HTTP 插件；只是把主机脚本里的关键动作搬到 Qt5.9.7 桌面程序里：

- 准备 RabbitMQ vhost、用户、权限、exchange、queue、binding
- 单机离线模式下自动启动本机 worker；远程实机模式下等待外部 worker 任务消费者上线
- 生成主机 module config
- 启动现有 E2E master runner
- 循环复用 BMP 目录里的真图，作为 HTTP 内存图像源
- 显示当前图像、任务状态、队列状态、吞吐和运行日志

算法结果区本轮只是占位，后续再接真实 OK/NG 和缺陷图。

## VS2015 + Qt5.9.7 构建

在内网机器上安装或解压 Qt 5.9.7 msvc2015_64，然后配置：

```powershell
cmake -S . -B build\vs2015-qt-demo -G "Visual Studio 14 2015 Win64" `
  -DMC_BUILD_TASK_FLOW_QT_DEMO=ON `
  -DMC_BUILD_TASK_FLOW_E2E=ON `
  -DCMAKE_PREFIX_PATH=C:\Qt\Qt5.9.7\5.9.7\msvc2015_64 `
  -DMC_FOUNDATION_SOURCE_DIR=third_party\Foundation `
  -DMC_AMQP_CPP_SOURCE_DIR=third_party\AMQP-CPP-CXX11 `
  -DMC_CPP_HTTPLIB_SOURCE_DIR=third_party\cpp-httplib

cmake --build build\vs2015-qt-demo --config Debug --target mc_task_flow_qt_master_demo
```

运行时把 Qt 运行库放到 exe 可搜索路径，或用 Qt 自带的 `windeployqt` 处理。`amqp_bus.dll`、`http_transport.dll`、`mc_core_framework.dll` 会随 target 构建后复制到 exe 目录。

Debug 配置下 demo exe 名称带 `d` 后缀，Release 配置不带后缀。工程内的 framework/module DLL 仍沿用现有项目命名和导入库规则，MSVC 会为 DLL 目标生成对应 `.lib` 导入库。

## 运行

1. 确认本机已有 RabbitMQ 服务，且 management 插件可访问：`http://127.0.0.1:15672/api`。
2. 打开桌面程序。Debug 是 `mc_task_flow_qt_master_demod.exe`，Release 是 `mc_task_flow_qt_master_demo.exe`。
3. 选择包含 `*.bmp` 的图像目录。少于 1 张会禁止启动。
4. 单机离线测试时，把 RabbitMQ Host 填成 `127.0.0.1`，桌面端会自动从 exe 同目录启动本机 worker，不需要脚本和 config 目录。
5. 远程实机测试时，把 RabbitMQ Host 填成主机实际 IP，并先在从机侧启动 worker。
6. 点击“启动 E2E”。

Qt demo 构建后会把 `mc_task_flow_worker_host.exe`、`amqp_bus.dll`、
`http_transport.dll`、`semiplugin_manager.dll` 和示例算法插件复制到桌面程序目录。
因此单机离线测试不依赖 `examples/task_flow/field_deploy/scripts`。
桌面端也不会在 `examples` 输出目录预放 config；每次点击启动时，它会在运行输出目录下生成
`master_module_config.json` 和 `local_workers/*/worker_module_config.json`。

桌面端仍然使用当前 E2E 的数据面：TaskMessage 只携带 `image_id/token/bytes` 和 HTTP 参数；worker 仍然通过 HTTP 从主机内存仓库拉图。

## SSH 截图验证

在不能打开远程桌面的环境里，可以让程序自己渲染主窗口并保存 PNG：

```powershell
.\mc_task_flow_qt_master_demod.exe --screenshot H:\Codex\Screenshots\qt-master-demo.png --screenshot-size 1920x1080
```

这个模式使用默认 Windows Qt 平台即可，不需要 Windows App / RDP 会话。
