# Project Agents Guide

This repository is maintained from macOS / Apple Silicon, but the target
validation environment is a remote Windows Server 2022 x64 ECS host.

## Operating Model

- Local working tree: macOS, usually edited by Codex.
- Remote Windows host: SSH alias `aliyun-win`.
- Remote checkout path: `C:\work\Module_Context`.
- GitHub repository: `https://github.com/yuqing112256313-pixel/Module_Context.git`.
- Preferred loop: edit on macOS, commit and push, SSH to ECS, pull, configure, build, test.

Use the helper from macOS when possible:

```sh
./scripts/ecs/push-and-verify.sh
```

The helper pushes the current branch, makes sure the ECS checkout exists,
pulls the same branch there, runs the Windows environment check, and then
optionally runs CMake build/test if the toolchain is installed.

## Windows Toolchain Targets

The legacy GUI validation target is:

- Windows Server 2022 x64
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

## ECS Commands

Run the environment check on ECS:

```powershell
powershell -ExecutionPolicy Bypass -File C:\work\Module_Context\scripts\ecs\Test-WindowsBuildEnv.ps1 -RequireVS2015
```

Clone or update the ECS checkout and run validation:

```powershell
powershell -ExecutionPolicy Bypass -File C:\work\Module_Context\scripts\ecs\Sync-BuildTestFromGit.ps1 `
  -RepoUrl https://github.com/yuqing112256313-pixel/Module_Context.git `
  -WorkDir C:\work\Module_Context `
  -Branch main `
  -Preset windows-vs2015-x64-debug
```

Qt GUI demo preset:

```powershell
powershell -ExecutionPolicy Bypass -File C:\work\Module_Context\scripts\ecs\Sync-BuildTestFromGit.ps1 `
  -RepoUrl https://github.com/yuqing112256313-pixel/Module_Context.git `
  -WorkDir C:\work\Module_Context `
  -Branch main `
  -Preset windows-vs2015-x64-qt597-debug
```

## Installation Notes

Git can be installed with `scripts\ecs\Install-GitForWindows.ps1`.

VS2015 and Qt 5.9.7 are best installed from official offline installers or
verified local archives because public download links and account requirements
change over time. After installing them, re-run `Test-WindowsBuildEnv.ps1`
before attempting a Qt build.

For VS2015, install the x64 C++ toolchain and Windows SDK components. For Qt,
install the `msvc2015_64` kit and confirm both `bin\qmake.exe` and
`lib\cmake\Qt5\Qt5Config.cmake` exist.

## Build Notes

- Keep C++ changes compatible with C++11 and MSVC 14.0.
- Prefer CMake presets over ad hoc command lines.
- Keep Windows-specific validation scripts under `scripts/ecs/`.
- Do not assume macOS can validate MSVC or Qt 5.9.7 behavior.
