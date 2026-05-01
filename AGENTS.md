# Project Agents Guide

This repository is edited on macOS / Apple Silicon and validated on a Windows
x64 host. Keep this file focused on the current workflow and repeatable
experience; do not turn it into a general installer manual.

## Current Windows Host

- Primary Windows host: SSH alias `win-home`.
- Remote checkout: `H:\Codex\Module_Context`.
- Installer/cache area: `H:\Installers`.
- GitHub repository: `https://github.com/yuqing112256313-pixel/Module_Context.git`.
- Legacy cloud host, only if explicitly needed: SSH alias `aliyun-win`.
- Windows helper scripts still live under `scripts/ecs/`; the directory name is
  historical.

Installed toolchain on `win-home`:

- VS2015 Update 3 / MSVC 14.0 x64.
- Qt 5.9.7 `msvc2015_64`: `H:\Qt\Qt5.9.7\5.9.7\msvc2015_64`.
- Erlang: `H:\Tools\Erlang\OTP-27.3.4.3`.
- RabbitMQ: `H:\Tools\RabbitMQ\rabbitmq_server-4.2.5`.
- PowerShell 7, Git for Windows, and CMake are expected on `PATH`.

Do not add VS2015 or Qt installation walkthroughs here. If a future machine is
missing a tool, first run the environment check and then ask the user for the
specific missing installer or permission to download it.

## Normal Loop

From macOS:

```sh
git status --short
git push origin main
ssh win-home 'pwsh -NoProfile -Command "Set-Location H:\Codex\Module_Context; git pull --ff-only"'
```

Prefer PowerShell 7 on Windows:

```sh
./scripts/ecs/run-remote-pwsh.sh local-script.ps1
```

Avoid sending complex PowerShell or Chinese text directly inside the SSH
command line. Put it in a UTF-8 `.ps1` file and run it with `pwsh`.

Remote environment check:

```powershell
pwsh -ExecutionPolicy Bypass -File H:\Codex\Module_Context\scripts\ecs\Test-WindowsBuildEnv.ps1 -RequireVS2015
```

Qt build preset:

```powershell
pwsh -ExecutionPolicy Bypass -File H:\Codex\Module_Context\scripts\ecs\Sync-BuildTestFromGit.ps1 `
  -RepoUrl https://github.com/yuqing112256313-pixel/Module_Context.git `
  -WorkDir H:\Codex\Module_Context `
  -Branch main `
  -Preset windows-vs2015-x64-qt597-debug
```

Direct build after the checkout is current:

```powershell
cmake --build --preset windows-vs2015-x64-qt597-debug --config Debug --target mc_task_flow_qt_master_demo
```

## GUI Verification Without RDP

The user has observed repeatable PC freezes when Windows App / Remote Desktop is
used and then SSH is used against the same machine. Treat RDP/Windows App as a
last resort and do not initiate it from an agent workflow.

For visual checks, prefer an app-owned screenshot path:

```powershell
$exe = 'H:\Codex\Module_Context\build\windows-vs2015-x64-qt597\examples\task_flow_qt_master_demo\Debug\mc_task_flow_qt_master_demod.exe'
& $exe --screenshot H:\Codex\Screenshots\qt-master-demo.png
```

Then pull the image to macOS:

```sh
mkdir -p _remote_screenshots
scp -O win-home:'H:/Codex/Screenshots/qt-master-demo.png' _remote_screenshots/
```

This validates rendering and Chinese text without opening an interactive remote
desktop. Let Qt use the default Windows platform plugin for this screenshot.
Forcing `QT_QPA_PLATFORM=offscreen` on Qt 5.9.7 produced a misleading image
with control outlines but no text. If a truly interactive desktop session is
required, ask the user before using it and keep SSH validation paused until
that session is closed.

## Encoding Rules

Remote scripts should initialize UTF-8 output explicitly:

```powershell
$PSStyle.OutputRendering = 'PlainText'
$utf8 = [Text.UTF8Encoding]::new($false)
[Console]::InputEncoding = $utf8
[Console]::OutputEncoding = $utf8
$OutputEncoding = $utf8
```

Keep Git readable for Chinese paths/logs:

```powershell
git config --global core.quotepath false
```

VS2015 notes:

- VS2015 Update 3 on `win-home` reports MSVC `19.00.24210` and MSBuild
  `14.0.25420.1`; compare that before treating old-toolchain errors as product
  code issues.
- Early VS2015 does not understand `/utf-8`, `/source-charset:utf-8`, or
  `/execution-charset:utf-8`.
- For `.cpp` or `.h` files containing Chinese string literals, keep UTF-8 with
  BOM so VS2015 reads the source correctly.
- In Qt 5.9 + VS2015 C++ code, a BOM is not enough for narrow string literals:
  Qt treats `const char*` text as UTF-8, while MSVC may emit the literal using
  the execution code page. Use `QStringLiteral("中文")` for UI labels, dialog
  text, and log text.
- AUTOUIC-generated `ui_*.h` files are usually safe because `uic` writes Chinese
  text as escaped UTF-8 bytes for `QApplication::translate`.

## RabbitMQ Test Environment

The single-machine E2E broker on `win-home` is isolated as a manual Windows
service:

- Service name: `RabbitMQ_ModuleContext`.
- Node name: `module_context_test@localhost`.
- Base/data/logs: `H:\Codex\RabbitMQTestEnv\base`.
- Config: `H:\Codex\RabbitMQTestEnv\config\rabbitmq.conf`.
- AMQP: `127.0.0.1:5672`.
- Management API: `http://127.0.0.1:15672/api`.

