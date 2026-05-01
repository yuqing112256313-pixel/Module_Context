param(
    [string]$RepoUrl = "https://github.com/yuqing112256313-pixel/Module_Context.git",
    [string]$WorkDir = "C:\work\Module_Context",
    [string]$Branch = "main",
    [string]$Preset = "windows-vs2015-x64-debug",
    [string]$GitHttpProxy = "",
    [switch]$SkipBuild,
    [switch]$SkipTests
)

$ErrorActionPreference = "Stop"

$utf8 = New-Object System.Text.UTF8Encoding $false
[Console]::InputEncoding = $utf8
[Console]::OutputEncoding = $utf8
$OutputEncoding = $utf8
& chcp.com 65001 > $null

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

function Invoke-Checked {
    param(
        [string]$FilePath,
        [string[]]$Arguments
    )

    & $FilePath @Arguments
    if ($LASTEXITCODE -ne 0) {
        throw "$FilePath failed with exit code $LASTEXITCODE."
    }
}

function Test-TcpPort {
    param(
        [string]$HostName,
        [int]$Port,
        [int]$TimeoutMs = 500
    )

    $client = New-Object System.Net.Sockets.TcpClient
    try {
        $async = $client.BeginConnect($HostName, $Port, $null, $null)
        if (-not $async.AsyncWaitHandle.WaitOne($TimeoutMs)) {
            return $false
        }
        $client.EndConnect($async)
        return $client.Connected
    } catch {
        return $false
    } finally {
        $client.Close()
    }
}

function Get-GitProxyArguments {
    $proxy = $GitHttpProxy
    if ([string]::IsNullOrWhiteSpace($proxy)) {
        $proxy = [Environment]::GetEnvironmentVariable("REMOTE_GIT_HTTP_PROXY")
    }
    if ([string]::IsNullOrWhiteSpace($proxy) -and
        (Test-TcpPort -HostName "127.0.0.1" -Port 7897 -TimeoutMs 500)) {
        $proxy = "http://127.0.0.1:7897"
    }
    if ([string]::IsNullOrWhiteSpace($proxy)) {
        return @()
    }

    Write-Host "Using Git HTTP proxy: $proxy"
    return @("-c", "http.proxy=$proxy", "-c", "https.proxy=$proxy")
}

$git = Find-Exe "git.exe" @(
    "C:\Program Files\Git\cmd\git.exe",
    "C:\Program Files\Git\bin\git.exe"
)
$gitProxyArgs = Get-GitProxyArguments

function Invoke-GitChecked {
    param([string[]]$Arguments)
    Invoke-Checked $git ($gitProxyArgs + $Arguments)
}

$parent = Split-Path -Parent $WorkDir
New-Item -ItemType Directory -Force -Path $parent | Out-Null

if (Test-Path (Join-Path $WorkDir ".git")) {
    Write-Host "Updating existing checkout: $WorkDir"
    Invoke-GitChecked @("-C", $WorkDir, "fetch", "origin")
    Invoke-GitChecked @("-C", $WorkDir, "checkout", $Branch)
    Invoke-GitChecked @("-C", $WorkDir, "pull", "--ff-only", "origin", $Branch)
} else {
    if (Test-Path $WorkDir) {
        throw "$WorkDir exists but is not a Git checkout."
    }

    Write-Host "Cloning $RepoUrl -> $WorkDir"
    Invoke-GitChecked @("clone", "--branch", $Branch, $RepoUrl, $WorkDir)
}

$envCheck = Join-Path $WorkDir "scripts\ecs\Test-WindowsBuildEnv.ps1"
if (Test-Path $envCheck) {
    Write-Host "Running environment check..."
    $requireVs2015 = $Preset -match "vs2015"
    $requireQt = $Preset -match "qt"
    $requireNinja = $Preset -match "ninja|mingw|llvm"
    $psExe = "powershell"
    $pwsh = Get-Command pwsh.exe -ErrorAction SilentlyContinue
    if (-not $pwsh -and (Test-Path "C:\Program Files\PowerShell\7\pwsh.exe")) {
        $psExe = "C:\Program Files\PowerShell\7\pwsh.exe"
    } elseif ($pwsh) {
        $psExe = $pwsh.Source
    }

    $envCheckArgs = @(
        "-NoProfile",
        "-ExecutionPolicy", "Bypass",
        "-File", $envCheck
    )
    if ($requireVs2015) { $envCheckArgs += "-RequireVS2015" }
    if ($requireQt) { $envCheckArgs += "-RequireQt" }
    if ($requireNinja) { $envCheckArgs += "-RequireNinja" }
    & $psExe @envCheckArgs
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
Push-Location $WorkDir
try {
    if ($Preset -match "vs2015") {
        $env:CodeAnalysisTargets = "CodeAnalysisTargets_disabled"
    }

    Invoke-Checked $cmake @("--preset", $Preset)

    Write-Host "Building preset: $Preset"
    Invoke-Checked $cmake @("--build", "--preset", $Preset)

    if (-not $SkipTests) {
        Write-Host "Testing preset: $Preset"
        $ctest = Join-Path (Split-Path -Parent $cmake) "ctest.exe"
        if (-not (Test-Path $ctest)) {
            $ctest = Find-Exe "ctest.exe"
        }
        Invoke-Checked $ctest @("--preset", $Preset)
    }
} finally {
    Pop-Location
}
