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

Large installers should be downloaded on macOS and copied with `scp` whenever
possible. Cloud hosts can access GitHub, but direct release-asset downloads can
be very slow. `bootstrap-ecs-from-mac.sh` auto-detects the macOS Wi-Fi HTTP
proxy or uses `LOCAL_HTTP_PROXY`, `HTTPS_PROXY`, or `HTTP_PROXY` if set.

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

## Windows Toolchain Targets

The legacy GUI validation target is:

- Windows 10/11 or Windows Server x64
- PowerShell 7 latest stable
- Visual Studio 2015 / MSVC 14.0 x64
- Qt 5.9.7 `msvc2015_64`
- CMake 3.23 or newer
- Git for Windows

Default expected install paths:

- VS2015: `C:\Program Files (x86)\Microsoft Visual Studio 14.0`
- Qt: `C:\Qt\Qt5.9.7\5.9.7\msvc2015_64` or `C:\Qt\5.9.7\msvc2015_64`
- CMake: available on `PATH`

If Qt is installed elsewhere, set:

```powershell
setx QT597_MSVC2015_64_DIR "D:\path\to\Qt\5.9.7\msvc2015_64"
```

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
- Expected verification paths:
  - `C:\Program Files (x86)\Microsoft Visual Studio 14.0\VC\vcvarsall.bat`
  - `C:\Program Files (x86)\MSBuild\14.0\Bin\MSBuild.exe`

Qt 5.9.7 manual install:

- Use the official Qt archive:
  `https://download.qt.io/archive/qt/5.9/5.9.7/qt-opensource-windows-x86-5.9.7.exe`.
- Run the installer from the Windows desktop as Administrator.
- Install to `C:\Qt\Qt5.9.7` if the installer asks for a root path.
- Select the `Qt 5.9.7 > msvc2015_64` component. Qt Creator is optional.
- After install, either the default path should exist:
  `C:\Qt\Qt5.9.7\5.9.7\msvc2015_64`, or set:

```powershell
setx QT597_MSVC2015_64_DIR "D:\path\to\5.9.7\msvc2015_64"
```

Verification files:

- `bin\qmake.exe`
- `lib\cmake\Qt5\Qt5Config.cmake`

## Build Notes

- Keep C++ changes compatible with C++11 and MSVC 14.0.
- Prefer CMake presets over ad hoc command lines.
- Keep Windows-specific validation scripts under `scripts/ecs/`.
- Do not assume macOS can validate MSVC or Qt 5.9.7 behavior.
