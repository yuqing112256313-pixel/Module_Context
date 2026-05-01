param(
    [string]$RepoUrl = "https://github.com/yuqing112256313-pixel/Module_Context.git",
    [string]$WorkDir = "C:\work\Module_Context",
    [string]$Branch = "main",
    [string]$Preset = "windows-vs2015-x64-debug",
    [switch]$SkipBuild,
    [switch]$SkipTests
)

$ErrorActionPreference = "Stop"

function Find-Exe {
    param(
        [string]$Name,
        [string[]]$FallbackPaths = @()
    )

    $cmd = Get-Command $Name -ErrorAction SilentlyContinue
    if ($cmd) {
        return $cmd.Source
    }

    foreach ($path in $FallbackPaths) {
        if (Test-Path $path) {
            return $path
        }
    }

    throw "$Name was not found."
}

$git = Find-Exe "git.exe" @(
    "C:\Program Files\Git\cmd\git.exe",
    "C:\Program Files\Git\bin\git.exe"
)

$parent = Split-Path -Parent $WorkDir
New-Item -ItemType Directory -Force -Path $parent | Out-Null

if (Test-Path (Join-Path $WorkDir ".git")) {
    Write-Host "Updating existing checkout: $WorkDir"
    & $git -C $WorkDir fetch origin
    & $git -C $WorkDir checkout $Branch
    & $git -C $WorkDir pull --ff-only origin $Branch
} else {
    if (Test-Path $WorkDir) {
        throw "$WorkDir exists but is not a Git checkout."
    }

    Write-Host "Cloning $RepoUrl -> $WorkDir"
    & $git clone --branch $Branch $RepoUrl $WorkDir
}

$envCheck = Join-Path $WorkDir "scripts\ecs\Test-WindowsBuildEnv.ps1"
if (Test-Path $envCheck) {
    Write-Host "Running environment check..."
    $requireVs2015 = $Preset -match "vs2015"
    $requireQt = $Preset -match "qt"
    $requireNinja = $Preset -match "ninja|mingw|llvm"
    & powershell -NoProfile -ExecutionPolicy Bypass -File $envCheck `
        -RequireVS2015:$requireVs2015 `
        -RequireQt:$requireQt `
        -RequireNinja:$requireNinja
    $envExit = $LASTEXITCODE
    if ($envExit -ne 0) {
        Write-Host "Environment is incomplete; checkout was updated but build is skipped."
        exit $envExit
    }
}

if ($SkipBuild) {
    Write-Host "SkipBuild was requested."
    exit 0
}

$cmake = Find-Exe "cmake.exe" @(
    "C:\Program Files\CMake\bin\cmake.exe",
    "C:\toolchains\portable\cmake-4.3.2-windows-x86_64\bin\cmake.exe"
)

Write-Host "Configuring preset: $Preset"
& $cmake --preset $Preset

Write-Host "Building preset: $Preset"
& $cmake --build --preset $Preset

if (-not $SkipTests) {
    Write-Host "Testing preset: $Preset"
    $ctest = Join-Path (Split-Path -Parent $cmake) "ctest.exe"
    if (-not (Test-Path $ctest)) {
        $ctest = Find-Exe "ctest.exe"
    }
    & $ctest --preset $Preset
}
