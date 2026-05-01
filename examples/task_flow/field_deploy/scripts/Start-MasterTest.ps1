param(
    [string]$BuildDir = '',
    [string]$Configuration = 'Debug',
    [string]$RabbitMqApiUrl = '',
    [string]$RabbitMqHost = '',
    [string]$RabbitMqAdminUser = '',
    [string]$RabbitMqAdminPass = '',
    [string]$RabbitMqVHost = '',
    [string]$RabbitMqMasterUser = '',
    [string]$RabbitMqMasterPass = '',
    [string]$RabbitMqWorkerUser = '',
    [string]$RabbitMqWorkerPass = '',
    [int]$ExpectedWorkerCount = 0,
    [int]$DurationSeconds = 0,
    [int]$PublishRatePerSecond = 0,
    [long]$ImageSizeBytes = 0,
    [int]$AlgorithmDelayMs = -1,
    [string]$MasterWritePublishThreads = '',
    [string]$MasterResultThreads = '',
    [int]$WorkerTaskThreads = 0,
    [string]$HttpListenAddress = '',
    [int]$HttpPort = 0,
    [string]$HttpRoute = '',
    [int]$HttpServerThreadCount = 0,
    [long]$HttpChunkBytes = 0,
    [long]$HttpReadBufferBytes = -1,
    [long]$HttpWriteBufferBytes = -1,
    [long]$HttpSocketReceiveBufferBytes = -1,
    [long]$HttpSocketSendBufferBytes = -1,
    [long]$ImageStoreCapacityBytes = 0,
    [string]$OutputRoot = '',
    [int]$WorkerReadyTimeoutSeconds = -1,
    [int]$TimeoutMs = -1,
    [switch]$Sweep,
    [switch]$Restart
)

$ErrorActionPreference = 'Stop'

. (Join-Path $PSScriptRoot 'TaskFlowFieldConfig.ps1')

$packageRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
$fieldConfig = Import-TaskFlowFieldConfig -PackageRoot $packageRoot
$masterConfig = $fieldConfig.Master
$rabbitConfig = $fieldConfig.RabbitMq
$workerConfig = $fieldConfig.Worker
$testConfig = $fieldConfig.Test
$httpConfig = if ($fieldConfig.ContainsKey('Http')) { $fieldConfig.Http } else { @{} }
$defaultHttpServerThreadCount = if ($httpConfig.ServerThreadCount) { [int]$httpConfig.ServerThreadCount } else { 64 }
$defaultHttpChunkBytes = if ($httpConfig.ChunkBytes) { [long]$httpConfig.ChunkBytes } else { 8388608 }
$defaultHttpReadBufferBytes = if ($httpConfig.ContainsKey('ReadBufferBytes')) { [long]$httpConfig.ReadBufferBytes } else { 0 }
$defaultHttpWriteBufferBytes = if ($httpConfig.ContainsKey('WriteBufferBytes')) { [long]$httpConfig.WriteBufferBytes } else { 0 }
$defaultHttpSocketReceiveBufferBytes = if ($httpConfig.ContainsKey('SocketReceiveBufferBytes')) { [long]$httpConfig.SocketReceiveBufferBytes } else { 0 }
$defaultHttpSocketSendBufferBytes = if ($httpConfig.ContainsKey('SocketSendBufferBytes')) { [long]$httpConfig.SocketSendBufferBytes } else { 0 }

function Get-TaskFlowNonNegativeLong {
    param(
        [long]$Value,
        [long]$Fallback
    )

    if ($Value -ge 0) {
        return $Value
    }
    return $Fallback
}

function Get-TaskFlowProfileValue {
    param(
        [object]$Profile,
        [string]$Key,
        [long]$Fallback
    )

    if ($null -ne $Profile -and $Profile.ContainsKey($Key)) {
        return [long]$Profile[$Key]
    }
    return $Fallback
}

