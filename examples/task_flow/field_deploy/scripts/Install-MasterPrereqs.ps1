param(
    [string]$InstallRoot = '',
    [string]$ToolsDir = '',
    [switch]$SkipRabbitMqInstall
)

$ErrorActionPreference = 'Stop'

. (Join-Path $PSScriptRoot 'TaskFlowFieldConfig.ps1')

function Assert-Admin {
    $identity = [Security.Principal.WindowsIdentity]::GetCurrent()
    $principal = [Security.Principal.WindowsPrincipal]::new($identity)
    if (-not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
        throw 'Please run this script from an elevated PowerShell session.'
    }
}

function Get-PackageRoot {
    return (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
}

function Add-FirewallRuleIfMissing {
    param(
        [string]$Name,
        [string]$DisplayName,
        [int]$Port
    )

    if (Get-NetFirewallRule -Name $Name -ErrorAction SilentlyContinue) {
        return
    }

    New-NetFirewallRule `
        -Name $Name `
        -DisplayName $DisplayName `
        -Direction Inbound `
        -Action Allow `
        -Protocol TCP `
        -LocalPort $Port | Out-Null
}

function Expand-ZipIfNeeded {
    param(
        [string]$ZipPath,
        [string]$Destination,
        [string]$ExpectedChild
    )

    $expected = Join-Path $Destination $ExpectedChild
    if (Test-Path -LiteralPath $expected) {
        Write-Host "[install] already exists; skip extract: $expected"
        return $expected
    }

    New-Item -ItemType Directory -Force -Path $Destination | Out-Null
    Expand-Archive -LiteralPath $ZipPath -DestinationPath $Destination -Force
    if (-not (Test-Path -LiteralPath $expected)) {
        throw "Expected extracted directory not found: $expected"
    }
    return $expected
}

function Find-ErlangHome {
    param([string]$Root)

    $candidate = Get-ChildItem -LiteralPath $Root -Directory -Filter 'erl-*' -ErrorAction SilentlyContinue |
        Sort-Object LastWriteTime -Descending |
        Select-Object -First 1
    if ($null -ne $candidate -and (Test-Path -LiteralPath (Join-Path $candidate.FullName 'bin\erl.exe'))) {
        return $candidate.FullName
    }
    return ''
}

function Find-RabbitMqHome {
    param([string]$Root)

    if (-not [string]::IsNullOrWhiteSpace($env:RABBITMQ_HOME) -and
        (Test-Path -LiteralPath (Join-Path $env:RABBITMQ_HOME 'sbin\rabbitmq-server.bat'))) {
        return $env:RABBITMQ_HOME
    }

    $candidate = Get-ChildItem -LiteralPath $Root -Directory -Filter 'rabbitmq_server-*' -ErrorAction SilentlyContinue |
        Sort-Object LastWriteTime -Descending |
        Select-Object -First 1
    if ($null -ne $candidate -and (Test-Path -LiteralPath (Join-Path $candidate.FullName 'sbin\rabbitmq-server.bat'))) {
        return $candidate.FullName
    }
    return ''
}

Assert-Admin

$packageRoot = Get-PackageRoot
$fieldConfig = Import-TaskFlowFieldConfig -PackageRoot $packageRoot
if ([string]::IsNullOrWhiteSpace($InstallRoot)) {
    $InstallRoot = [string]$fieldConfig.Master.InstallRoot
}
$installersDir = Join-Path $packageRoot 'installers'
if ([string]::IsNullOrWhiteSpace($ToolsDir)) {
    $ToolsDir = Join-Path $InstallRoot 'tools'
}

New-Item -ItemType Directory -Force -Path $ToolsDir | Out-Null

$rabbitMqHome = Find-RabbitMqHome -Root $ToolsDir
$erlangHome = Find-ErlangHome -Root $ToolsDir

if (-not $SkipRabbitMqInstall) {
    $otpInstaller = Get-ChildItem -LiteralPath $installersDir -File -Filter 'otp_win64_*.exe' |
        Select-Object -First 1
    if ($null -eq $otpInstaller) {
        throw "Erlang OTP installer not found under $installersDir"
    }

    if ([string]::IsNullOrWhiteSpace($erlangHome)) {
        $erlangTarget = Join-Path $ToolsDir 'erl-27.3.4.11'
        Write-Host "[install] Installing Erlang OTP to $erlangTarget"
        $process = Start-Process -FilePath $otpInstaller.FullName `
            -ArgumentList @('/S', "/D=$erlangTarget") `
            -Wait `
            -PassThru
        if ($process.ExitCode -ne 0) {
            throw "Erlang installer exited with code $($process.ExitCode)"
        }
        $erlangHome = Find-ErlangHome -Root $ToolsDir
        if ([string]::IsNullOrWhiteSpace($erlangHome)) {
            $erlangHome = $erlangTarget
        }
    } else {
        Write-Host "[install] Erlang already exists; skip install: $erlangHome"
    }

    $rabbitMqZip = Get-ChildItem -LiteralPath $installersDir -File -Filter 'rabbitmq-server-windows-*.zip' |
        Select-Object -First 1
    if ($null -eq $rabbitMqZip) {
        throw "RabbitMQ server zip not found under $installersDir"
    }

    $rabbitMqHome = Expand-ZipIfNeeded `
        -ZipPath $rabbitMqZip.FullName `
        -Destination $ToolsDir `
        -ExpectedChild 'rabbitmq_server-4.2.5'
} else {
    if ([string]::IsNullOrWhiteSpace($rabbitMqHome)) {
        throw '-SkipRabbitMqInstall was specified, but no usable RabbitMQ was found. Remove the flag or configure RABBITMQ_HOME first.'
    }
    Write-Host "[install] -SkipRabbitMqInstall specified; reusing RabbitMQ: $rabbitMqHome"
}

Add-FirewallRuleIfMissing -Name 'TaskFlowE2E-RabbitMQ-5672' -DisplayName 'TaskFlow E2E RabbitMQ AMQP 5672' -Port 5672
Add-FirewallRuleIfMissing -Name 'TaskFlowE2E-RabbitMQ-15672' -DisplayName 'TaskFlow E2E RabbitMQ Management 15672' -Port 15672
Add-FirewallRuleIfMissing -Name 'TaskFlowE2E-HTTP-50080' -DisplayName 'TaskFlow E2E HTTP image stream 50080' -Port 50080

$envFile = Join-Path $packageRoot 'runtime\master_env.ps1'
New-Item -ItemType Directory -Force -Path (Split-Path -Parent $envFile) | Out-Null
@"
`$env:ERLANG_HOME = '$erlangHome'
`$env:RABBITMQ_HOME = '$rabbitMqHome'
`$env:RABBITMQ_BASE = '$(Join-Path $InstallRoot 'rabbitmq-base')'
`$env:PATH = "`$env:ERLANG_HOME\bin;`$env:RABBITMQ_HOME\sbin;`$env:PATH"
"@ | Set-Content -LiteralPath $envFile -Encoding UTF8

Write-Host "[install] tools dir: $ToolsDir"
Write-Host "[install] Erlang home: $erlangHome"
Write-Host "[install] RabbitMQ home: $rabbitMqHome"
Write-Host "[install] environment file: $envFile"
