param(
    [Parameter(Mandatory = $true)]
    [string]$BuildDir,
    [string]$Configuration = '',
    [string]$RabbitMqApiUrl = '',
    [string]$RabbitMqHost = '',
    [string]$RabbitMqAdminUser = '',
    [string]$RabbitMqAdminPass = '',
    [string]$RabbitMqVHost = '',
    [string]$RabbitMqMasterUser = '',
    [string]$RabbitMqMasterPass = '',
    [string]$RabbitMqWorkerUser = '',
    [string]$RabbitMqWorkerPass = '',
    [int]$ExpectedWorkerCount = 5,
    [int]$DurationSeconds = 2,
    [int]$PublishRatePerSecond = 50,
    [long]$ImageSizeBytes = 20971520,
    [int]$AlgorithmDelayMs = 10,
    [string]$MasterWritePublishThreads = 'Auto',
    [string]$MasterResultThreads = 'Auto',
    [int]$WorkerTaskThreads = 64,
    [string]$HttpListenAddress = '0.0.0.0',
    [int]$HttpPort = 50080,
    [string]$HttpRoute = '/task-flow/images',
    [int]$HttpServerThreadCount = 64,
    [long]$HttpChunkBytes = 8388608,
    [long]$HttpReadBufferBytes = 0,
    [long]$HttpWriteBufferBytes = 0,
    [long]$HttpSocketReceiveBufferBytes = 0,
    [long]$HttpSocketSendBufferBytes = 0,
    [string]$ProfileName = 'single',
    [switch]$SkipShutdown,
    [long]$ImageStoreCapacityBytes = 4294967296,
    [string]$OutputRoot = '',
    [int]$WorkerReadyTimeoutSeconds = 300,
    [int]$TimeoutMs = 0,
    [switch]$Restart
)

$ErrorActionPreference = 'Stop'
trap {
    Write-Error $_
    exit 1
}

function Get-SettingValue {
    param(
        [string]$ExplicitValue,
        [string]$EnvName,
        [string]$FallbackValue
    )

    if (-not [string]::IsNullOrWhiteSpace($ExplicitValue)) {
        return $ExplicitValue
    }

    $envValue = [Environment]::GetEnvironmentVariable($EnvName)
    if (-not [string]::IsNullOrWhiteSpace($envValue)) {
        return $envValue
    }

    return $FallbackValue
}

function Encode-Segment {
    param([string]$Value)
    return [System.Uri]::EscapeDataString($Value)
}

function Get-BasicAuthHeader {
    param(
        [string]$UserName,
        [string]$Password
    )

    $bytes = [System.Text.Encoding]::ASCII.GetBytes("$UserName`:$Password")
    return 'Basic {0}' -f [Convert]::ToBase64String($bytes)
}

function Invoke-RabbitMqRequest {
    param(
        [string]$Method,
        [string]$Path,
        [object]$Body
    )

    $headers = @{ Authorization = $script:RabbitMqAuthHeader }
    $uri = '{0}{1}' -f $script:RabbitMqApiUrl.TrimEnd('/'), $Path
    if ($null -ne $Body) {
        $json = $Body | ConvertTo-Json -Compress -Depth 10
        Invoke-RestMethod -Method $Method -Uri $uri -Headers $headers -ContentType 'application/json' -Body $json | Out-Null
        return
    }

    Invoke-RestMethod -Method $Method -Uri $uri -Headers $headers | Out-Null
}

function Wait-RabbitMqApiReady {
    $headers = @{ Authorization = $script:RabbitMqAuthHeader }
    $overviewUri = '{0}/overview' -f $script:RabbitMqApiUrl.TrimEnd('/')

    for ($attempt = 1; $attempt -le 60; $attempt += 1) {
        try {
            Invoke-RestMethod -Method Get -Uri $overviewUri -Headers $headers | Out-Null
            return
        } catch {
            if ($attempt -eq 60) {
                throw "RabbitMQ Management API is not reachable at $overviewUri"
            }
            Start-Sleep -Seconds 1
        }
    }
}

function Get-RabbitMqQueueInfo {
    param(
        [string]$VHost,
        [string]$QueueName
    )

    $headers = @{ Authorization = $script:RabbitMqAuthHeader }
    $uri = '{0}/queues/{1}/{2}' -f $script:RabbitMqApiUrl.TrimEnd('/'),
        (Encode-Segment -Value $VHost),
        (Encode-Segment -Value $QueueName)
    return Invoke-RestMethod -Method Get -Uri $uri -Headers $headers
}

function Get-RabbitMqConnectionCount {
    $headers = @{ Authorization = $script:RabbitMqAuthHeader }
    $uri = '{0}/connections' -f $script:RabbitMqApiUrl.TrimEnd('/')
    try {
        $connections = @(Invoke-RestMethod -Method Get -Uri $uri -Headers $headers)
        return $connections.Count
    } catch {
        return -1
    }
}

function Get-QueueMessageCount {
    param([object]$QueueInfo)

    $ready = 0
    $unacked = 0
    if ($null -ne $QueueInfo -and $null -ne $QueueInfo.messages_ready) {
        $ready = [int]$QueueInfo.messages_ready
    }
    if ($null -ne $QueueInfo -and $null -ne $QueueInfo.messages_unacknowledged) {
        $unacked = [int]$QueueInfo.messages_unacknowledged
    }
    return ($ready + $unacked)
}

function Wait-QueueEmpty {
    param(
        [string]$VHost,
        [string]$QueueName,
        [int]$TimeoutSeconds = 30
    )

    $deadline = (Get-Date).AddSeconds($TimeoutSeconds)
    $lastInfo = $null
    do {
        $lastInfo = Get-RabbitMqQueueInfo -VHost $VHost -QueueName $QueueName
        if ((Get-QueueMessageCount -QueueInfo $lastInfo) -eq 0) {
            return $lastInfo
        }
        Start-Sleep -Milliseconds 500
    } while ((Get-Date) -lt $deadline)

    return $lastInfo
}

function Wait-QueueConsumerCount {
    param(
        [string]$VHost,
        [string]$QueueName,
        [int]$ExpectedConsumerCount,
        [int]$TimeoutSeconds
    )

    if ($ExpectedConsumerCount -le 0) {
        throw 'ExpectedConsumerCount must be a positive integer'
    }
    if ($TimeoutSeconds -lt 0) {
        throw 'WorkerReadyTimeoutSeconds must be zero or a positive integer'
    }

    $deadline = if ($TimeoutSeconds -eq 0) { $null } else { (Get-Date).AddSeconds($TimeoutSeconds) }
    $lastReportedCount = -1
    $lastReportAt = [datetime]::MinValue
    while ($true) {
        $queueInfo = Get-RabbitMqQueueInfo -VHost $VHost -QueueName $QueueName
        $consumerCount = 0
        if ($null -ne $queueInfo -and $null -ne $queueInfo.consumers) {
            $consumerCount = [int]$queueInfo.consumers
        }

        if ($consumerCount -ge $ExpectedConsumerCount) {
            Write-Host "[run] consumer ready: $consumerCount/$ExpectedConsumerCount on $QueueName"
            return
        }

        $now = Get-Date
        if ($consumerCount -ne $lastReportedCount -or
            ($now - $lastReportAt).TotalSeconds -ge 5) {
            $timeoutText = if ($TimeoutSeconds -eq 0) { 'no timeout' } else { "timeout ${TimeoutSeconds}s" }
            $connectionCount = Get-RabbitMqConnectionCount
            $connectionText = if ($connectionCount -ge 0) {
                "connections=$connectionCount"
            } else {
                'connections=unknown'
            }
            Write-Host "[run] waiting consumers on $QueueName`: $consumerCount/$ExpectedConsumerCount, $connectionText ($timeoutText)"
            $lastReportedCount = $consumerCount
            $lastReportAt = $now
        }

        if ($null -ne $deadline -and (Get-Date) -ge $deadline) {
            $connectionCount = Get-RabbitMqConnectionCount
            throw "Timed out waiting for $ExpectedConsumerCount consumers on queue $QueueName; current=$consumerCount; connections=$connectionCount. Ensure every worker is running the matching package and can connect to RabbitMQ AMQP."
        }
        Start-Sleep -Seconds 1
    }
}

