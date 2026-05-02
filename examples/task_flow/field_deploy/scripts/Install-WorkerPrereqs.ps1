param(
    [string]$RabbitMqHost = '',
    [int]$RabbitMqPort = 0,
    [switch]$SkipFirewall
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

Assert-Admin
$packageRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
$fieldConfig = Import-TaskFlowFieldConfig -PackageRoot $packageRoot
$RabbitMqHost = Get-TaskFlowString -Value $RabbitMqHost -Fallback ([string]$fieldConfig.Master.Host)
$RabbitMqPort = Get-TaskFlowInt -Value $RabbitMqPort -Fallback ([int]$fieldConfig.RabbitMq.AmqpPort)
$bin = Join-Path $packageRoot 'build\examples\task_flow'
foreach ($name in @('mc_task_flow_worker_host.exe', 'amqp_bus.dll', 'http_transport.dll', 'semiplugin_manager.dll', 'tgv_etching_semiplugin.dll', 'libmc_core_framework.dll', 'libc++.dll', 'libunwind.dll')) {
    $path = Join-Path $bin $name
    if (-not (Test-Path -LiteralPath $path)) {
        throw "Required runtime file missing: $path"
    }
}

if (-not $SkipFirewall) {
    if (-not (Get-NetFirewallRule -Name 'TaskFlowE2E-Worker-Out' -ErrorAction SilentlyContinue)) {
        New-NetFirewallRule `
            -Name 'TaskFlowE2E-Worker-Out' `
            -DisplayName 'TaskFlow E2E Worker outbound' `
            -Direction Outbound `
            -Action Allow `
            -Program (Join-Path $bin 'mc_task_flow_worker_host.exe') | Out-Null
        Write-Host '[worker-install] worker outbound firewall rule created'
    } else {
        Write-Host '[worker-install] worker outbound firewall rule already exists; skip'
    }
}

if (-not [string]::IsNullOrWhiteSpace($RabbitMqHost)) {
    Test-NetConnection -ComputerName $RabbitMqHost -Port $RabbitMqPort | Select-Object ComputerName, RemotePort, TcpTestSucceeded | Format-List
}

Write-Host "[worker-install] runtime files verified under $bin"
