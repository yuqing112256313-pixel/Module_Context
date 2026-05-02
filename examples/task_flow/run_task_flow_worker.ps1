param(
    [Parameter(Mandatory = $true)]
    [string]$BuildDir,
    [string]$Configuration = '',
    [string]$RabbitMqWorkerUri = '',
    [string]$RabbitMqHost = '',
    [string]$RabbitMqVHost = '',
    [string]$RabbitMqWorkerUser = '',
    [string]$RabbitMqWorkerPass = '',
    [Parameter(Mandatory = $true)]
    [string]$WorkerHostId,
    [int]$LocalWorkerCount = 1,
    [int]$WorkerTaskThreads = 64,
    [int]$AlgorithmDelayMs = 10,
    [bool]$UseAlgorithmPlugin = $true,
    [string]$AlgorithmPluginName = 'tgv_etching',
    [string]$AlgorithmPluginCreateFunc = 'CreatePluginEtching',
    [string]$AlgorithmPluginDestroyFunc = 'DestroyPluginEtching',
    [bool]$AlgorithmPersistInputImages = $false,
    [string]$AlgorithmInputDir = '',
    [string]$SemipluginManagerPath = '',
    [string]$AlgorithmPluginPath = '',
    [int]$TimeoutMs = 0,
    [string]$HttpEndpoint = '',
    [string]$HttpRoute = '/task-flow/images',
    [long]$HttpChunkBytes = 8388608,
    [string]$OutputRoot = '',
    [switch]$UseHostIdAsWorkerId,
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

function Get-FileSha256 {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Path
    )

    if (Get-Command Get-FileHash -ErrorAction SilentlyContinue) {
        return (Get-FileHash -Algorithm SHA256 -LiteralPath $Path).Hash
    }

    $resolvedPath = (Resolve-Path -LiteralPath $Path).Path
    $sha256 = [System.Security.Cryptography.SHA256]::Create()
    $stream = [System.IO.File]::OpenRead($resolvedPath)
    try {
        $hashBytes = $sha256.ComputeHash($stream)
        return (($hashBytes | ForEach-Object { $_.ToString('X2') }) -join '')
    } finally {
        $stream.Dispose()
        $sha256.Dispose()
    }
}

function Resolve-ExampleArtifact {
    param(
        [string]$ExamplesDir,
        [string]$ConfigurationName,
        [string]$Name
    )

    $candidates = @(
        (Join-Path (Join-Path $ExamplesDir $ConfigurationName) "$Name.exe"),
        (Join-Path (Join-Path $ExamplesDir $ConfigurationName) "$Name.dll"),
        (Join-Path $ExamplesDir "$Name.exe"),
        (Join-Path $ExamplesDir "$Name.dll")
    )

    foreach ($candidate in $candidates) {
        if (Test-Path -LiteralPath $candidate) {
            return (Resolve-Path -LiteralPath $candidate).Path
        }
    }

    throw "Artifact not found for $Name. Checked: $($candidates -join ', ')"
}

function ConvertTo-AmqpUri {
    param(
        [string]$HostName,
        [string]$VHost,
        [string]$UserName,
        [string]$Password
    )

    return ('amqp://{0}:{1}@{2}:5672/{3}' -f $UserName, $Password, $HostName, $VHost)
}

