param(
    [string]$InstallRoot = '',
    [string]$RabbitMqBase = '',
    [int]$StartupTimeoutSeconds = 90
)

$ErrorActionPreference = 'Stop'

. (Join-Path $PSScriptRoot 'TaskFlowFieldConfig.ps1')

function Get-PackageRoot {
    return (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
}

function Import-MasterEnv {
    $envFile = Join-Path (Get-PackageRoot) 'runtime\master_env.ps1'
    if (Test-Path -LiteralPath $envFile) {
        . $envFile
    }
}

function Test-RabbitMqRunning {
    $diagnostics = Join-Path $env:RABBITMQ_HOME 'sbin\rabbitmq-diagnostics.bat'
    if (-not (Test-Path -LiteralPath $diagnostics)) {
        return $false
    }

    $previousPreference = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    try {
        & $diagnostics ping 1>$null 2>$null
        return ($LASTEXITCODE -eq 0)
    } catch {
        return $false
    } finally {
        $ErrorActionPreference = $previousPreference
    }
}

function Test-RabbitMqManagementApi {
    try {
        Invoke-WebRequest -Uri 'http://127.0.0.1:15672/' -UseBasicParsing -TimeoutSec 3 | Out-Null
        return $true
    } catch {
        if ($null -ne $_.Exception.Response) {
            return $true
        }
        return $false
    }
}

function Enable-ManagementPlugin {
    param([switch]$Offline)

    $plugins = Join-Path $env:RABBITMQ_HOME 'sbin\rabbitmq-plugins.bat'
    $arguments = @('enable', 'rabbitmq_management')
    if ($Offline) {
        $arguments += '--offline'
    }

    & $plugins @arguments | Out-Host
    if ($LASTEXITCODE -ne 0) {
        throw "Failed to enable rabbitmq_management plugin. Exit code: $LASTEXITCODE"
    }
}

function Wait-RabbitMqReady {
    param([int]$TimeoutSeconds)

    $deadline = (Get-Date).AddSeconds($TimeoutSeconds)
    do {
        if ((Test-RabbitMqRunning) -and (Test-RabbitMqManagementApi)) {
            Write-Host '[rabbitmq] node is running'
            Write-Host '[rabbitmq] management API: http://127.0.0.1:15672'
            return
        }
        Start-Sleep -Seconds 2
    } while ((Get-Date) -lt $deadline)

    throw 'Timed out waiting for RabbitMQ to start.'
}

$packageRoot = Get-PackageRoot
$fieldConfig = Import-TaskFlowFieldConfig -PackageRoot $packageRoot
if ([string]::IsNullOrWhiteSpace($InstallRoot)) {
    $InstallRoot = [string]$fieldConfig.Master.InstallRoot
}

Import-MasterEnv
if ([string]::IsNullOrWhiteSpace($env:RABBITMQ_HOME)) {
    $candidate = Get-ChildItem -LiteralPath (Join-Path $InstallRoot 'tools') -Directory -Filter 'rabbitmq_server-*' -ErrorAction SilentlyContinue |
        Select-Object -First 1
    if ($null -ne $candidate) {
        $env:RABBITMQ_HOME = $candidate.FullName
    }
}
if ([string]::IsNullOrWhiteSpace($env:ERLANG_HOME)) {
    $candidate = Get-ChildItem -LiteralPath (Join-Path $InstallRoot 'tools') -Directory -Filter 'erl-*' -ErrorAction SilentlyContinue |
        Select-Object -First 1
    if ($null -ne $candidate) {
        $env:ERLANG_HOME = $candidate.FullName
    }
}
if ([string]::IsNullOrWhiteSpace($RabbitMqBase)) {
    $RabbitMqBase = Join-Path $InstallRoot 'rabbitmq-base'
}

if ([string]::IsNullOrWhiteSpace($env:RABBITMQ_HOME) -or -not (Test-Path -LiteralPath (Join-Path $env:RABBITMQ_HOME 'sbin\rabbitmq-server.bat'))) {
    throw 'RABBITMQ_HOME is not configured. Run Install-MasterPrereqs.ps1 first.'
}
if ([string]::IsNullOrWhiteSpace($env:ERLANG_HOME) -or -not (Test-Path -LiteralPath (Join-Path $env:ERLANG_HOME 'bin\erl.exe'))) {
    throw 'ERLANG_HOME is not configured. Run Install-MasterPrereqs.ps1 first.'
}

$env:RABBITMQ_BASE = $RabbitMqBase
$env:PATH = "$env:ERLANG_HOME\bin;$env:RABBITMQ_HOME\sbin;$env:PATH"
New-Item -ItemType Directory -Force -Path $env:RABBITMQ_BASE | Out-Null

if (Test-RabbitMqRunning) {
    Write-Host '[rabbitmq] RabbitMQ is already running; reusing current node'
    if (-not (Test-RabbitMqManagementApi)) {
        Write-Host '[rabbitmq] management API is not ready; enabling management plugin online'
        Enable-ManagementPlugin
        Wait-RabbitMqReady -TimeoutSeconds $StartupTimeoutSeconds
        return
    }
    Write-Host '[rabbitmq] management API: http://127.0.0.1:15672'
    return
}

Write-Host '[rabbitmq] enabling management plugin offline'
Enable-ManagementPlugin -Offline

Write-Host '[rabbitmq] starting server'
& (Join-Path $env:RABBITMQ_HOME 'sbin\rabbitmq-server.bat') -detached | Out-Host
if ($LASTEXITCODE -ne 0) {
    throw "RabbitMQ start command failed. Exit code: $LASTEXITCODE"
}

Wait-RabbitMqReady -TimeoutSeconds $StartupTimeoutSeconds
