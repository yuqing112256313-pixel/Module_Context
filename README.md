# Module Context Core

`module_context_core` 是面向 Windows 离线交付的 CMake 工程，包含：

- 核心 Module-Context 框架，接口在 `include/`，实现 在 `src/`。
- 平台能力模块：`amqp_bus` AMQP 控制面通信、`http_transport` 通用 HTTP 二进制流传输。
- `examples/task_flow` 端到端示例：Master 内存图片、RabbitMQ 共享任务队列、Worker 通过 HTTP 拉图到内存并回传结果。
- `tests/` 单元测试和插件集成测试。
- `third_party/` 离线三方依赖源码。

## 目录

```text
.
|-- examples/
|-- include/
|-- modules/
|-- src/
|-- tests/
`-- third_party/
```

公共头文件只放在 `include/module_context/**` 下。`examples/task_flow` 是业务示例层，不把图片、任务、算法等业务语义放进核心平台接口。

## 依赖

默认离线依赖：

- `third_party/Foundation`
- `third_party/AMQP-CPP-CXX11`
- `third_party/cpp-httplib`

`cpp-httplib` 固定使用 `v0.43.1` 单头库，可用脚本重新导入：

```powershell
.\scripts\import_cpp_httplib.ps1
```

## 构建

本仓库已提供 `CMakePresets.json`，可直接使用 `C:\toolchains\portable`
里的 LLVM-MinGW、CMake 和 Ninja：

```powershell
cmake --preset windows-llvm-mingw-debug
cmake --build --preset windows-llvm-mingw-debug
ctest --preset windows-llvm-mingw-debug
```

VSCode 推荐安装 `ms-vscode.cpptools` 和 `ms-vscode.cmake-tools`。
仓库内 `.vscode/` 已把 C/C++ IntelliSense、CMake Tools、默认构建任务和
PowerShell 7 终端接到便携工具链。首次打开后选择
`windows-llvm-mingw-debug` preset，或直接按 `Ctrl+Shift+B` 执行默认构建任务。

常规构建：

```powershell
cmake -S . -B build -DMC_BUILD_TESTS=ON
cmake --build build --config Debug
```

启用 Task Flow E2E：

```powershell
cmake -S . -B build-e2e `
  -DMC_BUILD_TESTS=ON `
  -DMC_BUILD_AMQP_BUS_MODULE=ON `
  -DMC_BUILD_HTTP_TRANSPORT_MODULE=ON `
  -DMC_BUILD_TASK_FLOW_E2E=ON
cmake --build build-e2e --config Debug
```

## 测试

```powershell
ctest --test-dir build-e2e --output-on-failure -C Debug
```

本机 E2E runner 默认使用 RabbitMQ `127.0.0.1:5672` 和 HTTP `127.0.0.1:50080`：

```powershell
powershell -ExecutionPolicy Bypass -File examples/task_flow/run_task_flow_e2e.ps1 `
  -BuildDir build-e2e `
  -Configuration Debug
```

## Task Flow 链路

1. Master 启动前预生成一份模拟相机内存图片，发布路径复用该 buffer。
2. Master `ImageStore.Put(image_id, frame)` 保存到进程内存并生成 token。
3. Master `PublishAsync(TaskMessage)` 发布到 RabbitMQ 共享任务队列，发送后立即继续。
4. RabbitMQ 将任务分发给每台 Worker 的单个 consumer，`prefetch` 等于本机任务线程数。
5. Worker 收到任务后不提前 Ack，通过 `http_transport` 下载图片到内存。
6. Worker 执行模拟算法，`PublishAsync(ResultMessage)` 入队发送后 Ack 任务。
7. Master 收到结果，按 `image_id` 幂等合并，Ack 结果并删除内存图片。
8. Master 通过 `mc.control.exchange` 广播 shutdown 控制消息。

图片不落地，不使用 SMB、RamDisk、ImDisk，也不在任务消息中携带 `image_path`。