function Test-TcpPort {
    param(
        [string]$HostName,
        [int]$Port,
        [int]$TimeoutMs = 3000
    )

    $client = [System.Net.Sockets.TcpClient]::new()
    try {
        $task = $client.ConnectAsync($HostName, $Port)
        if (-not $task.Wait($TimeoutMs)) {
            return $false
        }
        return $client.Connected
    } catch {
        return $false
    } finally {
        $client.Dispose()
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

function New-WorkerModuleConfig {
    param(
        [string]$BusModuleName,
        [string]$RabbitPluginPath,
        [string]$HttpModuleName,
        [string]$HttpPluginPath,
        [string]$SemipluginModuleName,
        [string]$SemipluginManagerPath,
        [string]$AlgorithmPluginName,
        [string]$AlgorithmPluginPath,
        [string]$AlgorithmPluginCreateFunc,
        [string]$AlgorithmPluginDestroyFunc,
        [bool]$UseAlgorithmPlugin,
        [string]$Uri,
        [string]$WorkerId,
        [int]$ThreadCount,
        [string]$Endpoint,
        [long]$HttpChunkBytes
    )

    if ($UseAlgorithmPlugin -and
        ([string]::IsNullOrWhiteSpace($SemipluginManagerPath) -or
         [string]::IsNullOrWhiteSpace($AlgorithmPluginName) -or
         [string]::IsNullOrWhiteSpace($AlgorithmPluginPath) -or
         [string]::IsNullOrWhiteSpace($AlgorithmPluginCreateFunc) -or
         [string]::IsNullOrWhiteSpace($AlgorithmPluginDestroyFunc))) {
        throw 'Algorithm plugin is enabled, but plugin manager path, plugin path, plugin name, create func, or destroy func is empty.'
    }

    $taskQueue = 'mc.task.queue'
    $controlExchange = 'mc.control.exchange'
    $controlQueueSuffix = $WorkerId -replace '[^A-Za-z0-9_.-]', '_'
    $controlQueueInstance = [Guid]::NewGuid().ToString('N')
    $controlQueue = 'mc.control.{0}.{1}' -f $controlQueueSuffix, $controlQueueInstance

    $consumers = @(
        @{
            name = 'control_consumer'
            queue = $controlQueue
            prefetch_count = 1
            auto_ack = $false
        },
        @{
            name = 'task_consumer'
            queue = $taskQueue
            prefetch_count = $ThreadCount
            auto_ack = $false
        }
    )

    $modules = @(
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
                            name = 'mc.result.exchange'
                            type = 'direct'
                            durable = $true
                            passive = $false
                        },
                        @{
                            name = $controlExchange
                            type = 'fanout'
                            durable = $true
                            passive = $false
                        }
                    )
                    queues = @(
                        @{
                            name = $taskQueue
                            durable = $true
                            passive = $false
                        },
                        @{
                            name = $controlQueue
                            durable = $false
                            auto_delete = $true
                            exclusive = $false
                            passive = $false
                        }
                    )
                    bindings = @(
                        @{
                            exchange = $controlExchange
                            queue = $controlQueue
                            routing_key = ''
                        }
                    )
                }
                publishers = @(
                    @{
                        name = 'result_producer'
                        exchange = 'mc.result.exchange'
                        routing_key = 'result.ready'
                        persistent = $false
                        content_type = 'text/plain'
                    }
                )
                consumers = $consumers
            }
        },
        @{
            name = $HttpModuleName
            type = 'http_transport'
            library_path = $HttpPluginPath
            config = @{
                role = 'client'
                endpoint = $Endpoint
                read_timeout_ms = 30000
                write_timeout_ms = 30000
                max_payload_bytes = 1073741824
                chunk_bytes = $HttpChunkBytes
            }
        }
    )

    if ($UseAlgorithmPlugin) {
        $modules += @{
            name = $SemipluginModuleName
            type = 'semiplugin_manager'
            library_path = $SemipluginManagerPath
            config = @{
                plugins = @(
                    @{
                        name = $AlgorithmPluginName
                        type = $AlgorithmPluginName
                        library = $AlgorithmPluginPath
                        create_func = $AlgorithmPluginCreateFunc
                        destroy_func = $AlgorithmPluginDestroyFunc
                    }
                )
            }
        }
    }

    return @{
        schema_version = 2
        modules = $modules
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
            "Suggestion: run Stop-Worker.ps1 first, then retry from an elevated PowerShell session if needed.`n" +
            "Original error: $message")
    }
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

function Get-TaskFlowLogTail {
    param(
        [string]$Path,
        [int]$LineCount = 40
    )

    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
        return ''
    }

    return (Get-Content -LiteralPath $Path -Tail $LineCount -ErrorAction SilentlyContinue |
        Out-String).Trim()
}

