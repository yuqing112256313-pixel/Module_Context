param(
    [string]$QtDir = $env:QT597_MSVC2015_64_DIR,
    [switch]$RequireVS2015,
    [switch]$RequireQt,
    [switch]$RequireNinja
)

$ErrorActionPreference = "Continue"
$script:Missing = New-Object System.Collections.Generic.List[string]

$utf8 = New-Object System.Text.UTF8Encoding $false
[Console]::InputEncoding = $utf8
[Console]::OutputEncoding = $utf8
$OutputEncoding = $utf8
& chcp.com 65001 > $null

function Add-Missing {
    param([string]$Message)
    $script:Missing.Add($Message) | Out-Null
}

function Test-Command {
    param(
        [string]$Name,
        [string[]]$FallbackPaths = @(),
        [bool]$Required = $true
    )

    $cmd = Get-Command $Name -ErrorAction SilentlyContinue
    if ($cmd) {
        Write-Host "[OK] $Name -> $($cmd.Source)"
        return $cmd.Source
    }

    foreach ($path in $FallbackPaths) {
        if (Test-Path $path) {
            Write-Host "[OK] $Name -> $path"
            return $path
        }
    }

    if ($Required) {
        Write-Host "[MISS] $Name"
        Add-Missing "$Name is not available."
    } else {
        Write-Host "[OPT] $Name not found"
    }
    return $null
}

function Test-PathExists {
    param(
        [string]$Label,
        [string[]]$Paths,
        [bool]$Required = $true
    )

    foreach ($path in $Paths) {
        if ($path -and (Test-Path $path)) {
            Write-Host "[OK] $Label -> $path"
            return $path
        }
    }

    if ($Required) {
        Write-Host "[MISS] $Label"
        Add-Missing "$Label was not found."
    } else {
        Write-Host "[OPT] $Label not found"
    }
    return $null
}

Write-Host "== Module_Context Windows build environment =="
Write-Host "Host: $env:COMPUTERNAME"
Write-Host "User: $env:USERNAME"
Write-Host ""

$git = Test-Command "git.exe" @(
    "C:\Program Files\Git\cmd\git.exe",
    "C:\Program Files\Git\bin\git.exe",
    "E:\Program Files\Git\cmd\git.exe",
    "E:\Program Files\Git\bin\git.exe"
)
if ($git) { & $git --version }

$pwsh = Test-Command "pwsh.exe" @(
    "C:\Program Files\PowerShell\7\pwsh.exe"
) -Required $false
if ($pwsh) { & $pwsh --version }

$cmake = Test-Command "cmake.exe" @(
    "C:\Program Files\CMake\bin\cmake.exe",
    "H:\Program Files\CMake\bin\cmake.exe",
    "C:\toolchains\portable\cmake-4.3.2-windows-x86_64\bin\cmake.exe"
)
if ($cmake) { & $cmake --version | Select-Object -First 1 }

$ninja = Test-Command "ninja.exe" @(
    "C:\toolchains\portable\ninja-1.13.2\ninja.exe"
) -Required ([bool]$RequireNinja)
if ($ninja) { & $ninja --version }

$msbuild = Test-Command "MSBuild.exe" @(
    "C:\Program Files (x86)\MSBuild\14.0\Bin\MSBuild.exe",
    "C:\Program Files (x86)\Microsoft Visual Studio\2019\BuildTools\MSBuild\Current\Bin\MSBuild.exe",
    "C:\Program Files\Microsoft Visual Studio\2022\BuildTools\MSBuild\Current\Bin\MSBuild.exe"
) -Required $RequireVS2015
if ($msbuild) { & $msbuild -version | Select-Object -Last 1 }

$vs140VcVars = Test-PathExists "VS2015 vcvarsall.bat" @(
    "C:\Program Files (x86)\Microsoft Visual Studio 14.0\VC\vcvarsall.bat"
) -Required $RequireVS2015

$qtCandidates = @()
if ($QtDir) { $qtCandidates += $QtDir }
$qtCandidates += @(
    "C:\Qt\Qt5.9.7\5.9.7\msvc2015_64",
    "C:\Qt\5.9.7\msvc2015_64"
)
$qtRoot = Test-PathExists "Qt 5.9.7 msvc2015_64" $qtCandidates -Required ([bool]$RequireQt)

if ($qtRoot) {
    Test-PathExists "qmake.exe" @(Join-Path $qtRoot "bin\qmake.exe") -Required ([bool]$RequireQt) | Out-Null
    Test-PathExists "Qt5Config.cmake" @(Join-Path $qtRoot "lib\cmake\Qt5\Qt5Config.cmake") -Required ([bool]$RequireQt) | Out-Null
}

Write-Host ""
if ($script:Missing.Count -eq 0) {
    Write-Host "Environment check passed."
    exit 0
}

Write-Host "Environment check found missing items:"
foreach ($item in $script:Missing) {
    Write-Host " - $item"
}

Write-Host ""
Write-Host "Expected setup:"
Write-Host " - Install Git for Windows or run scripts\ecs\Install-GitForWindows.ps1."
Write-Host " - Install CMake 3.23+ and make cmake.exe available on PATH."
Write-Host " - Install VS2015 C++ x64 tools, including vcvarsall.bat."
Write-Host " - Install Qt 5.9.7 msvc2015_64 and set QT597_MSVC2015_64_DIR if not under C:\Qt."
exit 2