function Resolve-ExampleArtifact {
    param([string]$Name)

    $candidates = @(
        (Join-Path (Join-Path $script:ExamplesDir $script:Configuration) "$Name.exe"),
        (Join-Path (Join-Path $script:ExamplesDir $script:Configuration) "$Name.dll"),
        (Join-Path $script:ExamplesDir "$Name.exe"),
        (Join-Path $script:ExamplesDir "$Name.dll")
    )

    foreach ($candidate in $candidates) {
        if (Test-Path -LiteralPath $candidate) {
            return (Resolve-Path -LiteralPath $candidate).Path
        }
    }

    throw "Artifact not found for $Name. Checked: $($candidates -join ', ')"
}

function Parse-KeyValueFile {
    param([string]$Path)

    $values = @{}
    foreach ($line in [regex]::Split((Get-Content -LiteralPath $Path -Raw), "\r?\n")) {
        if ($line -match '^(?<key>[^=]+)=(?<value>.*)$') {
            $values[$matches['key']] = $matches['value']
        }
    }
    return $values
}

function ConvertTo-TaskFlowDouble {
    param([object]$Value)

    $number = 0.0
    if ([double]::TryParse(
            [string]$Value,
            [System.Globalization.NumberStyles]::Float,
            [System.Globalization.CultureInfo]::InvariantCulture,
            [ref]$number)) {
        return $number
    }
    return 0.0
}

function Get-TaskFlowPercentile {
    param(
        [double[]]$Values,
        [double]$Percentile
    )

    if ($null -eq $Values -or $Values.Count -eq 0) {
        return 0.0
    }

    $sorted = @($Values | Sort-Object)
    $rank = [int][Math]::Ceiling(($Percentile / 100.0) * $sorted.Count) - 1
    if ($rank -lt 0) {
        $rank = 0
    }
    if ($rank -ge $sorted.Count) {
        $rank = $sorted.Count - 1
    }
    return [double]$sorted[$rank]
}

function Format-TaskFlowNumber {
    param(
        [double]$Value,
        [int]$Digits = 3
    )

    return $Value.ToString("F$Digits", [System.Globalization.CultureInfo]::InvariantCulture)
}

function New-TaskFlowStageStats {
    param(
        [object[]]$Rows,
        [string]$Name,
        [string]$Column
    )

    if ($Rows.Count -eq 0 -or
        -not ($Rows[0].PSObject.Properties.Name -contains $Column)) {
        return $null
    }

    $values = @($Rows | ForEach-Object { ConvertTo-TaskFlowDouble $_.$Column })
    $measure = $values | Measure-Object -Average -Maximum
    return [pscustomobject]@{
        Name = $Name
        Avg = [double]$measure.Average
        P95 = Get-TaskFlowPercentile -Values $values -Percentile 95
        Max = [double]$measure.Maximum
    }
}

function Get-TaskFlowAverage {
    param(
        [object[]]$Rows,
        [string]$Column
    )

    if ($Rows.Count -eq 0 -or
        -not ($Rows[0].PSObject.Properties.Name -contains $Column)) {
        return 0.0
    }

    $measure = $Rows |
        ForEach-Object { ConvertTo-TaskFlowDouble $_.$Column } |
        Measure-Object -Average
    if ($null -eq $measure.Average) {
        return 0.0
    }
    return [double]$measure.Average
}

function ConvertTo-TaskFlowHtml {
    param([object]$Value)
    return [System.Net.WebUtility]::HtmlEncode([string]$Value)
}

function New-TaskFlowHeatCell {
    param(
        [double]$Value,
        [double]$MaxValue,
        [string]$Text,
        [string]$Title = ''
    )

    $ratio = 0.0
    if ($MaxValue -gt 0.0) {
        $ratio = [Math]::Max(0.0, [Math]::Min(1.0, $Value / $MaxValue))
    }
    $alpha = 0.08 + (0.82 * $ratio)
    $alphaText = $alpha.ToString('F3', [System.Globalization.CultureInfo]::InvariantCulture)
    return '<td class="heat-cell" style="background-color: rgba(220, 38, 38, {0});" title="{1}">{2}</td>' -f
        $alphaText,
        (ConvertTo-TaskFlowHtml $Title),
        (ConvertTo-TaskFlowHtml $Text)
}