function Get-TaskFlowProfileName {
    param(
        [object]$Profile,
        [int]$Index
    )

    $name = if ($null -ne $Profile -and $Profile.ContainsKey('Name')) { [string]$Profile.Name } else { '' }
    if ([string]::IsNullOrWhiteSpace($name)) {
        $name = 'profile-{0:00}' -f ($Index + 1)
    }
    $safe = $name -replace '[^A-Za-z0-9_.-]', '-'
    if ([string]::IsNullOrWhiteSpace($safe)) {
        $safe = 'profile-{0:00}' -f ($Index + 1)
    }
    return $safe
}

function Test-TaskFlowProfileEnabled {
    param([object]$Profile)

    if ($null -ne $Profile -and $Profile.ContainsKey('Enabled')) {
        return [bool]$Profile.Enabled
    }
    return $true
}

function Import-TaskFlowKeyValueFile {
    param([string]$Path)

    $values = @{}
    foreach ($line in Get-Content -LiteralPath $Path) {
        if ([string]::IsNullOrWhiteSpace($line)) {
            continue
        }
        $parts = $line -split '=', 2
        if ($parts.Count -eq 2) {
            $values[$parts[0]] = $parts[1]
        }
    }
    return $values
}

function ConvertTo-TaskFlowDouble {
    param([object]$Value)

    if ($null -eq $Value) {
        return 0.0
    }
    $text = [string]$Value
    if ([string]::IsNullOrWhiteSpace($text)) {
        return 0.0
    }
    return [double]::Parse($text, [System.Globalization.CultureInfo]::InvariantCulture)
}

function Get-TaskFlowAverage {
    param(
        [object[]]$Rows,
        [string]$Column
    )

    if ($Rows.Count -eq 0) {
        return 0.0
    }
    $sum = 0.0
    foreach ($row in $Rows) {
        $property = $row.PSObject.Properties[$Column]
        if ($null -ne $property) {
            $sum += ConvertTo-TaskFlowDouble $property.Value
        }
    }
    return $sum / [double]$Rows.Count
}

function Get-TaskFlowMax {
    param(
        [object[]]$Rows,
        [string]$Column
    )

    $max = 0.0
    foreach ($row in $Rows) {
        $property = $row.PSObject.Properties[$Column]
        $value = if ($null -ne $property) { ConvertTo-TaskFlowDouble $property.Value } else { 0.0 }
        if ($value -gt $max) {
            $max = $value
        }
    }
    return $max
}

function Format-TaskFlowNumber {
    param([double]$Value)
    return $Value.ToString('0.###', [System.Globalization.CultureInfo]::InvariantCulture)
}

