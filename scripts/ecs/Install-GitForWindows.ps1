param(
    [string]$DownloadDir = "C:\Installers",
    [switch]$Force
)

$ErrorActionPreference = "Stop"

function Find-Git {
    $cmd = Get-Command git.exe -ErrorAction SilentlyContinue
    if ($cmd) {
        return $cmd.Source
    }

    $candidates = @(
        "C:\Program Files\Git\cmd\git.exe",
        "C:\Program Files\Git\bin\git.exe"
    )
    foreach ($candidate in $candidates) {
        if (Test-Path $candidate) {
            return $candidate
        }
    }

    return $null
}

$existingGit = Find-Git
if ($existingGit -and -not $Force) {
    Write-Host "Git is already installed: $existingGit"
    & $existingGit --version
    exit 0
}

New-Item -ItemType Directory -Force -Path $DownloadDir | Out-Null

[Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12
$release = Invoke-RestMethod -Uri "https://api.github.com/repos/git-for-windows/git/releases/latest"
$asset = $release.assets |
    Where-Object { $_.name -match "^Git-.*-64-bit\.exe$" } |
    Select-Object -First 1

if (-not $asset) {
    throw "Could not find a 64-bit Git for Windows installer in the latest release."
}

$installerPath = Join-Path $DownloadDir $asset.name
if (-not (Test-Path $installerPath) -or $Force) {
    Write-Host "Downloading $($asset.name)..."
    Invoke-WebRequest -Uri $asset.browser_download_url -OutFile $installerPath
}

Write-Host "Installing Git for Windows silently..."
$process = Start-Process -FilePath $installerPath `
    -ArgumentList "/VERYSILENT", "/NORESTART", "/NOCANCEL", "/SP-" `
    -Wait -PassThru

if ($process.ExitCode -ne 0) {
    throw "Git installer failed with exit code $($process.ExitCode)."
}

$installedGit = Find-Git
if (-not $installedGit) {
    throw "Git installer completed, but git.exe was not found."
}

Write-Host "Git installed: $installedGit"
& $installedGit --version