$BuildDir = (Resolve-Path -LiteralPath $BuildDir).Path
$Configuration = Get-SettingValue -ExplicitValue $Configuration -EnvName 'Configuration' -FallbackValue 'Debug'
$RabbitMqHost = Get-SettingValue -ExplicitValue $RabbitMqHost -EnvName 'RABBITMQ_HOST' -FallbackValue '127.0.0.1'
$RabbitMqVHost = Get-SettingValue -ExplicitValue $RabbitMqVHost -EnvName 'RABBITMQ_VHOST' -FallbackValue 'mc_integration'
$RabbitMqWorkerUser = Get-SettingValue -ExplicitValue $RabbitMqWorkerUser -EnvName 'RABBITMQ_WORKER_USER' -FallbackValue 'mc_worker'
$RabbitMqWorkerPass = Get-SettingValue -ExplicitValue $RabbitMqWorkerPass -EnvName 'RABBITMQ_WORKER_PASS' -FallbackValue 'worker_secret'
if ([string]::IsNullOrWhiteSpace($HttpEndpoint)) {
    $HttpEndpoint = 'http://{0}:50080' -f $RabbitMqHost
}

if ($LocalWorkerCount -le 0 -or $WorkerTaskThreads -le 0 -or $AlgorithmDelayMs -lt 0 -or
    $TimeoutMs -lt 0 -or $HttpChunkBytes -le 0) {
    throw 'Invalid worker numeric parameters.'
}
if ([string]::IsNullOrWhiteSpace($WorkerHostId)) {
    throw 'WorkerHostId must not be empty.'
}
if ($UseHostIdAsWorkerId -and $LocalWorkerCount -ne 1) {
    throw 'UseHostIdAsWorkerId requires LocalWorkerCount=1.'
}
if ([string]::IsNullOrWhiteSpace($HttpEndpoint) -or [string]::IsNullOrWhiteSpace($HttpRoute)) {
    throw 'HttpEndpoint and HttpRoute must not be empty.'
}

if (-not (Test-TcpPort -HostName $RabbitMqHost -Port 5672 -TimeoutMs 3000)) {
    throw "RabbitMQ AMQP is not reachable from this worker at $RabbitMqHost`:5672. Check config\TaskFlowFieldConfig.psd1 Master.Host, firewall, and RabbitMQ listener."
}

$ExamplesDir = Join-Path $BuildDir 'examples\task_flow'
$workerExe = Resolve-ExampleArtifact -ExamplesDir $ExamplesDir -ConfigurationName $Configuration -Name 'mc_task_flow_worker_host'
$rabbitPluginDll = Resolve-ExampleArtifact -ExamplesDir $ExamplesDir -ConfigurationName $Configuration -Name 'amqp_bus'
$httpPluginDll = Resolve-ExampleArtifact -ExamplesDir $ExamplesDir -ConfigurationName $Configuration -Name 'http_transport'
if ($UseAlgorithmPlugin) {
    if ([string]::IsNullOrWhiteSpace($SemipluginManagerPath)) {
        $SemipluginManagerPath = Resolve-ExampleArtifact -ExamplesDir $ExamplesDir -ConfigurationName $Configuration -Name 'semiplugin_manager'
    } else {
        $SemipluginManagerPath = (Resolve-Path -LiteralPath $SemipluginManagerPath).Path
    }
    if ([string]::IsNullOrWhiteSpace($AlgorithmPluginPath)) {
        $AlgorithmPluginPath = Resolve-ExampleArtifact -ExamplesDir $ExamplesDir -ConfigurationName $Configuration -Name 'tgv_etching_semiplugin'
    } else {
        $AlgorithmPluginPath = (Resolve-Path -LiteralPath $AlgorithmPluginPath).Path
    }
}
$runtimeDir = if ([string]::IsNullOrWhiteSpace($OutputRoot)) {
    Join-Path $ExamplesDir 'amqp_task_flow_worker_runtime'
} else {
    Resolve-OutputDirectoryPath -Path $OutputRoot -BasePath (Get-Location).Path -Name 'OutputRoot'
}
$hostRuntimeDir = Join-Path $runtimeDir $WorkerHostId

$resolvedWorkerUri = if (-not [string]::IsNullOrWhiteSpace($RabbitMqWorkerUri)) {
    $RabbitMqWorkerUri
} else {
    ConvertTo-AmqpUri `
        -HostName $RabbitMqHost `
        -VHost $RabbitMqVHost `
        -UserName $RabbitMqWorkerUser `
        -Password $RabbitMqWorkerPass
}

$workerProcesses = @()
$workerArtifacts = @()