if ([string]::IsNullOrWhiteSpace($BuildDir)) {
    $BuildDir = Join-Path $packageRoot 'build'
}
if ([string]::IsNullOrWhiteSpace($OutputRoot)) {
    $OutputRoot = [string]$masterConfig.OutputRoot
}
$masterHost = [string]$masterConfig.Host
$rabbitManagementHost = if ($rabbitConfig.ContainsKey('ManagementHost')) {
    [string]$rabbitConfig.ManagementHost
} else {
    '127.0.0.1'
}
$rabbitManagementPort = [int]$rabbitConfig.ManagementPort
$RabbitMqApiUrl = Get-TaskFlowString `
    -Value $RabbitMqApiUrl `
    -Fallback (Get-TaskFlowApiUrl `
        -MasterHost $rabbitManagementHost `
        -ManagementPort $rabbitManagementPort `
        -ApiPath ([string]$rabbitConfig.ApiPath))
$RabbitMqHost = Get-TaskFlowString -Value $RabbitMqHost -Fallback $masterHost
$RabbitMqAdminUser = Get-TaskFlowString -Value $RabbitMqAdminUser -Fallback ([string]$rabbitConfig.AdminUser)
$RabbitMqAdminPass = Get-TaskFlowString -Value $RabbitMqAdminPass -Fallback ([string]$rabbitConfig.AdminPass)
$RabbitMqVHost = Get-TaskFlowString -Value $RabbitMqVHost -Fallback ([string]$rabbitConfig.VHost)
$RabbitMqMasterUser = Get-TaskFlowString -Value $RabbitMqMasterUser -Fallback ([string]$rabbitConfig.MasterUser)
$RabbitMqMasterPass = Get-TaskFlowString -Value $RabbitMqMasterPass -Fallback ([string]$rabbitConfig.MasterPass)
$RabbitMqWorkerUser = Get-TaskFlowString -Value $RabbitMqWorkerUser -Fallback ([string]$rabbitConfig.WorkerUser)
$RabbitMqWorkerPass = Get-TaskFlowString -Value $RabbitMqWorkerPass -Fallback ([string]$rabbitConfig.WorkerPass)
$ExpectedWorkerCount = Get-TaskFlowInt -Value $ExpectedWorkerCount -Fallback ([int]$testConfig.ExpectedWorkerCount)
$DurationSeconds = Get-TaskFlowInt -Value $DurationSeconds -Fallback ([int]$testConfig.DurationSeconds)
$PublishRatePerSecond = Get-TaskFlowInt -Value $PublishRatePerSecond -Fallback ([int]$testConfig.PublishRatePerSecond)
$ImageSizeBytes = Get-TaskFlowLong -Value $ImageSizeBytes -Fallback ([long]$testConfig.ImageSizeBytes)
$AlgorithmDelayMs = Get-TaskFlowNonNegativeInt -Value $AlgorithmDelayMs -Fallback ([int]$workerConfig.AlgorithmDelayMs)
$MasterWritePublishThreads = Get-TaskFlowString -Value $MasterWritePublishThreads -Fallback ([string]$testConfig.MasterWritePublishThreads)
$MasterResultThreads = Get-TaskFlowString -Value $MasterResultThreads -Fallback ([string]$testConfig.MasterResultThreads)
$WorkerTaskThreads = Get-TaskFlowInt -Value $WorkerTaskThreads -Fallback ([int]$workerConfig.WorkerTaskThreads)
$HttpListenAddress = Get-TaskFlowString -Value $HttpListenAddress -Fallback ([string]$masterConfig.HttpListenAddress)
$HttpPort = Get-TaskFlowInt -Value $HttpPort -Fallback ([int]$masterConfig.HttpPort)
$HttpRoute = Get-TaskFlowString -Value $HttpRoute -Fallback ([string]$masterConfig.HttpRoute)
$HttpServerThreadCount = Get-TaskFlowInt -Value $HttpServerThreadCount -Fallback $defaultHttpServerThreadCount
$HttpChunkBytes = Get-TaskFlowLong -Value $HttpChunkBytes -Fallback $defaultHttpChunkBytes
$HttpReadBufferBytes = Get-TaskFlowNonNegativeLong -Value $HttpReadBufferBytes -Fallback $defaultHttpReadBufferBytes
$HttpWriteBufferBytes = Get-TaskFlowNonNegativeLong -Value $HttpWriteBufferBytes -Fallback $defaultHttpWriteBufferBytes
$HttpSocketReceiveBufferBytes = Get-TaskFlowNonNegativeLong -Value $HttpSocketReceiveBufferBytes -Fallback $defaultHttpSocketReceiveBufferBytes
$HttpSocketSendBufferBytes = Get-TaskFlowNonNegativeLong -Value $HttpSocketSendBufferBytes -Fallback $defaultHttpSocketSendBufferBytes
$ImageStoreCapacityBytes = Get-TaskFlowLong -Value $ImageStoreCapacityBytes -Fallback ([long]$testConfig.ImageStoreCapacityBytes)
$WorkerReadyTimeoutSeconds = Get-TaskFlowNonNegativeInt -Value $WorkerReadyTimeoutSeconds -Fallback ([int]$testConfig.WorkerReadyTimeoutSeconds)
$TimeoutMs = Get-TaskFlowNonNegativeInt -Value $TimeoutMs -Fallback ([int]$testConfig.TimeoutMs)

$runner = Join-Path $PSScriptRoot 'run_task_flow_master.ps1'
if (-not (Test-Path -LiteralPath $runner)) {
    throw "Internal runner not found: $runner"
}

$baseRunnerArgs = @{
    BuildDir = $BuildDir
    Configuration = $Configuration
    RabbitMqApiUrl = $RabbitMqApiUrl
    RabbitMqHost = $RabbitMqHost
    RabbitMqAdminUser = $RabbitMqAdminUser
    RabbitMqAdminPass = $RabbitMqAdminPass
    RabbitMqVHost = $RabbitMqVHost
    RabbitMqMasterUser = $RabbitMqMasterUser
    RabbitMqMasterPass = $RabbitMqMasterPass
    RabbitMqWorkerUser = $RabbitMqWorkerUser
    RabbitMqWorkerPass = $RabbitMqWorkerPass
    ExpectedWorkerCount = $ExpectedWorkerCount
    DurationSeconds = $DurationSeconds
    PublishRatePerSecond = $PublishRatePerSecond
    ImageSizeBytes = $ImageSizeBytes
    AlgorithmDelayMs = $AlgorithmDelayMs
    MasterWritePublishThreads = $MasterWritePublishThreads
    MasterResultThreads = $MasterResultThreads
    WorkerTaskThreads = $WorkerTaskThreads
    HttpListenAddress = $HttpListenAddress
    HttpPort = $HttpPort
    HttpRoute = $HttpRoute
    ImageStoreCapacityBytes = $ImageStoreCapacityBytes
    WorkerReadyTimeoutSeconds = $WorkerReadyTimeoutSeconds
    TimeoutMs = $TimeoutMs
}
if ($Restart) {
    $baseRunnerArgs.Restart = $true
}

$useSweep = $Sweep.IsPresent
if (-not $useSweep -and $httpConfig.ContainsKey('SweepEnabled')) {
    $useSweep = [bool]$httpConfig.SweepEnabled
}

$profiles = @()
if ($useSweep -and $httpConfig.ContainsKey('SweepProfiles')) {
    $profiles = @($httpConfig.SweepProfiles | Where-Object {
            Test-TaskFlowProfileEnabled -Profile $_
        })
    if ($profiles.Count -eq 0) {
        throw 'Http.SweepEnabled is true but no enabled Http.SweepProfiles were found'
    }
}
if ($profiles.Count -eq 0) {
    $profiles = @(
        @{
            Name = 'single'
            ServerThreadCount = $HttpServerThreadCount
            ChunkBytes = $HttpChunkBytes
            ReadBufferBytes = $HttpReadBufferBytes
            WriteBufferBytes = $HttpWriteBufferBytes
            SocketReceiveBufferBytes = $HttpSocketReceiveBufferBytes
            SocketSendBufferBytes = $HttpSocketSendBufferBytes
        }
    )
    $useSweep = $false
}

$profileResults = @()
for ($index = 0; $index -lt $profiles.Count; ++$index) {
    $profile = $profiles[$index]
    $profileName = Get-TaskFlowProfileName -Profile $profile -Index $index
    $profileOutputRoot = if ($useSweep) {
        Join-Path (Join-Path $OutputRoot 'profiles') $profileName
    } else {
        $OutputRoot
    }

    $profileArgs = $baseRunnerArgs.Clone()
    $profileArgs.OutputRoot = $profileOutputRoot
    $profileArgs.ProfileName = $profileName
    $profileArgs.HttpServerThreadCount = [int](Get-TaskFlowProfileValue -Profile $profile -Key 'ServerThreadCount' -Fallback $HttpServerThreadCount)
    $profileArgs.HttpChunkBytes = Get-TaskFlowProfileValue -Profile $profile -Key 'ChunkBytes' -Fallback $HttpChunkBytes
    $profileArgs.HttpReadBufferBytes = Get-TaskFlowProfileValue -Profile $profile -Key 'ReadBufferBytes' -Fallback $HttpReadBufferBytes
    $profileArgs.HttpWriteBufferBytes = Get-TaskFlowProfileValue -Profile $profile -Key 'WriteBufferBytes' -Fallback $HttpWriteBufferBytes
    $profileArgs.HttpSocketReceiveBufferBytes = Get-TaskFlowProfileValue -Profile $profile -Key 'SocketReceiveBufferBytes' -Fallback $HttpSocketReceiveBufferBytes
    $profileArgs.HttpSocketSendBufferBytes = Get-TaskFlowProfileValue -Profile $profile -Key 'SocketSendBufferBytes' -Fallback $HttpSocketSendBufferBytes
    if ($useSweep -and $index -lt ($profiles.Count - 1)) {
        $profileArgs.SkipShutdown = $true
    }

    Write-Host "[sweep] running profile $($index + 1)/$($profiles.Count): $profileName"
    & $runner @profileArgs

    $reportDir = Join-Path $profileOutputRoot 'master'
    $summaryPath = Join-Path $reportDir 'master_summary.txt'
    $metricsPath = Join-Path $reportDir 'task_metrics.tsv'
    $summary = Import-TaskFlowKeyValueFile -Path $summaryPath
    $taskRows = @(Import-Csv -LiteralPath $metricsPath -Delimiter "`t")
    $completionMs = Get-TaskFlowMax -Rows $taskRows -Column 'completion_offset_ms'
    $completedCount = [int]$summary['completed_count']
    $imageBytes = [double]$summary['image_size_bytes']
    $seconds = if ($completionMs -gt 0) { $completionMs / 1000.0 } else { 0.0 }
    $imagesPerSecond = if ($seconds -gt 0) { [double]$completedCount / $seconds } else { 0.0 }
    $gbps = if ($seconds -gt 0) { ([double]$completedCount * $imageBytes * 8.0) / ($seconds * 1000000000.0) } else { 0.0 }

    $profileResults += [pscustomobject]@{
        Profile = $profileName
        Completed = $completedCount
        ImagesPerSecond = $imagesPerSecond
        Gbps = $gbps
        ChunkBytes = [long]$summary['http_chunk_bytes']
        ReadBufferBytes = [long]$summary['http_read_buffer_bytes']
        WriteBufferBytes = [long]$summary['http_write_buffer_bytes']
        SocketReceiveBufferBytes = [long]$summary['http_socket_receive_buffer_bytes']
        SocketSendBufferBytes = [long]$summary['http_socket_send_buffer_bytes']
        AvgHttpTotalMs = Get-TaskFlowAverage -Rows $taskRows -Column 'http_total_ms'
        AvgHttpFirstByteMs = Get-TaskFlowAverage -Rows $taskRows -Column 'http_first_byte_ms'
        AvgHttpBodyMs = Get-TaskFlowAverage -Rows $taskRows -Column 'http_body_ms'
        AvgHttpCopyMs = Get-TaskFlowAverage -Rows $taskRows -Column 'http_chunk_callback_ms'
        AvgHttpChunks = Get-TaskFlowAverage -Rows $taskRows -Column 'http_chunk_count'
        MaxHttpTotalMs = Get-TaskFlowMax -Rows $taskRows -Column 'http_total_ms'
        AvgWorkerQueueMs = Get-TaskFlowAverage -Rows $taskRows -Column 'worker_queue_ms'
        ReportDir = $reportDir
    }
}

