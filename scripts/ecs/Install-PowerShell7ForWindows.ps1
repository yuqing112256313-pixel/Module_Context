param(
    [string]$DownloadDir = "C:\Installers",
    [switch]$Force
)

$ErrorActionPreference = "Stop"

function Find-Pwsh {
    $cmd = Get-Command pwsh.exe -ErrorAction SilentlyContinue
    if ($cmd) {
        return $cmd.Source
    }

    $candidates = @(
        "C:\Program Files\PowerShell\7\pwsh.exe",
        "C:\Program Files\PowerShell\7-preview\pwsh.exe"
    )
    foreach ($candidate in $candidates) {
        if (Test-Path $candidate) {
            return $candidate
        }
    }

    return $null
}

$existingPwsh = Find-Pwsh
if ($existingPwsh -and -not $Force) {
    Write-Host "PowerShell 7 is already installed: $existingPwsh"
    & $existingPwsh --version
    exit 0
}

New-Item -ItemType Directory -Force -Path $DownloadDir | Out-Null

[Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12
$release = Invoke-RestMethod -Uri "https://api.github.com/repos/PowerShell/PowerShell/releases/latest"
$asset = $release.assets |
    Where-Object { $_.name -match "^PowerShell-.*-win-x64\.msi$" } |
    Select-Object -First 1

if (-not $asset) {
    throw "Could not find a PowerShell Windows x64 MSI in the latest release."
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

Write-Host "Installing PowerShell 7 silently..."
$process = Start-Process -FilePath "msiexec.exe" `
    -ArgumentList "/i", $installerPath, "/qn", "ADD_PATH=1", "DISABLE_TELEMETRY=1", "/norestart" `
    -Wait -PassThru

if ($process.ExitCode -ne 0) {
    throw "PowerShell installer failed with exit code $($process.ExitCode)."
}

$installedPwsh = Find-Pwsh
if (-not $installedPwsh) {
    throw "PowerShell installer completed, but pwsh.exe was not found."
}

Write-Host "PowerShell installed: $installedPwsh"
& $installedPwsh --version