try {
    $existingProcesses = @(Get-TaskFlowProcessByCommandLine `
        -ProcessImageName 'mc_task_flow_worker_host.exe' `
        -Needles @($hostRuntimeDir))
    if ($existingProcesses.Count -gt 0) {
        if ($Restart) {
            Write-Host "[worker] found $($existingProcesses.Count) existing worker process(es) for this runtime dir; stopping because -Restart was specified"
            Stop-TaskFlowProcesses -Processes $existingProcesses
        } else {
            Write-Host "[worker] worker process already exists for this runtime dir; skip duplicate start: $hostRuntimeDir"
            Write-Host '[worker] To restart, run Stop-Worker.ps1 first or pass -Restart.'
            return
        }
    }

    Reset-TaskFlowRuntimeDirectory -Path $hostRuntimeDir -Label 'worker'

    Write-Host "[worker] build dir: $BuildDir"
    Write-Host "[worker] host id: $WorkerHostId"
    Write-Host "[worker] local worker count: $LocalWorkerCount"
    Write-Host "[worker] runtime dir: $hostRuntimeDir"
    Write-Host "[worker] RabbitMQ AMQP: $RabbitMqHost`:5672"
    Write-Host "[worker] HTTP endpoint: $HttpEndpoint$HttpRoute"
    Write-Host "[worker] HTTP chunk bytes: $HttpChunkBytes"
    Write-Host "[worker] amqp_bus.dll: $rabbitPluginDll"
    Write-Host "[worker] amqp_bus.dll sha256: $(Get-FileSha256 -Path $rabbitPluginDll)"
    if ($UseAlgorithmPlugin) {
        Write-Host "[worker] semiplugin_manager.dll: $SemipluginManagerPath"
        Write-Host "[worker] algorithm plugin: $AlgorithmPluginName -> $AlgorithmPluginPath"
        Write-Host "[worker] algorithm plugin sha256: $(Get-FileSha256 -Path $AlgorithmPluginPath)"
        Write-Host "[worker] algorithm input persist: $AlgorithmPersistInputImages"
    } else {
        Write-Host '[worker] algorithm plugin disabled'
    }

    foreach ($index in 1..$LocalWorkerCount) {
        $workerId = if ($UseHostIdAsWorkerId) {
            $WorkerHostId
        } else {
            '{0}-{1:d2}' -f $WorkerHostId, $index
        }
        $workerDir = Join-Path $hostRuntimeDir $workerId
        $workerLog = Join-Path $workerDir 'worker.log'
        $workerErrLog = Join-Path $workerDir 'worker.err.log'
        $moduleConfigPath = Join-Path $workerDir 'worker_module_config.json'
        $moduleSuffix = ($workerId -replace '[^A-Za-z0-9_]', '_')
        $moduleName = 'task_flow_worker_bus_{0}' -f $moduleSuffix
        $httpModuleName = 'task_flow_worker_http_{0}' -f $moduleSuffix
        $semipluginModuleName = 'task_flow_worker_semiplugin_{0}' -f $moduleSuffix
        $workerAlgorithmInputDir = if ([string]::IsNullOrWhiteSpace($AlgorithmInputDir)) {
            Join-Path $workerDir 'algorithm_input'
        } else {
            Resolve-OutputDirectoryPath -Path $AlgorithmInputDir -BasePath $workerDir -Name 'AlgorithmInputDir'
        }
        New-Item -ItemType Directory -Force -Path $workerDir | Out-Null

        Write-JsonFile `
            -Path $moduleConfigPath `
            -Value (New-WorkerModuleConfig `
                -BusModuleName $moduleName `
                -RabbitPluginPath $rabbitPluginDll `
                -HttpModuleName $httpModuleName `
                -HttpPluginPath $httpPluginDll `
                -SemipluginModuleName $semipluginModuleName `
                -SemipluginManagerPath $SemipluginManagerPath `
                -AlgorithmPluginName $AlgorithmPluginName `
                -AlgorithmPluginPath $AlgorithmPluginPath `
                -AlgorithmPluginCreateFunc $AlgorithmPluginCreateFunc `
                -AlgorithmPluginDestroyFunc $AlgorithmPluginDestroyFunc `
                -UseAlgorithmPlugin $UseAlgorithmPlugin `
                -Uri $resolvedWorkerUri `
                -WorkerId $workerId `
                -ThreadCount $WorkerTaskThreads `
                -Endpoint $HttpEndpoint `
                -HttpChunkBytes $HttpChunkBytes)

        $workerArgs = @(
            '--module-config', $moduleConfigPath,
            '--worker-id', $workerId,
            '--output-dir', $workerDir,
            '--worker-task-threads', "$WorkerTaskThreads",
            '--algorithm-delay-ms', "$AlgorithmDelayMs",
            '--timeout-ms', "$TimeoutMs",
            '--http-endpoint', $HttpEndpoint,
            '--http-route', $HttpRoute
        )
        if ($UseAlgorithmPlugin) {
            $workerArgs += @(
                '--algorithm-plugin-name', $AlgorithmPluginName,
                '--algorithm-persist-input-images', "$AlgorithmPersistInputImages",
                '--algorithm-input-dir', $workerAlgorithmInputDir
            )
        }

        $previousAlgorithmDelay = [Environment]::GetEnvironmentVariable(
            'TASK_FLOW_ALGORITHM_DELAY_MS',
            'Process')
        $process = $null
        try {
            [Environment]::SetEnvironmentVariable(
                'TASK_FLOW_ALGORITHM_DELAY_MS',
                "$AlgorithmDelayMs",
                'Process')
            $process = Start-Process -FilePath $workerExe `
                -ArgumentList $workerArgs `
                -RedirectStandardOutput $workerLog `
                -RedirectStandardError $workerErrLog `
                -PassThru
        } finally {
            [Environment]::SetEnvironmentVariable(
                'TASK_FLOW_ALGORITHM_DELAY_MS',
                $previousAlgorithmDelay,
                'Process')
        }

        $workerProcesses += $process
        $workerArtifacts += [pscustomobject]@{
            WorkerId = $workerId
            WorkerDir = $workerDir
            Log = $workerLog
            ErrLog = $workerErrLog
            ModuleConfig = $moduleConfigPath
            Process = $process
        }
        Write-Host "[worker] $workerId pid: $($process.Id)"
        Write-Host "[worker] $workerId log: $workerLog"
        Write-Host "[worker] $workerId err log: $workerErrLog"
    }

    Start-Sleep -Seconds 3
    foreach ($artifact in $workerArtifacts) {
        if ($artifact.Process.HasExited) {
            $stdoutTail = Get-TaskFlowLogTail -Path $artifact.Log
            $stderrTail = Get-TaskFlowLogTail -Path $artifact.ErrLog
            throw ("$($artifact.WorkerId) exited early with code $($artifact.Process.ExitCode).`n" +
                "stdout tail:`n$stdoutTail`n" +
                "stderr tail:`n$stderrTail")
        }
    }

    foreach ($artifact in $workerArtifacts) {
        if ($TimeoutMs -eq 0) {
            $artifact.Process.WaitForExit()
        } elseif (-not $artifact.Process.WaitForExit($TimeoutMs)) {
            throw "$($artifact.WorkerId) did not exit within timeout."
        }

        try {
            $artifact.Process.Refresh()
        } catch {
        }

        $exitCode = $null
        try {
            $exitCode = $artifact.Process.ExitCode
        } catch {
        }

        if ($null -ne $exitCode -and "$exitCode" -ne '' -and $exitCode -ne 0) {
            $stdoutTail = Get-TaskFlowLogTail -Path $artifact.Log
            $stderrTail = Get-TaskFlowLogTail -Path $artifact.ErrLog
            throw ("$($artifact.WorkerId) exited with code $exitCode.`n" +
                "stdout tail:`n$stdoutTail`n" +
                "stderr tail:`n$stderrTail")
        }
    }

    Write-Host '[worker] all worker processes exited cleanly'
    foreach ($artifact in $workerArtifacts) {
        Write-Host "[worker] $($artifact.WorkerId) log: $($artifact.Log)"
        Write-Host "[worker] $($artifact.WorkerId) err log: $($artifact.ErrLog)"
    }
} finally {
    foreach ($process in $workerProcesses) {
        try {
            if ($null -ne $process -and -not $process.HasExited) {
                Stop-Process -Id $process.Id -Force
                Wait-Process -Id $process.Id -Timeout 10 -ErrorAction SilentlyContinue
            }
        } catch {
        }
        try {
            if ($null -ne $process) {
                $process.Dispose()
            }
        } catch {
        }
    }
}