if ($useSweep) {
    New-Item -ItemType Directory -Force -Path $OutputRoot | Out-Null
    $sweepTsv = Join-Path $OutputRoot 'sweep_summary.tsv'
    $sweepHtml = Join-Path $OutputRoot 'sweep_summary.html'
    $profileResults |
        Sort-Object -Property ImagesPerSecond -Descending |
        Export-Csv -LiteralPath $sweepTsv -Delimiter "`t" -NoTypeInformation -Encoding UTF8

    $htmlRows = foreach ($result in ($profileResults | Sort-Object -Property ImagesPerSecond -Descending)) {
        '<tr><td>{0}</td><td>{1}</td><td>{2}</td><td>{3}</td><td>{4}</td><td>{5}</td><td>{6}</td><td>{7}</td><td>{8}</td><td>{9}</td><td>{10}</td><td>{11}</td></tr>' -f `
            [System.Net.WebUtility]::HtmlEncode($result.Profile),
            (Format-TaskFlowNumber $result.ImagesPerSecond),
            (Format-TaskFlowNumber $result.Gbps),
            $result.Completed,
            $result.ChunkBytes,
            $result.ReadBufferBytes,
            $result.SocketReceiveBufferBytes,
            (Format-TaskFlowNumber $result.AvgHttpTotalMs),
            (Format-TaskFlowNumber $result.AvgHttpFirstByteMs),
            (Format-TaskFlowNumber $result.AvgHttpBodyMs),
            (Format-TaskFlowNumber $result.AvgHttpCopyMs),
            (Format-TaskFlowNumber $result.AvgHttpChunks)
    }
    $best = $profileResults | Sort-Object -Property ImagesPerSecond -Descending | Select-Object -First 1
    $html = @"
<!doctype html>
<html lang="zh-CN">
<head>
<meta charset="utf-8">
<title>Task Flow HTTP 参数扫测</title>
<style>
body{font-family:Segoe UI,Microsoft YaHei,Arial,sans-serif;margin:28px;color:#172033;background:#f7f8fb}
h1{font-size:26px;margin:0 0 12px}
p{line-height:1.6}
table{border-collapse:collapse;width:100%;background:#fff;border:1px solid #d9deea}
th,td{padding:9px 10px;border-bottom:1px solid #e5e8f0;text-align:right;white-space:nowrap}
th:first-child,td:first-child{text-align:left}
th{background:#eef2f7;color:#28364d}
.best{background:#fff;border:1px solid #d9deea;padding:14px 16px;margin:16px 0}
.path{font-family:Consolas,monospace}
</style>
</head>
<body>
<h1>Task Flow HTTP 参数扫测</h1>
<p>这张表把同一套调度面、同一批 20MiB 图片，在不同 HTTP 数据面参数下连续跑出来。看第一列“每秒多少张”和“等第一块/收正文/拷内存”，就能判断到底是连接建立、服务端出块、网络接收，还是 worker 写入内存拖慢。</p>
<div class="best">当前最快：<b>$([System.Net.WebUtility]::HtmlEncode($best.Profile))</b>，<b>$(Format-TaskFlowNumber $best.ImagesPerSecond)</b> 张/秒，约 <b>$(Format-TaskFlowNumber $best.Gbps)</b> Gbps。</div>
<table>
<thead><tr><th>参数组</th><th>每秒多少张</th><th>等效带宽 Gbps</th><th>完成张数</th><th>主机出块大小</th><th>worker 读缓冲</th><th>socket 接收缓冲</th><th>HTTP 总耗时 ms</th><th>等第一块 ms</th><th>收正文 ms</th><th>拷内存 ms</th><th>平均块数</th></tr></thead>
<tbody>
$($htmlRows -join "`n")
</tbody>
</table>
<p class="path">明细目录：$([System.Net.WebUtility]::HtmlEncode($OutputRoot))\profiles\*</p>
</body>
</html>
"@
    Set-Content -LiteralPath $sweepHtml -Value $html -Encoding UTF8
    Write-Host "[sweep] summary: $sweepHtml"
}
