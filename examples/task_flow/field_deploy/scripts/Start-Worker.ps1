param(
    [string]$BuildDir = '',
    [string]$Configuration = 'Debug',
    [string]$RabbitMqHost = '',
    [string]$RabbitMqVHost = '',
    [string]$RabbitMqWorkerUser = '',
    [string]$RabbitMqWorkerPass = '',
    [string]$WorkerHostId = '',
    [int]$LocalWorkerCount = 0,
    [int]$WorkerTaskThreads = 0,
    [int]$AlgorithmDelayMs = -1,
    [string]$AlgorithmPluginName = '',
    [string]$AlgorithmPluginPath = '',
    [string]$AlgorithmPluginCreateFunc = '',
    [string]$AlgorithmPluginDestroyFunc = '',
    [string]$AlgorithmInputDir = '',
    [switch]$PersistAlgorithmInputImages,
    [switch]$DisableAlgorithmPlugin,
    [string]$HttpEndpoint = '',
    [string]$HttpRoute = '',
    [long]$HttpChunkBytes = 0,
    [string]$OutputRoot = '',
    [int]$TimeoutMs = -1,
    [switch]$Restart
)

$ErrorActionPreference = 'Stop'

. (Join-Path $PSScriptRoot 'TaskFlowFieldConfig.ps1')

function Get-DefaultWorkerHostId {
    $addresses = @(Get-NetIPAddress -AddressFamily IPv4 -ErrorAction SilentlyContinue |
        Where-Object { $script:WorkerIpToId.ContainsKey([string]$_.IPAddress) } |
        Select-Object -ExpandProperty IPAddress)
    if ($addresses.Count -eq 1) {
        return $script:WorkerIpToId[[string]$addresses[0]]
    }
    if ($addresses.Count -gt 1) {
        throw "Multiple Task Flow worker IPs were found on this host: $($addresses -join ', '). Pass -WorkerHostId explicitly."
    }

    throw "Cannot infer WorkerHostId from local IP. Verify this host matches config\TaskFlowFieldConfig.psd1, or pass -WorkerHostId explicitly."
}

$packageRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
$fieldConfig = Import-TaskFlowFieldConfig -PackageRoot $packageRoot
$masterConfig = $fieldConfig.Master
$rabbitConfig = $fieldConfig.RabbitMq
$workerConfig = $fieldConfig.Worker
$testConfig = $fieldConfig.Test
$httpConfig = if ($fieldConfig.ContainsKey('Http')) { $fieldConfig.Http } else { @{} }
$algorithmConfig = if ($workerConfig.ContainsKey('Algorithm')) { $workerConfig.Algorithm } else { @{} }
$defaultHttpChunkBytes = if ($httpConfig.ChunkBytes) { [long]$httpConfig.ChunkBytes } else { 8388608 }
$WorkerIpToId = Get-TaskFlowWorkerMap -Nodes @($workerConfig.Nodes)