function Write-TaskMetricsHtml {
    param(
        [string]$Path,
        [string]$RunId,
        [hashtable]$Summary,
        [object[]]$Rows
    )

    $processedRows = @($Rows | Where-Object { $_.status -eq 'processed' })
    $totalRows = $Rows.Count
    $processedCount = $processedRows.Count
    $completionMaxMs = 0.0
    if ($Rows.Count -gt 0) {
        $completionMaxMs = [double](($Rows | ForEach-Object {
                    ConvertTo-TaskFlowDouble $_.completion_offset_ms
                }) | Measure-Object -Maximum).Maximum
    }
    $durationSeconds = [Math]::Max($completionMaxMs / 1000.0, 0.001)
    $throughput = $processedCount / $durationSeconds
    $imageBytes = if ($Summary.ContainsKey('image_size_bytes')) {
        [double]$Summary['image_size_bytes']
    } else {
        0.0
    }
    $gbps = (($imageBytes * $processedCount) / $durationSeconds) / 1073741824.0

    $stageDefs = @(
        @('主机准备图片', 'source_stage_ms'),
        @('任务进线程池等待', 'task_queue_ms'),
        @('主机把图片放进内存仓库', 'image_store_put_ms'),
        @('主机发出任务消息', 'publish_ms'),
        @('从机收到任务后等处理槽', 'worker_queue_ms'),
        @('从机通过 HTTP 拉图', 'image_fetch_ms'),
        @('从机算法处理', 'algorithm_ms'),
        @('从机发回结果消息', 'worker_publish_ms'),
        @('主机删除内存图片', 'image_store_delete_ms'),
        @('结果消息在 RabbitMQ 等待', 'result_queue_ms'),
        @('主机处理结果', 'result_handle_ms'),
        @('未拆出来的其他时间', 'residual_ms'),
        @('从发图到删图完成', 'master_end_to_end_ms')
    )
    $httpStageDefs = @(
        @('从机准备 HTTP 请求', 'http_setup_ms'),
        @('等待主机响应头', 'http_header_wait_ms'),
        @('等第一块数据到达', 'http_first_byte_ms'),
        @('持续接收图片正文', 'http_body_ms'),
        @('把 HTTP 数据拷进图片内存', 'http_chunk_callback_ms'),
        @('HTTP 拉图总耗时', 'http_total_ms')
    )
    $stageRows = @()
    $stageStatsObjects = @()
    foreach ($stage in $stageDefs) {
        $stats = New-TaskFlowStageStats -Rows $Rows -Name $stage[0] -Column $stage[1]
        if ($null -ne $stats) {
            $stageStatsObjects += $stats
            $stageRows += '<tr><td>{0}</td><td>{1}</td><td>{2}</td><td>{3}</td></tr>' -f
                (ConvertTo-TaskFlowHtml $stats.Name),
                (Format-TaskFlowNumber $stats.Avg),
                (Format-TaskFlowNumber $stats.P95),
                (Format-TaskFlowNumber $stats.Max)
        }
    }
    $httpStageRows = @()
    foreach ($stage in $httpStageDefs) {
        $stats = New-TaskFlowStageStats -Rows $processedRows -Name $stage[0] -Column $stage[1]
        if ($null -ne $stats) {
            $httpStageRows += '<tr><td>{0}</td><td>{1}</td><td>{2}</td><td>{3}</td></tr>' -f
                (ConvertTo-TaskFlowHtml $stats.Name),
                (Format-TaskFlowNumber $stats.Avg),
                (Format-TaskFlowNumber $stats.P95),
                (Format-TaskFlowNumber $stats.Max)
        }
    }
    $dominantStage = @($stageStatsObjects |
        Where-Object { $_.Name -ne '从发图到删图完成' -and $_.Name -ne '未拆出来的其他时间' } |
        Sort-Object Avg -Descending |
        Select-Object -First 1)
    $dominantStageText = if ($dominantStage.Count -gt 0) {
        '{0}，平均 {1} ms' -f $dominantStage[0].Name, (Format-TaskFlowNumber $dominantStage[0].Avg)
    } else {
        '没有可用数据'
    }

    $flowSteps = @(
        '主机：复用预生成的 20MiB 图片，把图片登记到内存仓库，只把 image_id/token/大小放进任务消息。',
        'RabbitMQ：只负责调度任务消息；任务消息被某个从机处理槽消费后，才算进入那台从机。',
        '从机：收到任务后，通过 HTTP 向主机按 image_id/token 拉取图片正文，再做算法处理。',
        '从机：处理完后把结果消息发回 RabbitMQ；当前 E2E 不等待 publisher confirm，所以调度面不会被确认等待卡住。',
        '主机：消费结果消息，记录指标，删除内存仓库里的图片，整张图的 E2E 到这里结束。',
        '控制队列：只广播停止信号，不承载图片和任务，避免多个任务消费者抢同一条停止消息导致任务队列残留。'
    )
    $flowItems = @($flowSteps | ForEach-Object {
            '<li>{0}</li>' -f (ConvertTo-TaskFlowHtml $_)
        })

    $readingTips = @(
        '先看“平均最慢阶段”：它告诉你本轮最该盯哪一层。',
        '如果“从机通过 HTTP 拉图”最大，调度面基本已经让路，瓶颈在数据面。',
        'HTTP 里面如果“等待主机响应头/第一块数据”高，优先怀疑连接、排队、主机 HTTP 线程或请求调度。',
        'HTTP 里面如果“持续接收图片正文”高，优先怀疑网络吞吐、TCP 栈、HTTP 实现和发送端写出效率。',
        'HTTP 里面如果“拷进图片内存”高，优先怀疑从机侧内存带宽和多线程内存拷贝压力。'
    )
    $readingTipItems = @($readingTips | ForEach-Object {
            '<li>{0}</li>' -f (ConvertTo-TaskFlowHtml $_)
        })

    $nodeRows = @()
    foreach ($group in @($processedRows | Group-Object worker_id | Sort-Object Name)) {
        $nodeTaskRows = @($group.Group)
        $nodeCompletionMax = [double](($nodeTaskRows | ForEach-Object {
                    ConvertTo-TaskFlowDouble $_.completion_offset_ms
                }) | Measure-Object -Maximum).Maximum
        $nodeSourceMin = [double](($nodeTaskRows | ForEach-Object {
                    ConvertTo-TaskFlowDouble $_.source_offset_ms
                }) | Measure-Object -Minimum).Minimum
        $nodeSeconds = [Math]::Max(($nodeCompletionMax - $nodeSourceMin) / 1000.0, 0.001)
        $nodeFetchAvg = [double](($nodeTaskRows | ForEach-Object {
                    ConvertTo-TaskFlowDouble $_.image_fetch_ms
                }) | Measure-Object -Average).Average
        $nodeFirstByteAvg = Get-TaskFlowAverage -Rows $nodeTaskRows -Column 'http_first_byte_ms'
        $nodeBodyAvg = Get-TaskFlowAverage -Rows $nodeTaskRows -Column 'http_body_ms'
        $nodeCopyAvg = Get-TaskFlowAverage -Rows $nodeTaskRows -Column 'http_chunk_callback_ms'
        $nodeE2EAvg = [double](($nodeTaskRows | ForEach-Object {
                    ConvertTo-TaskFlowDouble $_.master_end_to_end_ms
                }) | Measure-Object -Average).Average
        $nodeRows += '<tr><td>{0}</td><td>{1}</td><td>{2}</td><td>{3}</td><td>{4}</td><td>{5}</td><td>{6}</td><td>{7}</td></tr>' -f
            (ConvertTo-TaskFlowHtml $group.Name),
            $nodeTaskRows.Count,
            (Format-TaskFlowNumber ($nodeTaskRows.Count / $nodeSeconds)),
            (Format-TaskFlowNumber $nodeFetchAvg),
            (Format-TaskFlowNumber $nodeFirstByteAvg),
            (Format-TaskFlowNumber $nodeBodyAvg),
            (Format-TaskFlowNumber $nodeCopyAvg),
            (Format-TaskFlowNumber $nodeE2EAvg)
    }

    $heatStageDefs = @(
        @('准备图', 'source_stage_ms'),
        @('存内存', 'image_store_put_ms'),
        @('发任务', 'publish_ms'),
        @('从机等槽', 'worker_queue_ms'),
        @('HTTP 拉图', 'image_fetch_ms'),
        @('算法', 'algorithm_ms'),
        @('发结果', 'worker_publish_ms'),
        @('删图片', 'image_store_delete_ms'),
        @('结果等待', 'result_queue_ms'),
        @('主机收尾', 'result_handle_ms')
    )
    $workerGroups = @($processedRows | Group-Object worker_id | Sort-Object Name)
    $stageHeatValues = @()
    foreach ($group in $workerGroups) {
        $workerRows = @($group.Group)
        foreach ($stage in $heatStageDefs) {
            $stageHeatValues += [pscustomobject]@{
                Worker = [string]$group.Name
                Stage = [string]$stage[0]
                Column = [string]$stage[1]
                Value = Get-TaskFlowAverage -Rows $workerRows -Column $stage[1]
            }
        }
    }
    $stageHeatMax = 0.0
    if ($stageHeatValues.Count -gt 0) {
        $stageHeatMax = [double]($stageHeatValues | Measure-Object Value -Maximum).Maximum
    }
    $stageHeatHeader = '<tr><th>从机</th>' +
        (($heatStageDefs | ForEach-Object { '<th>{0}</th>' -f (ConvertTo-TaskFlowHtml $_[0]) }) -join '') +
        '</tr>'
    $stageHeatRows = @()
    foreach ($group in $workerGroups) {
        $cells = @()
        foreach ($stage in $heatStageDefs) {
            $entry = @($stageHeatValues | Where-Object {
                    $_.Worker -eq [string]$group.Name -and $_.Column -eq [string]$stage[1]
                } | Select-Object -First 1)
            $value = if ($entry.Count -gt 0) { [double]$entry[0].Value } else { 0.0 }
            $cells += New-TaskFlowHeatCell `
                -Value $value `
                -MaxValue $stageHeatMax `
                -Text (Format-TaskFlowNumber $value 1) `
                -Title ('{0}，{1}，平均 {2} ms' -f $group.Name, $stage[0], (Format-TaskFlowNumber $value))
        }
        $stageHeatRows += '<tr><td>{0}</td>{1}</tr>' -f
            (ConvertTo-TaskFlowHtml $group.Name),
            ($cells -join '')
    }

    $httpHeatStageDefs = @(
        @('准备请求', 'http_setup_ms'),
        @('等响应头', 'http_header_wait_ms'),
        @('等第一块', 'http_first_byte_ms'),
        @('收图片正文', 'http_body_ms'),
        @('拷进内存', 'http_chunk_callback_ms'),
        @('HTTP 总耗时', 'http_total_ms')
    )
    $httpHeatValues = @()
    foreach ($group in $workerGroups) {
        $workerRows = @($group.Group)
        foreach ($stage in $httpHeatStageDefs) {
            $httpHeatValues += [pscustomobject]@{
                Worker = [string]$group.Name
                Stage = [string]$stage[0]
                Column = [string]$stage[1]
                Value = Get-TaskFlowAverage -Rows $workerRows -Column $stage[1]
            }
        }
    }
    $httpHeatMax = 0.0
    if ($httpHeatValues.Count -gt 0) {
        $httpHeatMax = [double]($httpHeatValues | Measure-Object Value -Maximum).Maximum
    }
    $httpHeatHeader = '<tr><th>从机</th>' +
        (($httpHeatStageDefs | ForEach-Object { '<th>{0}</th>' -f (ConvertTo-TaskFlowHtml $_[0]) }) -join '') +
        '</tr>'
    $httpHeatRows = @()
    foreach ($group in $workerGroups) {
        $cells = @()
        foreach ($stage in $httpHeatStageDefs) {
            $entry = @($httpHeatValues | Where-Object {
                    $_.Worker -eq [string]$group.Name -and $_.Column -eq [string]$stage[1]
                } | Select-Object -First 1)
            $value = if ($entry.Count -gt 0) { [double]$entry[0].Value } else { 0.0 }
            $cells += New-TaskFlowHeatCell `
                -Value $value `
                -MaxValue $httpHeatMax `
                -Text (Format-TaskFlowNumber $value 1) `
                -Title ('{0}，{1}，平均 {2} ms' -f $group.Name, $stage[0], (Format-TaskFlowNumber $value))
        }
        $httpHeatRows += '<tr><td>{0}</td>{1}</tr>' -f
            (ConvertTo-TaskFlowHtml $group.Name),
            ($cells -join '')
    }

    $bucketCount = if ($processedCount -gt 0) {
        [Math]::Min(20, [Math]::Max(1, $processedCount))
    } else {
        1
    }
    $bucketWidthMs = [Math]::Max($completionMaxMs / [double]$bucketCount, 1.0)
    $fetchBuckets = @()
    foreach ($group in $workerGroups) {
        $workerRows = @($group.Group)
        for ($bucket = 0; $bucket -lt $bucketCount; ++$bucket) {
            $startMs = $bucketWidthMs * $bucket
            $endMs = if ($bucket -eq ($bucketCount - 1)) {
                $completionMaxMs + 0.001
            } else {
                $bucketWidthMs * ($bucket + 1)
            }
            $bucketRows = @($workerRows | Where-Object {
                    $offset = ConvertTo-TaskFlowDouble $_.completion_offset_ms
                    $offset -ge $startMs -and $offset -lt $endMs
                })
            $avgFetch = Get-TaskFlowAverage -Rows $bucketRows -Column 'image_fetch_ms'
            $fetchBuckets += [pscustomobject]@{
                Worker = [string]$group.Name
                Bucket = $bucket
                StartMs = $startMs
                EndMs = $endMs
                Count = $bucketRows.Count
                AvgFetch = $avgFetch
            }
        }
    }
    $fetchBucketMax = 0.0
    if ($fetchBuckets.Count -gt 0) {
        $fetchBucketMax = [double]($fetchBuckets | Measure-Object AvgFetch -Maximum).Maximum
    }
    $fetchHeatHeaderCells = @()
    for ($bucket = 0; $bucket -lt $bucketCount; ++$bucket) {
        $fetchHeatHeaderCells += '<th>{0}-{1}s</th>' -f
            (Format-TaskFlowNumber (($bucketWidthMs * $bucket) / 1000.0) 1),
            (Format-TaskFlowNumber (($bucketWidthMs * ($bucket + 1)) / 1000.0) 1)
    }
    $fetchHeatHeader = '<tr><th>从机</th>{0}</tr>' -f ($fetchHeatHeaderCells -join '')
    $fetchHeatRows = @()
    foreach ($group in $workerGroups) {
        $cells = @()
        for ($bucket = 0; $bucket -lt $bucketCount; ++$bucket) {
            $entry = @($fetchBuckets | Where-Object {
                    $_.Worker -eq [string]$group.Name -and $_.Bucket -eq $bucket
                } | Select-Object -First 1)
            if ($entry.Count -eq 0 -or $entry[0].Count -eq 0) {
                $cells += '<td class="heat-empty">-</td>'
                continue
            }
            $title = '{0}，任务数={1}，平均拉图={2} ms，完成时间窗={3}-{4} ms' -f
                $group.Name,
                $entry[0].Count,
                (Format-TaskFlowNumber $entry[0].AvgFetch),
                (Format-TaskFlowNumber $entry[0].StartMs 1),
                (Format-TaskFlowNumber $entry[0].EndMs 1)
            $cells += New-TaskFlowHeatCell `
                -Value ([double]$entry[0].AvgFetch) `
                -MaxValue $fetchBucketMax `
                -Text ('{0} ({1})' -f (Format-TaskFlowNumber $entry[0].AvgFetch 1), $entry[0].Count) `
                -Title $title
        }
        $fetchHeatRows += '<tr><td>{0}</td>{1}</tr>' -f
            (ConvertTo-TaskFlowHtml $group.Name),
            ($cells -join '')
    }

    $slowRows = @($Rows |
        Sort-Object { ConvertTo-TaskFlowDouble $_.master_end_to_end_ms } -Descending |
        Select-Object -First 10)
    $slowTaskRows = @()
    foreach ($row in $slowRows) {
        $slowTaskRows += '<tr><td>{0}</td><td>{1}</td><td>{2}</td><td>{3}</td><td>{4}</td><td>{5}</td><td>{6}</td><td>{7}</td></tr>' -f
            (ConvertTo-TaskFlowHtml $row.task_id),
            (ConvertTo-TaskFlowHtml $row.worker_id),
            (ConvertTo-TaskFlowHtml $row.status),
            (Format-TaskFlowNumber (ConvertTo-TaskFlowDouble $row.master_end_to_end_ms)),
            (Format-TaskFlowNumber (ConvertTo-TaskFlowDouble $row.image_fetch_ms)),
            (Format-TaskFlowNumber (ConvertTo-TaskFlowDouble $row.http_first_byte_ms)),
            (Format-TaskFlowNumber (ConvertTo-TaskFlowDouble $row.http_body_ms)),
            (Format-TaskFlowNumber (ConvertTo-TaskFlowDouble $row.http_chunk_callback_ms))
    }

    $taskHeatStageDefs = @(
        @('从机等槽', 'worker_queue_ms'),
        @('拉图总耗时', 'image_fetch_ms'),
        @('等第一块', 'http_first_byte_ms'),
        @('收图片正文', 'http_body_ms'),
        @('拷进内存', 'http_chunk_callback_ms'),
        @('HTTP 总耗时', 'http_total_ms'),
        @('算法', 'algorithm_ms'),
        @('发结果', 'worker_publish_ms'),
        @('结果等待', 'result_queue_ms'),
        @('主机收尾', 'result_handle_ms'),
        @('全链路', 'master_end_to_end_ms')
    )
    $slowHeatRows = @($Rows |
        Sort-Object { ConvertTo-TaskFlowDouble $_.master_end_to_end_ms } -Descending |
        Select-Object -First 40)
    $taskHeatMax = 0.0
    foreach ($row in $slowHeatRows) {
        foreach ($stage in $taskHeatStageDefs) {
            $taskHeatMax = [Math]::Max($taskHeatMax, (ConvertTo-TaskFlowDouble $row.($stage[1])))
        }
    }
    $taskHeatHeader = '<tr><th>任务</th><th>从机</th>' +
        (($taskHeatStageDefs | ForEach-Object { '<th>{0}</th>' -f (ConvertTo-TaskFlowHtml $_[0]) }) -join '') +
        '</tr>'
    $taskHeatRows = @()
    foreach ($row in $slowHeatRows) {
        $cells = @()
        foreach ($stage in $taskHeatStageDefs) {
            $value = ConvertTo-TaskFlowDouble $row.($stage[1])
            $cells += New-TaskFlowHeatCell `
                -Value $value `
                -MaxValue $taskHeatMax `
                -Text (Format-TaskFlowNumber $value 1) `
                -Title ('{0}：{1} ms' -f $stage[0], (Format-TaskFlowNumber $value))
        }
        $taskHeatRows += '<tr><td>{0}</td><td>{1}</td>{2}</tr>' -f
            (ConvertTo-TaskFlowHtml $row.task_id),
            (ConvertTo-TaskFlowHtml $row.worker_id),
            ($cells -join '')
    }

    $html = @"
<!doctype html>
<html lang="zh-CN">
<head>
<meta charset="utf-8">
<title>Task Flow E2E 报表 - $(ConvertTo-TaskFlowHtml $RunId)</title>
<style>
body { font-family: Segoe UI, Microsoft YaHei, Arial, sans-serif; margin: 24px; color: #1f2933; background: #f7f9fb; }
h1 { margin: 0 0 16px; font-size: 24px; }
h2 { margin: 28px 0 10px; font-size: 18px; }
p { margin: 6px 0; line-height: 1.55; }
ul { margin: 8px 0 0 20px; padding: 0; line-height: 1.6; }
.cards { display: grid; grid-template-columns: repeat(auto-fit, minmax(180px, 1fr)); gap: 12px; }
.card { background: #fff; border: 1px solid #d9e2ec; border-radius: 6px; padding: 14px; }
.label { color: #52606d; font-size: 12px; }
.value { font-size: 24px; font-weight: 650; margin-top: 4px; }
.value.small { font-size: 16px; line-height: 1.25; }
.story { background: #fff; border: 1px solid #d9e2ec; border-radius: 6px; padding: 14px 16px; margin: 14px 0; }
.story-title { font-weight: 650; margin-bottom: 4px; color: #243b53; }
.section-note { color: #52606d; margin: 4px 0 10px; }
table { width: 100%; border-collapse: collapse; background: #fff; border: 1px solid #d9e2ec; }
th, td { padding: 8px 10px; border-bottom: 1px solid #e4e7eb; text-align: right; }
th:first-child, td:first-child { text-align: left; }
th { background: #eef2f7; color: #334e68; font-weight: 650; }
tr:last-child td { border-bottom: 0; }
.heat-table { table-layout: fixed; font-size: 12px; }
.heat-table th, .heat-table td { padding: 6px 7px; text-align: center; }
.heat-table th:first-child, .heat-table td:first-child { text-align: left; width: 130px; }
.heat-cell { color: #111827; font-variant-numeric: tabular-nums; }
.heat-empty { color: #9aa5b1; background: #f8fafc; }
.task-heat th:first-child, .task-heat td:first-child { width: 260px; }
</style>
</head>
<body>
<h1>Task Flow E2E 报表 - $(ConvertTo-TaskFlowHtml $RunId)</h1>
<div class="cards">
<div class="card"><div class="label">已处理图片</div><div class="value">$processedCount / $totalRows</div></div>
<div class="card"><div class="label">总吞吐，张/秒</div><div class="value">$(Format-TaskFlowNumber $throughput)</div></div>
<div class="card"><div class="label">图片数据吞吐，GiB/秒</div><div class="value">$(Format-TaskFlowNumber $gbps)</div></div>
<div class="card"><div class="label">本轮总耗时，秒</div><div class="value">$(Format-TaskFlowNumber $durationSeconds)</div></div>
<div class="card"><div class="label">平均最慢阶段</div><div class="value small">$(ConvertTo-TaskFlowHtml $dominantStageText)</div></div>
</div>
<div class="story">
<div class="story-title">这一轮 E2E 在做什么</div>
<ul>
$($flowItems -join "`n")
</ul>
</div>
<div class="story">
<div class="story-title">读报表时先看哪里</div>
<ul>
$($readingTipItems -join "`n")
</ul>
</div>
<h2>每台从机跑得怎么样</h2>
<p class="section-note">这里看分发是否均匀，以及哪台从机拉图慢。理论上调度面打通后，差异主要会落在 HTTP 拉图这条数据面上。</p>
<table><thead><tr><th>从机</th><th>处理图片数</th><th>每秒多少张</th><th>平均拉图耗时 ms</th><th>平均等第一块 ms</th><th>平均收正文 ms</th><th>平均拷内存 ms</th><th>平均全链路 ms</th></tr></thead><tbody>
$($nodeRows -join "`n")
</tbody></table>
<h2>整条链路各阶段耗时</h2>
<p class="section-note">这些阶段按“主机发任务 -> RabbitMQ 调度 -> 从机拉图处理 -> 主机收结果删图”的顺序拆开。平均值看常态，P95 和最大值看偶发卡顿。</p>
<table><thead><tr><th>阶段</th><th>平均 ms</th><th>P95 ms</th><th>最大 ms</th></tr></thead><tbody>
$($stageRows -join "`n")
</tbody></table>
<h2>HTTP 拉图内部拆解</h2>
<p class="section-note">这一段只看数据面：从机发起 HTTP 请求，到主机回包，再到从机把图片正文拷进自己的内存。</p>
<table><thead><tr><th>HTTP 内部步骤</th><th>平均 ms</th><th>P95 ms</th><th>最大 ms</th></tr></thead><tbody>
$($httpStageRows -join "`n")
</tbody></table>
<h2>从机阶段热力图</h2>
<p class="section-note">红色越深，表示这台从机在这个阶段平均越慢。用它看“是不是某台机器拖后腿”。</p>
<table class="heat-table"><thead>$stageHeatHeader</thead><tbody>
$($stageHeatRows -join "`n")
</tbody></table>
<h2>HTTP 数据面热力图</h2>
<p class="section-note">红色集中在“等第一块/等响应头”偏调度或连接问题；集中在“收图片正文”偏网络吞吐或 HTTP 发送效率；集中在“拷进内存”偏 worker 内存拷贝压力。</p>
<table class="heat-table"><thead>$httpHeatHeader</thead><tbody>
$($httpHeatRows -join "`n")
</tbody></table>
<h2>拉图耗时随时间变化</h2>
<p class="section-note">横向是本轮运行时间窗，纵向是从机。它能看出瓶颈是一直存在，还是某个时间段突然变慢。</p>
<table class="heat-table"><thead>$fetchHeatHeader</thead><tbody>
$($fetchHeatRows -join "`n")
</tbody></table>
<h2>最慢的任务</h2>
<p class="section-note">这里直接列最慢图片，方便回到日志里按 task_id 或 image_id 追单张图。</p>
<table><thead><tr><th>任务</th><th>从机</th><th>状态</th><th>全链路 ms</th><th>拉图 ms</th><th>等第一块 ms</th><th>收正文 ms</th><th>拷内存 ms</th></tr></thead><tbody>
$($slowTaskRows -join "`n")
</tbody></table>
<h2>最慢任务逐阶段热力图</h2>
<p class="section-note">这里看单张慢图到底慢在调度、HTTP、算法，还是主机收尾。</p>
<table class="heat-table task-heat"><thead>$taskHeatHeader</thead><tbody>
$($taskHeatRows -join "`n")
</tbody></table>
</body>
</html>
"@

    [System.IO.File]::WriteAllText($Path, $html, [System.Text.UTF8Encoding]::new($false))
}

function Resolve-ThreadCount {
    param(
        [string]$Value,
        [string]$Name
    )

    if ([string]::IsNullOrWhiteSpace($Value) -or $Value -ieq 'Auto') {
        return [Environment]::ProcessorCount
    }

    $parsed = 0
    if (-not [int]::TryParse($Value, [ref]$parsed) -or $parsed -le 0) {
        throw "$Name must be a positive integer or Auto"
    }

    return $parsed
}

function Resolve-OutputDirectoryPath {
    param(
        [string]$Path,
        [string]$BasePath,
        [string]$Name
    )

    if ([string]::IsNullOrWhiteSpace($Path)) {
        throw "$Name must not be empty."
    }

    if ([System.IO.Path]::IsPathRooted($Path)) {
        return [System.IO.Path]::GetFullPath($Path)
    }

    return [System.IO.Path]::GetFullPath((Join-Path $BasePath $Path))
}

function Reset-TaskFlowRuntimeDirectory {
    param(
        [string]$Path,
        [string]$Label
    )

    try {
        if (Test-Path -LiteralPath $Path -ErrorAction Stop) {
            Remove-Item -LiteralPath $Path -Recurse -Force -ErrorAction Stop
        }
        New-Item -ItemType Directory -Force -Path $Path -ErrorAction Stop | Out-Null
    } catch {
        $message = $_.Exception.Message
        throw ("$Label failed to prepare runtime directory: $Path`n" +
            "Reason: permission denied, locked files from previous run, or broken ACL.`n" +
            "Suggestion: run Stop-TestEnv.ps1 first, then retry from an elevated PowerShell session if needed.`n" +
            "Original error: $message")
    }
}

function Write-JsonFile {
    param(
        [string]$Path,
        [object]$Value
    )

    $directory = Split-Path -Parent $Path
    if (-not [string]::IsNullOrWhiteSpace($directory)) {
        New-Item -ItemType Directory -Force -Path $directory | Out-Null
    }

    $json = $Value | ConvertTo-Json -Depth 12
    [System.IO.File]::WriteAllText($Path, $json, [System.Text.UTF8Encoding]::new($false))
}

function Get-TaskFlowProcessByCommandLine {
    param(
        [string]$ProcessImageName,
        [string[]]$Needles
    )

    try {
        $processes = @(Get-CimInstance Win32_Process -Filter "Name='$ProcessImageName'" -ErrorAction Stop)
    } catch {
        return @()
    }

    $matches = @()
    foreach ($process in $processes) {
        $commandLine = [string]$process.CommandLine
        if ([string]::IsNullOrWhiteSpace($commandLine)) {
            continue
        }

        $matched = $true
        foreach ($needle in $Needles) {
            if ($commandLine.IndexOf($needle, [System.StringComparison]::OrdinalIgnoreCase) -lt 0) {
                $matched = $false
                break
            }
        }

        if ($matched) {
            $matches += [pscustomobject]@{
                ProcessId = [int]$process.ProcessId
                CommandLine = $commandLine
            }
        }
    }
    return $matches
}

function Stop-TaskFlowProcesses {
    param([object[]]$Processes)

    foreach ($process in $Processes) {
        try {
            $processObject = Get-Process -Id $process.ProcessId -ErrorAction SilentlyContinue
            Stop-Process -Id $process.ProcessId -Force -ErrorAction SilentlyContinue
            Wait-Process -Id $process.ProcessId -Timeout 10 -ErrorAction SilentlyContinue
            if ($null -ne $processObject) {
                $processObject.Dispose()
            }
        } catch {
        }
    }
}

function New-ConnectionConfig {
    param([string]$Uri)

    return @{
        uri = $Uri
        heartbeat_seconds = 10
        connect_timeout_ms = 5000
        socket_timeout_ms = 10
        reconnect = @{
            enabled = $true
            initial_delay_ms = 200
            max_delay_ms = 2000
        }
    }
}

function New-MasterModuleConfig {
    param(
        [string]$BusModuleName,
        [string]$RabbitPluginPath,
        [string]$HttpModuleName,
        [string]$HttpPluginPath,
        [string]$Uri,
        [int]$ThreadCount,
        [string]$ListenAddress,
        [int]$ListenPort,
        [int]$HttpServerThreadCount,
        [long]$HttpChunkBytes,
        [long]$HttpReadBufferBytes,
        [long]$HttpWriteBufferBytes,
        [long]$HttpSocketReceiveBufferBytes,
        [long]$HttpSocketSendBufferBytes
    )

    return @{
        schema_version = 2
        modules = @(
            @{
                name = $BusModuleName
                type = 'amqp_bus'
                library_path = $RabbitPluginPath
                config = @{
                    connection = (New-ConnectionConfig -Uri $Uri)
                    worker_pool = @{
                        thread_count = $ThreadCount
                    }
                    topology = @{
                        exchanges = @(
                            @{
                                name = 'mc.task.exchange'
                                type = 'direct'
                                durable = $true
                                passive = $true
                            },
                            @{
                                name = 'mc.control.exchange'
                                type = 'fanout'
                                durable = $true
                                passive = $true
                            }
                        )
                        queues = @(
                            @{
                                name = 'mc.result.queue'
                                durable = $true
                                passive = $true
                            }
                        )
                    }
                    publishers = @(
                        @{
                            name = 'task_producer'
                            exchange = 'mc.task.exchange'
                            routing_key = 'task.ready'
                            persistent = $false
                            content_type = 'text/plain'
                        },
                        @{
                            name = 'control_producer'
                            exchange = 'mc.control.exchange'
                            routing_key = ''
                            persistent = $false
                            content_type = 'text/plain'
                        }
                    )
                    consumers = @(
                        @{
                            name = 'result_consumer'
                            queue = 'mc.result.queue'
                            prefetch_count = 0
                            auto_ack = $false
                        }
                    )
                }
            },
            @{
                name = $HttpModuleName
                type = 'http_transport'
                library_path = $HttpPluginPath
                config = @{
                    role = 'server'
                    listen_address = $ListenAddress
                    port = $ListenPort
                    server_thread_count = $HttpServerThreadCount
                    read_timeout_ms = 30000
                    write_timeout_ms = 30000
                    max_payload_bytes = 1073741824
                    chunk_bytes = $HttpChunkBytes
                    read_buffer_bytes = $HttpReadBufferBytes
                    write_buffer_bytes = $HttpWriteBufferBytes
                    socket_receive_buffer_bytes = $HttpSocketReceiveBufferBytes
                    socket_send_buffer_bytes = $HttpSocketSendBufferBytes
                }
            }
        )
    }
}

$BuildDir = (Resolve-Path -LiteralPath $BuildDir).Path
$Configuration = Get-SettingValue -ExplicitValue $Configuration -EnvName 'Configuration' -FallbackValue 'Debug'
$RabbitMqApiUrl = Get-SettingValue -ExplicitValue $RabbitMqApiUrl -EnvName 'RABBITMQ_API_URL' -FallbackValue 'http://127.0.0.1:15672/api'
$RabbitMqHost = Get-SettingValue -ExplicitValue $RabbitMqHost -EnvName 'RABBITMQ_HOST' -FallbackValue ''
$RabbitMqAdminUser = Get-SettingValue -ExplicitValue $RabbitMqAdminUser -EnvName 'RABBITMQ_ADMIN_USER' -FallbackValue 'guest'
$RabbitMqAdminPass = Get-SettingValue -ExplicitValue $RabbitMqAdminPass -EnvName 'RABBITMQ_ADMIN_PASS' -FallbackValue 'guest'
$RabbitMqVHost = Get-SettingValue -ExplicitValue $RabbitMqVHost -EnvName 'RABBITMQ_VHOST' -FallbackValue 'mc_integration'
$RabbitMqMasterUser = Get-SettingValue -ExplicitValue $RabbitMqMasterUser -EnvName 'RABBITMQ_MASTER_USER' -FallbackValue 'mc_master'
$RabbitMqMasterPass = Get-SettingValue -ExplicitValue $RabbitMqMasterPass -EnvName 'RABBITMQ_MASTER_PASS' -FallbackValue 'master_secret'
$RabbitMqWorkerUser = Get-SettingValue -ExplicitValue $RabbitMqWorkerUser -EnvName 'RABBITMQ_WORKER_USER' -FallbackValue 'mc_worker'
$RabbitMqWorkerPass = Get-SettingValue -ExplicitValue $RabbitMqWorkerPass -EnvName 'RABBITMQ_WORKER_PASS' -FallbackValue 'worker_secret'

$apiUri = [System.Uri]$RabbitMqApiUrl
if ([string]::IsNullOrWhiteSpace($RabbitMqHost)) {
    $RabbitMqHost = if ([string]::IsNullOrWhiteSpace($apiUri.Host)) { '127.0.0.1' } else { $apiUri.Host }
}

if ($ExpectedWorkerCount -le 0 -or $DurationSeconds -le 0 -or $PublishRatePerSecond -le 0 -or
    $ImageSizeBytes -le 0 -or $AlgorithmDelayMs -lt 0 -or $WorkerTaskThreads -le 0 -or
    $WorkerReadyTimeoutSeconds -lt 0 -or $TimeoutMs -lt 0 -or $HttpPort -le 0 -or
    $ImageStoreCapacityBytes -le 0 -or $HttpServerThreadCount -le 0 -or
    $HttpChunkBytes -le 0 -or $HttpReadBufferBytes -lt 0 -or
    $HttpWriteBufferBytes -lt 0 -or $HttpSocketReceiveBufferBytes -lt 0 -or
    $HttpSocketSendBufferBytes -lt 0) {
    throw 'Invalid E2E numeric parameters'
}

$TaskCount = $DurationSeconds * $PublishRatePerSecond
$MasterWritePublishThreadCount = Resolve-ThreadCount -Value $MasterWritePublishThreads -Name 'MasterWritePublishThreads'
$MasterResultThreadCount = Resolve-ThreadCount -Value $MasterResultThreads -Name 'MasterResultThreads'
if ([string]::IsNullOrWhiteSpace($HttpListenAddress) -or [string]::IsNullOrWhiteSpace($HttpRoute)) {
    throw 'HttpListenAddress and HttpRoute must not be empty'
}

$RabbitMqAuthHeader = Get-BasicAuthHeader -UserName $RabbitMqAdminUser -Password $RabbitMqAdminPass

$ExamplesDir = Join-Path $BuildDir 'examples\task_flow'
$masterExe = Resolve-ExampleArtifact -Name 'mc_task_flow_master_host'
$rabbitPluginDll = Resolve-ExampleArtifact -Name 'amqp_bus'
$httpPluginDll = Resolve-ExampleArtifact -Name 'http_transport'

$runtimeDir = if ([string]::IsNullOrWhiteSpace($OutputRoot)) {
    Join-Path $ExamplesDir 'amqp_task_flow_runtime'
} else {
    Resolve-OutputDirectoryPath -Path $OutputRoot -BasePath (Get-Location).Path -Name 'OutputRoot'
}
$reportDir = Join-Path $runtimeDir 'master'
$runId = 'task-flow-{0}' -f (Get-Date -Format 'yyyyMMdd-HHmmssfff')
$masterModuleConfigPath = Join-Path $reportDir 'master_module_config.json'

$masterLog = Join-Path $reportDir 'master.log'
$masterErrLog = Join-Path $reportDir 'master.err.log'
$summaryPath = Join-Path $reportDir 'master_summary.txt'
$taskMetricsPath = Join-Path $reportDir 'task_metrics.tsv'
$taskMetricsHtmlPath = Join-Path $reportDir 'task_metrics.html'
$timeoutMs = $TimeoutMs

$masterUri = ('amqp://{0}:{1}@{2}:5672/{3}' -f $RabbitMqMasterUser, $RabbitMqMasterPass, $RabbitMqHost, $RabbitMqVHost)
$masterModuleName = 'task_flow_master_bus'
$httpModuleName = 'task_flow_master_http'

$taskExchange = 'mc.task.exchange'
$taskQueue = 'mc.task.queue'
$taskRoutingKey = 'task.ready'
$controlExchange = 'mc.control.exchange'
$resultExchange = 'mc.result.exchange'
$resultQueue = 'mc.result.queue'
$resultRoutingKey = 'result.ready'

$masterProcess = $null

try {
    $existingMasterProcesses = @(Get-TaskFlowProcessByCommandLine `
        -ProcessImageName 'mc_task_flow_master_host.exe' `
        -Needles @($runtimeDir))
    if ($existingMasterProcesses.Count -gt 0) {
        if ($Restart) {
            Write-Host "[run] found $($existingMasterProcesses.Count) existing master process(es) for this runtime dir; stopping because -Restart was specified"
            Stop-TaskFlowProcesses -Processes $existingMasterProcesses
        } else {
            Write-Host "[run] master process already exists for this runtime dir; skip duplicate start: $runtimeDir"
            Write-Host '[run] To restart, stop the existing process first or pass -Restart.'
            return
        }
    }

    Reset-TaskFlowRuntimeDirectory -Path $runtimeDir -Label 'master'
    New-Item -ItemType Directory -Force -Path $reportDir | Out-Null

    Write-Host "[run] build dir: $BuildDir"
    Write-Host "[run] configuration: $Configuration"
    Write-Host "[run] runtime dir: $runtimeDir"
    Write-Host "[run] run id: $runId"
    Write-Host "[run] task count: $TaskCount"
    Write-Host "[run] HTTP listen: $HttpListenAddress`:$HttpPort$HttpRoute"
    Write-Host "[run] HTTP server threads: $HttpServerThreadCount"
    Write-Host "[run] HTTP chunk bytes: $HttpChunkBytes"
    Write-Host "[run] HTTP read/write buffers: $HttpReadBufferBytes / $HttpWriteBufferBytes"
    Write-Host "[run] HTTP socket receive/send buffers: $HttpSocketReceiveBufferBytes / $HttpSocketSendBufferBytes"
    Write-Host "[run] profile: $ProfileName"
    Write-Host "[run] shutdown broadcast: $(-not $SkipShutdown.IsPresent)"
    Write-Host "[run] RabbitMQ API: $RabbitMqApiUrl"
    Write-Host "[run] RabbitMQ AMQP: $RabbitMqHost`:5672"

    Wait-RabbitMqApiReady

    $encodedVHost = Encode-Segment -Value $RabbitMqVHost
    $encodedMasterUser = Encode-Segment -Value $RabbitMqMasterUser
    $encodedWorkerUser = Encode-Segment -Value $RabbitMqWorkerUser
    $encodedTaskExchange = Encode-Segment -Value $taskExchange
    $encodedTaskQueue = Encode-Segment -Value $taskQueue
    $encodedControlExchange = Encode-Segment -Value $controlExchange
    $encodedResultExchange = Encode-Segment -Value $resultExchange
    $encodedResultQueue = Encode-Segment -Value $resultQueue

    Write-Host '[run] bootstrapping RabbitMQ topology'
    Invoke-RabbitMqRequest -Method Put -Path "/vhosts/$encodedVHost" -Body @{}
    Invoke-RabbitMqRequest -Method Put -Path "/users/$encodedMasterUser" -Body @{
        password = $RabbitMqMasterPass
        tags = ''
    }
    Invoke-RabbitMqRequest -Method Put -Path "/users/$encodedWorkerUser" -Body @{
        password = $RabbitMqWorkerPass
        tags = ''
    }
    Invoke-RabbitMqRequest -Method Put -Path "/exchanges/$encodedVHost/$encodedTaskExchange" -Body @{
        type = 'direct'
        durable = $true
        auto_delete = $false
        internal = $false
        arguments = @{}
    }
    Invoke-RabbitMqRequest -Method Put -Path "/exchanges/$encodedVHost/$encodedResultExchange" -Body @{
        type = 'direct'
        durable = $true
        auto_delete = $false
        internal = $false
        arguments = @{}
    }
    Invoke-RabbitMqRequest -Method Put -Path "/exchanges/$encodedVHost/$encodedControlExchange" -Body @{
        type = 'fanout'
        durable = $true
        auto_delete = $false
        internal = $false
        arguments = @{}
    }
    Invoke-RabbitMqRequest -Method Put -Path "/queues/$encodedVHost/$encodedTaskQueue" -Body @{
        durable = $true
        auto_delete = $false
        arguments = @{}
    }
    Invoke-RabbitMqRequest -Method Put -Path "/queues/$encodedVHost/$encodedResultQueue" -Body @{
        durable = $true
        auto_delete = $false
        arguments = @{}
    }
    Invoke-RabbitMqRequest -Method Post -Path "/bindings/$encodedVHost/e/$encodedTaskExchange/q/$encodedTaskQueue" -Body @{
        routing_key = $taskRoutingKey
        arguments = @{}
    }
    Invoke-RabbitMqRequest -Method Post -Path "/bindings/$encodedVHost/e/$encodedResultExchange/q/$encodedResultQueue" -Body @{
        routing_key = $resultRoutingKey
        arguments = @{}
    }
    Invoke-RabbitMqRequest -Method Delete -Path "/queues/$encodedVHost/$encodedTaskQueue/contents" -Body $null
    Invoke-RabbitMqRequest -Method Delete -Path "/queues/$encodedVHost/$encodedResultQueue/contents" -Body $null
    Invoke-RabbitMqRequest -Method Put -Path "/permissions/$encodedVHost/$encodedMasterUser" -Body @{
        configure = '^(mc\.task\.exchange|mc\.task\.queue|mc\.result\.exchange|mc\.result\.queue|mc\.control\.exchange)$'
        write = '^(mc\.task\.exchange|mc\.control\.exchange)$'
        read = '^(mc\.result\.queue)$'
    }
    Invoke-RabbitMqRequest -Method Put -Path "/permissions/$encodedVHost/$encodedWorkerUser" -Body @{
        configure = '^(mc\.task\.queue|mc\.result\.exchange|mc\.control\.exchange|mc\.control\..*)$'
        write = '^(mc\.result\.exchange|mc\.control\.exchange|mc\.control\..*)$'
        read = '^(mc\.task\.queue|mc\.control\..*)$'
    }

    $expectedTaskConsumerCount = $ExpectedWorkerCount
    Write-Host "[run] waiting for $expectedTaskConsumerCount task consumer(s) on $taskQueue"
    Write-Host '[run] note: master host process starts after worker consumers are ready; RabbitMQ UI will not show a master AMQP connection during this wait'
    Wait-QueueConsumerCount `
        -VHost $RabbitMqVHost `
        -QueueName $taskQueue `
        -ExpectedConsumerCount $expectedTaskConsumerCount `
        -TimeoutSeconds $WorkerReadyTimeoutSeconds

    Write-JsonFile `
        -Path $masterModuleConfigPath `
        -Value (New-MasterModuleConfig `
            -BusModuleName $masterModuleName `
            -RabbitPluginPath $rabbitPluginDll `
            -HttpModuleName $httpModuleName `
            -HttpPluginPath $httpPluginDll `
            -Uri $masterUri `
            -ThreadCount $MasterResultThreadCount `
            -ListenAddress $HttpListenAddress `
            -ListenPort $HttpPort `
            -HttpServerThreadCount $HttpServerThreadCount `
            -HttpChunkBytes $HttpChunkBytes `
            -HttpReadBufferBytes $HttpReadBufferBytes `
            -HttpWriteBufferBytes $HttpWriteBufferBytes `
            -HttpSocketReceiveBufferBytes $HttpSocketReceiveBufferBytes `
            -HttpSocketSendBufferBytes $HttpSocketSendBufferBytes)

    Write-Host '[run] launching master'
    $masterProcess = Start-Process -FilePath $masterExe `
        -ArgumentList @(
            '--module-config', $masterModuleConfigPath,
            '--run-id', $runId,
            '--report-dir', $reportDir,
            '--profile-name', $ProfileName,
            '--http-route', $HttpRoute,
            '--task-count', "$TaskCount",
            '--publish-rate', "$PublishRatePerSecond",
            '--image-size-bytes', "$ImageSizeBytes",
            '--image-store-capacity-bytes', "$ImageStoreCapacityBytes",
            '--master-write-publish-threads', "$MasterWritePublishThreadCount",
            '--master-result-threads', "$MasterResultThreadCount",
            '--worker-count', "$ExpectedWorkerCount",
            '--timeout-ms', "$timeoutMs",
            '--http-chunk-bytes', "$HttpChunkBytes",
            '--http-read-buffer-bytes', "$HttpReadBufferBytes",
            '--http-write-buffer-bytes', "$HttpWriteBufferBytes",
            '--http-socket-receive-buffer-bytes', "$HttpSocketReceiveBufferBytes",
            '--http-socket-send-buffer-bytes', "$HttpSocketSendBufferBytes",
            '--send-shutdown', "$(if ($SkipShutdown) { 0 } else { 1 })"
        ) `
        -RedirectStandardOutput $masterLog `
        -RedirectStandardError $masterErrLog `
        -PassThru `
        -Wait

    $masterExitCode = $masterProcess.ExitCode

    if (-not (Test-Path -LiteralPath $summaryPath)) {
        throw "Master summary file not found: $summaryPath"
    }
    if (-not (Test-Path -LiteralPath $taskMetricsPath)) {
        throw "Task metrics file not found: $taskMetricsPath"
    }

    $summaryValues = Parse-KeyValueFile -Path $summaryPath
    $taskRows = Import-Csv -LiteralPath $taskMetricsPath -Delimiter "`t"

    if ([int]$summaryValues['sent_count'] -ne $TaskCount) {
        throw "Expected sent_count=$TaskCount, got $($summaryValues['sent_count'])"
    }
    if ([int]$summaryValues['completed_count'] -ne $TaskCount) {
        throw "Expected completed_count=$TaskCount, got $($summaryValues['completed_count'])"
    }
    if ([int]$summaryValues['success_count'] -ne $TaskCount) {
        throw "Expected success_count=$TaskCount, got $($summaryValues['success_count'])"
    }
    if ([int]$summaryValues['failure_count'] -ne 0) {
        throw "Expected failure_count=0, got $($summaryValues['failure_count'])"
    }
    if ([int]$summaryValues['result_count'] -ne $TaskCount) {
        throw "Expected result_count=$TaskCount, got $($summaryValues['result_count'])"
    }
    if ([int]$summaryValues['worker_count'] -ne $ExpectedWorkerCount) {
        throw "Expected worker_count=$ExpectedWorkerCount, got $($summaryValues['worker_count'])"
    }
    if ([int]$summaryValues['cleanup_failure_count'] -ne 0) {
        throw "Expected cleanup_failure_count=0, got $($summaryValues['cleanup_failure_count'])"
    }
    if ([string]$summaryValues['task_queue'] -ne $taskQueue) {
        throw "Expected task_queue=$taskQueue, got $($summaryValues['task_queue'])"
    }
    if ([int]$summaryValues['image_store_remaining_count'] -ne 0) {
        throw "Expected image_store_remaining_count=0, got $($summaryValues['image_store_remaining_count'])"
    }

    $taskQueueInfo = Wait-QueueEmpty -VHost $RabbitMqVHost -QueueName $taskQueue
    $resultQueueInfo = Wait-QueueEmpty -VHost $RabbitMqVHost -QueueName $resultQueue
    if ((Get-QueueMessageCount -QueueInfo $taskQueueInfo) -ne 0) {
        throw "Expected $taskQueue to be empty after run"
    }
    if ((Get-QueueMessageCount -QueueInfo $resultQueueInfo) -ne 0) {
        throw "Expected $resultQueue to be empty after run"
    }

    $processedRows = @($taskRows | Where-Object { $_.status -eq 'processed' })
    if ($processedRows.Count -ne $TaskCount) {
        throw "Expected $TaskCount processed task rows, found $($processedRows.Count)"
    }
    $cleanupFailedRows = @($taskRows | Where-Object { $_.image_store_status -ne 'deleted' })
    if ($cleanupFailedRows.Count -ne 0) {
        throw "Expected all task rows to have image_store_status=deleted, found $($cleanupFailedRows.Count)"
    }
    if ($taskRows.Count -gt 0) {
        $columns = @($taskRows[0].PSObject.Properties.Name)
        if ($columns -contains 'image_path' -or $columns -contains 'master_write_path') {
            throw 'Task metrics must not contain image_path or master_write_path after HTTP in-memory refactor'
        }
    }

    $observedWorkers = @($processedRows | Group-Object worker_id | Sort-Object Name | ForEach-Object { '{0}:{1}' -f $_.Name, $_.Count })
    Write-Host "[run] observed worker distribution: $($observedWorkers -join ', ')"
    Write-TaskMetricsHtml `
        -Path $taskMetricsHtmlPath `
        -RunId $runId `
        -Summary $summaryValues `
        -Rows $taskRows

    if ($masterExitCode -ne 0) {
        throw "Master exited with code $masterExitCode"
    }

    Write-Host '[run] Task Flow E2E passed'
    Write-Host "[run] master log: $masterLog"
    Write-Host "[run] master err log: $masterErrLog"
    Write-Host "[run] summary: $summaryPath"
    Write-Host "[run] task metrics: $taskMetricsPath"
    Write-Host "[run] task metrics html: $taskMetricsHtmlPath"
} finally {
    if ($null -ne $masterProcess) {
        try {
            if (-not $masterProcess.HasExited) {
                Stop-Process -Id $masterProcess.Id -Force
                Wait-Process -Id $masterProcess.Id -Timeout 10 -ErrorAction SilentlyContinue
            }
        } catch {
        }
        try {
            $masterProcess.Dispose()
        } catch {
        }
    }
}
