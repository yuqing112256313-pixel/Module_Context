param(
    [string]$DownloadDir = "C:\Installers",
    [switch]$Force
)

$ErrorActionPreference = "Stop"

function Find-CMake {
    $cmd = Get-Command cmake.exe -ErrorAction SilentlyContinue
    if ($cmd) {
        return $cmd.Source
    }

    $candidates = @(
        "C:\Program Files\CMake\bin\cmake.exe",
        "C:\toolchains\portable\cmake-4.3.2-windows-x86_64\bin\cmake.exe"
    )
    foreach ($candidate in $candidates) {
        if (Test-Path $candidate) {
            return $candidate
        }
    }

    return $null
}

$existingCMake = Find-CMake
if ($existingCMake -and -not $Force) {
    Write-Host "CMake is already installed: $existingCMake"
    & $existingCMake --version | Select-Object -First 1
    exit 0
}

New-Item -ItemType Directory -Force -Path $DownloadDir | Out-Null

[Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12
$release = Invoke-RestMethod -Uri "https://api.github.com/repos/Kitware/CMake/releases/latest"
$asset = $release.assets |
    Where-Object { $_.name -match "^cmake-.*-windows-x86_64\.msi$" } |
    Select-Object -First 1

if (-not $asset) {
    throw "Could not find a Windows x86_64 CMake MSI in the latest release."
}

$installerPath = Join-Path $DownloadDir $asset.name
if (-not (Test-Path $installerPath) -or $Force) {
    Write-Host "Downloading $($asset.name)..."
    & curl.exe -L --fail --connect-timeout 20 --max-time 600 `
        -o $installerPath $asset.browser_download_url
    if ($LASTEXITCODE -ne 0) {
        throw "curl failed with exit code $LASTEXITCODE."
    }
}

Write-Host "Installing CMake silently..."
$process = Start-Process -FilePath "msiexec.exe" `
    -ArgumentList "/i", $installerPath, "/qn", "ADD_CMAKE_TO_PATH=System", "/norestart" `
    -Wait -PassThru

if ($process.ExitCode -ne 0) {
    throw "CMake installer failed with exit code $($process.ExitCode)."
}

$installedCMake = Find-CMake
if (-not $installedCMake) {
    throw "CMake installer completed, but cmake.exe was not found."
}

Write-Host "CMake installed: $installedCMake"
& $installedCMake --version | Select-Object -First 1