if ([string]::IsNullOrWhiteSpace($BuildDir)) {
    $BuildDir = Join-Path $packageRoot 'build'
}
if ([string]::IsNullOrWhiteSpace($OutputRoot)) {
    $OutputRoot = [string]$workerConfig.OutputRoot
}
$RabbitMqHost = Get-TaskFlowString -Value $RabbitMqHost -Fallback ([string]$masterConfig.Host)
$RabbitMqVHost = Get-TaskFlowString -Value $RabbitMqVHost -Fallback ([string]$rabbitConfig.VHost)
$RabbitMqWorkerUser = Get-TaskFlowString -Value $RabbitMqWorkerUser -Fallback ([string]$rabbitConfig.WorkerUser)
$RabbitMqWorkerPass = Get-TaskFlowString -Value $RabbitMqWorkerPass -Fallback ([string]$rabbitConfig.WorkerPass)
$LocalWorkerCount = Get-TaskFlowInt -Value $LocalWorkerCount -Fallback ([int]$workerConfig.LocalWorkerCount)
$WorkerTaskThreads = Get-TaskFlowInt -Value $WorkerTaskThreads -Fallback ([int]$workerConfig.WorkerTaskThreads)
$AlgorithmDelayMs = Get-TaskFlowNonNegativeInt -Value $AlgorithmDelayMs -Fallback ([int]$workerConfig.AlgorithmDelayMs)
$UseAlgorithmPlugin = if ($DisableAlgorithmPlugin) {
    $false
} elseif ($algorithmConfig.ContainsKey('Enabled')) {
    [bool]$algorithmConfig.Enabled
} else {
    $true
}
$defaultAlgorithmPluginName = if ($algorithmConfig.ContainsKey('PluginName')) { [string]$algorithmConfig.PluginName } else { 'tgv_etching' }
$defaultAlgorithmPluginPath = if ($algorithmConfig.ContainsKey('PluginPath')) { [string]$algorithmConfig.PluginPath } else { '' }
$defaultAlgorithmPluginCreateFunc = if ($algorithmConfig.ContainsKey('CreateFunc')) { [string]$algorithmConfig.CreateFunc } else { 'CreatePluginEtching' }
$defaultAlgorithmPluginDestroyFunc = if ($algorithmConfig.ContainsKey('DestroyFunc')) { [string]$algorithmConfig.DestroyFunc } else { 'DestroyPluginEtching' }
$defaultAlgorithmInputDir = if ($algorithmConfig.ContainsKey('InputDir')) { [string]$algorithmConfig.InputDir } else { '' }
$AlgorithmPluginName = Get-TaskFlowString -Value $AlgorithmPluginName -Fallback $defaultAlgorithmPluginName
$AlgorithmPluginPath = Get-TaskFlowString -Value $AlgorithmPluginPath -Fallback $defaultAlgorithmPluginPath
$AlgorithmPluginCreateFunc = Get-TaskFlowString -Value $AlgorithmPluginCreateFunc -Fallback $defaultAlgorithmPluginCreateFunc
$AlgorithmPluginDestroyFunc = Get-TaskFlowString -Value $AlgorithmPluginDestroyFunc -Fallback $defaultAlgorithmPluginDestroyFunc
$AlgorithmPersistInputImages = if ($PersistAlgorithmInputImages) {
    $true
} elseif ($algorithmConfig.ContainsKey('PersistInputImages')) {
    [bool]$algorithmConfig.PersistInputImages
} else {
    $false
}
$AlgorithmInputDir = Get-TaskFlowString -Value $AlgorithmInputDir -Fallback $defaultAlgorithmInputDir
if ([string]::IsNullOrWhiteSpace($HttpEndpoint)) {
    $HttpEndpoint = Get-TaskFlowHttpEndpoint `
        -MasterHost ([string]$masterConfig.Host) `
        -HttpPort ([int]$masterConfig.HttpPort)
}
$HttpRoute = Get-TaskFlowString -Value $HttpRoute -Fallback ([string]$masterConfig.HttpRoute)
$HttpChunkBytes = Get-TaskFlowLong -Value $HttpChunkBytes -Fallback $defaultHttpChunkBytes
$TimeoutMs = Get-TaskFlowNonNegativeInt -Value $TimeoutMs -Fallback ([int]$testConfig.TimeoutMs)
if ([string]::IsNullOrWhiteSpace($WorkerHostId)) {
    $WorkerHostId = Get-DefaultWorkerHostId
    Write-Host "[worker] inferred WorkerHostId: $WorkerHostId"
}

$runner = Join-Path $PSScriptRoot 'run_task_flow_worker.ps1'
if (-not (Test-Path -LiteralPath $runner)) {
    throw "Internal runner not found: $runner"
}

$runnerArgs = @{
    BuildDir = $BuildDir
    Configuration = $Configuration
    RabbitMqHost = $RabbitMqHost
    RabbitMqVHost = $RabbitMqVHost
    RabbitMqWorkerUser = $RabbitMqWorkerUser
    RabbitMqWorkerPass = $RabbitMqWorkerPass
    WorkerHostId = $WorkerHostId
    LocalWorkerCount = $LocalWorkerCount
    WorkerTaskThreads = $WorkerTaskThreads
    AlgorithmDelayMs = $AlgorithmDelayMs
    UseAlgorithmPlugin = $UseAlgorithmPlugin
    AlgorithmPluginName = $AlgorithmPluginName
    AlgorithmPluginPath = $AlgorithmPluginPath
    AlgorithmPluginCreateFunc = $AlgorithmPluginCreateFunc
    AlgorithmPluginDestroyFunc = $AlgorithmPluginDestroyFunc
    AlgorithmPersistInputImages = $AlgorithmPersistInputImages
    AlgorithmInputDir = $AlgorithmInputDir
    HttpEndpoint = $HttpEndpoint
    HttpRoute = $HttpRoute
    HttpChunkBytes = $HttpChunkBytes
    OutputRoot = $OutputRoot
    TimeoutMs = $TimeoutMs
    UseHostIdAsWorkerId = $true
}
if ($Restart) {
    $runnerArgs.Restart = $true
}

& $runner @runnerArgs