Start or verify it:

```powershell
pwsh -ExecutionPolicy Bypass -File H:\Codex\Module_Context\scripts\ecs\Start-RabbitMqTestEnv.ps1
```

Run the E2E gate:

```powershell
ctest --preset windows-vs2015-x64-qt597-debug -R mc_rabbitmq_task_flow_e2e --output-on-failure
```

The RabbitMQ service runs outside the SSH user session, so
`rabbitmqctl`/`rabbitmq-diagnostics` can fail Erlang cookie auth while the
broker is healthy. For this project, gate on TCP `127.0.0.1:5672` and the
Management API with `guest:guest`.

Do not start RabbitMQ for E2E with `rabbitmq-server.bat -detached` from SSH and
expect it to survive after SSH exits. Use the manual Windows service.

## Network And Transfer Notes

`win-home` has a local proxy at `http://127.0.0.1:7897`, but probe it after a
reboot before depending on it. For downloads on the PC, prefer:

```powershell
curl.exe --proxy http://127.0.0.1:7897 -L --fail -o H:\Installers\file.exe URL
```

For GitHub access through the proxy:

```powershell
git -c http.proxy=http://127.0.0.1:7897 -c https.proxy=http://127.0.0.1:7897 fetch origin
```

After a reboot, user-session proxy software may not be running because no one
opened the Windows desktop. If `7897` is not listening and direct GitHub access
fails, use an SSH-transferred Git bundle as the validation fallback:

```sh
git bundle create /tmp/module_context.bundle main
scp -O -p /tmp/module_context.bundle win-home:'H:/Installers/module_context.bundle'
```

```powershell
Set-Location H:\Codex\Module_Context
git fetch H:/Installers/module_context.bundle main
git merge --ff-only FETCH_HEAD
```

For large uploads from macOS to Windows OpenSSH, prefer legacy scp mode:

```sh
scp -O -p -o Compression=no installer.iso win-home:'H:/Installers/OfflinePackages/'
```

The default OpenSSH SFTP mode may create a 0-byte destination file and not show
growth until close, which makes large transfer monitoring misleading.

macOS privacy can block Codex from reading `~/Downloads` even when filenames are
visible. If `shasum` or `scp` gets `Operation not permitted`, stage the file into
the workspace through Finder first:

```sh
mkdir -p _offline_installers
osascript -e 'tell application "Finder" to duplicate POSIX file "/Users/zhangsitai/Downloads/file.iso" to POSIX file "/Users/zhangsitai/Documents/Codex/Module_Context/_offline_installers" with replacing'
```

Keep `_offline_installers/` and `_remote_screenshots/` out of Git.

## Build Notes

- Keep C++ changes compatible with C++11 and MSVC 14.0.
- Prefer CMake presets over ad hoc command lines.
- Keep Windows-specific validation scripts under `scripts/ecs/`.
- Do not assume macOS can validate MSVC or Qt 5.9.7 behavior.
- The Qt preset can build the task-flow GUI without RabbitMQ; RabbitMQ E2E needs
  both Management API `15672` and AMQP `5672`.
- PowerShell variable names are case-insensitive; avoid `$home` as a local
  variable because it collides with built-in `$HOME`.
- Windows PowerShell child processes launched by CTest can have a different
  module autoload environment than an interactive shell. Avoid relying solely on
  cmdlets such as `Get-FileHash`; keep .NET fallbacks for SHA256.
