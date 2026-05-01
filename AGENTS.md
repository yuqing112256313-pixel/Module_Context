# Project Agents Guide

This repository is maintained from macOS / Apple Silicon, but the target
validation environment is a remote Windows x64 build host.

## Operating Model

- Local working tree: macOS, usually edited by Codex.
- Primary remote Windows host: SSH alias `win-home`.
- Legacy cloud host, if needed: SSH alias `aliyun-win`.
- Primary remote checkout path: `H:\Codex\Module_Context`.
- Primary installer/cache path: `H:\Installers`.
- GitHub repository: `https://github.com/yuqing112256313-pixel/Module_Context.git`.
- Preferred loop: edit on macOS, commit and push, SSH to Windows, pull, configure, build, test.
- Preferred remote shell: PowerShell 7 (`pwsh.exe`), with Windows PowerShell 5 only as a bootstrap fallback.

Use the helper from macOS when possible:

```sh
./scripts/ecs/push-and-verify.sh
```

The helper pushes the current branch, makes sure the ECS checkout exists,
pulls the same branch there, runs the Windows environment check, and then
optionally runs CMake build/test if the toolchain is installed.

Bootstrap or refresh the Windows tool layer from macOS:

```sh
./scripts/ecs/bootstrap-ecs-from-mac.sh
```

Large installers should be downloaded on macOS and copied with `scp` only when
the installer already lives on macOS. `win-home` has good network and a local
HTTP proxy at `http://127.0.0.1:7897`; for new downloads on that PC, prefer
remote `curl.exe --proxy http://127.0.0.1:7897 -L --fail ...` directly into
`H:\Installers`. Cloud hosts can access GitHub, but direct release-asset
downloads can be very slow. If GitHub fetch/pull/clone from Windows is flaky,
use the Windows proxy for Git as well, for example
`git -c http.proxy=http://127.0.0.1:7897 -c https.proxy=http://127.0.0.1:7897 ...`.
If a Git command prints `fatal`, treat it as a hard failure instead of
continuing a build from a possibly stale checkout.
After a reboot, do not assume the proxy is actually listening just because the
machine usually has one; probe `127.0.0.1:7897` first. If a GitHub release asset
hangs at 0 bytes without the proxy, retry with `HTTPS_PROXY=http://127.0.0.1:7897`
on macOS or `curl.exe --proxy http://127.0.0.1:7897` on Windows. Erlang's
official download host can be much slower than GitHub; if a single `curl`
stream crawls, use HTTP range segments, concatenate, and verify SHA256 before
copying to Windows.

For multi-GB installer uploads to Windows OpenSSH, prefer legacy scp mode:

```sh
scp -O -p -o Compression=no installer.iso win-home:'H:/Installers/OfflinePackages/'
```

The default OpenSSH SFTP mode may create a 0-byte destination file and not show
growth until close, which makes large transfer monitoring misleading.

macOS privacy can block Codex from reading `~/Downloads` even when `ls` can see
the filenames. If `shasum` or `scp` fails with `Operation not permitted`, stage
the file into the workspace through Finder first:

```sh
mkdir -p _offline_installers
osascript -e 'tell application "Finder" to duplicate POSIX file "/Users/zhangsitai/Downloads/file.iso" to POSIX file "/Users/zhangsitai/Documents/Codex/Module_Context/_offline_installers" with replacing'
```

Keep `_offline_installers/` ignored by Git.

When running ad hoc PowerShell on Windows, avoid putting Chinese text or complex
PowerShell snippets directly in the SSH command line or in Windows PowerShell 5
stdin. It can corrupt Unicode before PowerShell 7 sees it, and cmd built-in
errors may still print through a legacy code page. Put the script in a UTF-8
`.ps1` file and execute it with PowerShell 7:

```sh
./scripts/ecs/run-remote-pwsh.sh local-script.ps1
```

For scripts read from stdin:

```sh
cat local-script.ps1 | ./scripts/ecs/run-remote-pwsh.sh
```

Inside remote scripts, use:

```powershell
$PSStyle.OutputRendering = 'PlainText'
$utf8 = [Text.UTF8Encoding]::new($false)
[Console]::InputEncoding = $utf8
[Console]::OutputEncoding = $utf8
$OutputEncoding = $utf8
```

## Windows Toolchain Targets

The legacy GUI validation target is:

- Windows 10/11 or Windows Server x64
- PowerShell 7 latest stable
- Visual Studio 2015 / MSVC 14.0 x64
- Qt 5.9.7 `msvc2015_64`
- CMake 3.23 or newer
- Git for Windows
- Erlang OTP for RabbitMQ-backed E2E testing
- RabbitMQ Server with management plugin for Task Flow E2E

Default expected install paths:

- VS2015: `C:\Program Files (x86)\Microsoft Visual Studio 14.0`
- Qt: prefer `H:\Qt\Qt5.9.7\5.9.7\msvc2015_64`; `C:\Qt\...` is also recognized.
- Erlang on `win-home`: `H:\Tools\Erlang\OTP-27.3.4.3`
- RabbitMQ on `win-home`: `H:\Tools\RabbitMQ\rabbitmq_server-4.2.5`
- CMake: available on `PATH`

If Qt is installed elsewhere, set:

```powershell
setx QT597_MSVC2015_64_DIR "D:\path\to\Qt\5.9.7\msvc2015_64"
```

On `win-home`, Qt is installed at `H:\Qt\Qt5.9.7\5.9.7\msvc2015_64`.
Keep both `QT597_MSVC2015_64_DIR` and `QTDIR` pointed there, and keep
`H:\Qt\Qt5.9.7\5.9.7\msvc2015_64\bin` on the user `Path` so `qmake.exe` and
Qt runtime DLLs are visible from fresh SSH/Desktop sessions.

On `win-home`, the standard RabbitMQ test broker is isolated as a manual
Windows service:

- Service name: `RabbitMQ_ModuleContext`
- Node name: `module_context_test@localhost`
- Base/data/logs: `H:\Codex\RabbitMQTestEnv\base`
- Config: `H:\Codex\RabbitMQTestEnv\config\rabbitmq.conf`
- AMQP: `127.0.0.1:5672`
- Management API: `http://127.0.0.1:15672/api`

## Windows Host Commands

Run the environment check on the Windows host:

```powershell
pwsh -ExecutionPolicy Bypass -File H:\Codex\Module_Context\scripts\ecs\Test-WindowsBuildEnv.ps1 -RequireVS2015
```

Clone or update the ECS checkout and run validation:

```powershell
pwsh -ExecutionPolicy Bypass -File H:\Codex\Module_Context\scripts\ecs\Sync-BuildTestFromGit.ps1 `
  -RepoUrl https://github.com/yuqing112256313-pixel/Module_Context.git `
  -WorkDir H:\Codex\Module_Context `
  -Branch main `
  -Preset windows-vs2015-x64-debug
```

Qt GUI demo preset:

```powershell
pwsh -ExecutionPolicy Bypass -File H:\Codex\Module_Context\scripts\ecs\Sync-BuildTestFromGit.ps1 `
  -RepoUrl https://github.com/yuqing112256313-pixel/Module_Context.git `
  -WorkDir H:\Codex\Module_Context `
  -Branch main `
  -Preset windows-vs2015-x64-qt597-debug
```

Start or verify the isolated local RabbitMQ broker before running Task Flow E2E:

```powershell
pwsh -ExecutionPolicy Bypass -File H:\Codex\Module_Context\scripts\ecs\Start-RabbitMqTestEnv.ps1
```

Run the single-machine RabbitMQ Task Flow E2E directly:

```powershell
powershell -ExecutionPolicy Bypass -File H:\Codex\Module_Context\examples\task_flow\run_task_flow_e2e.ps1 `
  -BuildDir H:\Codex\Module_Context\build\windows-vs2015-x64-qt597 `
  -Configuration Debug
```

Or run it through CTest:

```powershell
ctest --preset windows-vs2015-x64-qt597-debug -R mc_rabbitmq_task_flow_e2e --output-on-failure
```

## Installation Notes

PowerShell 7 can be installed with `scripts\ecs\Install-PowerShell7ForWindows.ps1`.
Git can be installed with `scripts\ecs\Install-GitForWindows.ps1`.
CMake can be installed with `scripts\ecs\Install-CMakeForWindows.ps1`.

After installing or updating shell tooling, run:

```powershell
pwsh -ExecutionPolicy Bypass -File H:\Codex\Module_Context\scripts\ecs\Initialize-EcsEncoding.ps1
```

Encoding rules:

- Use `pwsh`, not Windows PowerShell 5, for normal ECS work.
- Launch remote commands through `cmd /d /c "chcp 65001 >NUL && pwsh ..."` when
  writing ad hoc SSH commands that may print Chinese.
- Keep Git configured with `core.quotepath=false` and UTF-8 log/commit output.
- Always check the exact VS2015 compiler patch level with `cl` before treating
  a compile error as a product-code issue. Early VS2015 installs can report
  MSVC `19.00.23026`; VS2015 Update 3 on `win-home` reports
  MSVC `19.00.24210` and MSBuild `14.0.25420.1`. Code that fails on the early
  toolset may compile cleanly after the Update 3 toolchain is installed.
- Early MSVC `19.00.23026` does not understand `/utf-8`,
  `/source-charset:utf-8`, or `/execution-charset:utf-8`. For any `.cpp` or
  `.h` file that contains non-ASCII string literals, keep the file as UTF-8
  with BOM so VS2015 reads Chinese UI/log text correctly. Comments-only files
  do not need this, but string-literal files do.
- Do not enable the system-wide Windows "Beta: UTF-8" locale switch unless a
  specific tool requires it; VS2015-era tools are safer with command-scoped UTF-8.

VS2015 and Qt 5.9.7 are best installed from official offline installers or
verified local archives because public download links and account requirements
change over time. After installing them, re-run `Test-WindowsBuildEnv.ps1`
before attempting a Qt build.

VS2015 manual install:

- Use the official Visual Studio Older Downloads page and sign in with a
  Microsoft account that has Dev Essentials or a Visual Studio subscription.
- Prefer `Visual Studio Community 2015 with Update 3` or
  `Visual C++ Build Tools for Visual Studio 2015`.
- Run the installer from the Windows desktop as Administrator.
- Choose a custom install and include:
  - Visual C++ / Common Tools for Visual C++ 2015.
  - x64 compiler tools.
  - Windows SDK components offered by the VS2015 installer.
- Prefer the default VS2015 path if the installer offers no clean custom path;
  old Visual Studio installers still put shared components and registry entries
  under system locations even when part of the payload is moved.
- Expected verification paths:
  - `C:\Program Files (x86)\Microsoft Visual Studio 14.0\VC\vcvarsall.bat`
  - `C:\Program Files (x86)\MSBuild\14.0\Bin\MSBuild.exe`

Qt 5.9.7 manual install:

- Use the official Qt archive:
  `https://download.qt.io/archive/qt/5.9/5.9.7/qt-opensource-windows-x86-5.9.7.exe`.
- Run the installer from the Windows desktop as Administrator.
- Install to `H:\Qt\Qt5.9.7` if the installer asks for a root path.
- Select the `Qt 5.9.7 > msvc2015_64` component. Qt Creator is optional.
- After install, either the preferred path should exist:
  `H:\Qt\Qt5.9.7\5.9.7\msvc2015_64`, or set:

```powershell
setx QT597_MSVC2015_64_DIR "D:\path\to\5.9.7\msvc2015_64"
```

Verification files:

- `bin\qmake.exe`
- `lib\cmake\Qt5\Qt5Config.cmake`

Debugger note:

- Qt Creator can build with the VS2015 compiler even if the debugger list is
  empty.
- Debugging from Qt Creator needs CDB from Windows SDK Debugging Tools, usually
  under `C:\Program Files (x86)\Windows Kits\10\Debuggers\x64\cdb.exe` or the
  Windows Kits 8.1 equivalent.
- If CDB is missing, install Windows SDK Debugging Tools; this is not required
  for the CMake/MSBuild validation loop.

## Build Notes

- Keep C++ changes compatible with C++11 and MSVC 14.0.
- Early VS2015 toolsets can fail overload resolution on lambdas passed directly
  to heavily overloaded APIs such as `cpp-httplib` and AMQP-CPP. Before changing
  module code for this, compare the `cl` version with the known-good Windows
  machine and upgrade to VS2015 Update 3 first; on `win-home`, Update 3 compiled
  the original module callbacks successfully.
- Do not pass `/utf-8` to early VS2015; it is ignored and can hide the real
  encoding problem. Use UTF-8 BOM for C++ files with Chinese string literals.
- The Qt preset can build the task-flow GUI without RabbitMQ, but the RabbitMQ
  E2E test needs both Management API `15672` and AMQP `5672`. If those ports are
  unavailable, the E2E test should skip or fail explicitly, not appear green by
  accident.
- Do not start RabbitMQ for E2E with `rabbitmq-server.bat -detached` from an
  SSH session and expect it to survive after SSH exits. Windows OpenSSH can tear
  down child processes with the session. Use the manual
  `RabbitMQ_ModuleContext` Windows service instead.
- The RabbitMQ service runs outside the SSH user session, so
  `rabbitmq-diagnostics` or `rabbitmqctl` from the SSH user can fail Erlang
  cookie authentication even while the broker is healthy. For this project's
  E2E gate, verify TCP `127.0.0.1:5672` and Management API
  `http://127.0.0.1:15672/api/overview` with `guest:guest`.
- Windows PowerShell child processes launched by CTest can have a different
  module autoload environment than an interactive shell. Avoid making test
  scripts depend solely on cmdlets such as `Get-FileHash`; use a .NET fallback
  for SHA256 when the cmdlet is unavailable.
- PowerShell variable names are case-insensitive; avoid `$home` as a local
  variable because it collides with the built-in read-only `$HOME`.
- Prefer CMake presets over ad hoc command lines.
- Keep Windows-specific validation scripts under `scripts/ecs/`.
- Do not assume macOS can validate MSVC or Qt 5.9.7 behavior.
