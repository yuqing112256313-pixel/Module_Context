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
    [int]$WorkerCount = 5,
    [int]$DurationSeconds = 2,
    [long]$ImageSizeBytes = 20971520,
    [int]$PublishRatePerSecond = 50,
    [int]$AlgorithmDelayMs = 10,
    [string]$MasterWritePublishThreads = 'Auto',
    [string]$MasterResultThreads = 'Auto',
    [int]$WorkerTaskThreads = 64,
    [string]$HttpListenAddress = '127.0.0.1',
    [int]$HttpPort = 50080,
    [string]$HttpEndpoint = '',
    [string]$HttpRoute = '/task-flow/images',
    [long]$ImageStoreCapacityBytes = 4294967296,
    [int]$WorkerReadyTimeoutSeconds = 300,
    [int]$TimeoutMs = 0,
    [switch]$SkipIfRabbitMqUnavailable
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

function Get-BasicAuthHeader {
    param(
        [string]$UserName,
        [string]$Password
    )

    $bytes = [System.Text.Encoding]::ASCII.GetBytes("$UserName`:$Password")
    return 'Basic {0}' -f [Convert]::ToBase64String($bytes)
}

function Test-TcpPort {
    param(
        [string]$HostName,
        [int]$Port,
        [int]$TimeoutMs = 3000
    )

    $client = New-Object System.Net.Sockets.TcpClient
    try {
        $async = $client.BeginConnect($HostName, $Port, $null, $null)
        if (-not $async.AsyncWaitHandle.WaitOne($TimeoutMs)) {
            return $false
        }
        $client.EndConnect($async)
        return $client.Connected
    } catch {
        return $false
    } finally {
        $client.Close()
    }
}

function Test-RabbitMqApi {
    param(
        [string]$ApiUrl,
        [string]$UserName,
        [string]$Password
    )

    $headers = @{
        Authorization = (Get-BasicAuthHeader -UserName $UserName -Password $Password)
    }
    $overviewUri = '{0}/overview' -f $ApiUrl.TrimEnd('/')
    try {
        Invoke-RestMethod -Method Get -Uri $overviewUri -Headers $headers -TimeoutSec 5 | Out-Null
        return $true
    } catch {
        return $false
    }
}

function Wait-ProcessOrThrow {
    param(
        [System.Diagnostics.Process]$Process,
        [int]$TimeoutMs,
        [string]$Name
    )

    if ($TimeoutMs -eq 0) {
        $Process.WaitForExit()
    } elseif (-not $Process.WaitForExit($TimeoutMs)) {
        throw "$Name did not exit within timeout."
    }

    try {
        $Process.Refresh()
    } catch {
    }

    $exitCode = $null
    try {
        $exitCode = $Process.ExitCode
    } catch {
    }
    if ($null -ne $exitCode -and "$exitCode" -ne '' -and $exitCode -ne 0) {
        throw "$Name exited with code $exitCode"
    }
}

$BuildDir = (Resolve-Path -LiteralPath $BuildDir).Path
$Configuration = Get-SettingValue -ExplicitValue $Configuration -EnvName 'Configuration' -FallbackValue 'Debug'
$RabbitMqApiUrl = Get-SettingValue -ExplicitValue $RabbitMqApiUrl -EnvName 'RABBITMQ_API_URL' -FallbackValue 'http://127.0.0.1:15672/api'
$RabbitMqHost = Get-SettingValue -ExplicitValue $RabbitMqHost -EnvName 'RABBITMQ_HOST' -FallbackValue '127.0.0.1'
$RabbitMqAdminUser = Get-SettingValue -ExplicitValue $RabbitMqAdminUser -EnvName 'RABBITMQ_ADMIN_USER' -FallbackValue 'guest'
$RabbitMqAdminPass = Get-SettingValue -ExplicitValue $RabbitMqAdminPass -EnvName 'RABBITMQ_ADMIN_PASS' -FallbackValue 'guest'
$RabbitMqVHost = Get-SettingValue -ExplicitValue $RabbitMqVHost -EnvName 'RABBITMQ_VHOST' -FallbackValue 'mc_integration'
$RabbitMqMasterUser = Get-SettingValue -ExplicitValue $RabbitMqMasterUser -EnvName 'RABBITMQ_MASTER_USER' -FallbackValue 'mc_master'
$RabbitMqMasterPass = Get-SettingValue -ExplicitValue $RabbitMqMasterPass -EnvName 'RABBITMQ_MASTER_PASS' -FallbackValue 'master_secret'
$RabbitMqWorkerUser = Get-SettingValue -ExplicitValue $RabbitMqWorkerUser -EnvName 'RABBITMQ_WORKER_USER' -FallbackValue 'mc_worker'
$RabbitMqWorkerPass = Get-SettingValue -ExplicitValue $RabbitMqWorkerPass -EnvName 'RABBITMQ_WORKER_PASS' -FallbackValue 'worker_secret'

if ([string]::IsNullOrWhiteSpace($HttpEndpoint)) {
    $HttpEndpoint = 'http://127.0.0.1:{0}' -f $HttpPort
}

$rabbitApiReady = Test-RabbitMqApi `
    -ApiUrl $RabbitMqApiUrl `
    -UserName $RabbitMqAdminUser `
    -Password $RabbitMqAdminPass
$rabbitAmqpReady = Test-TcpPort -HostName $RabbitMqHost -Port 5672 -TimeoutMs 3000
if (-not $rabbitApiReady -or -not $rabbitAmqpReady) {
    $message = "RabbitMQ E2E prerequisites are unavailable: API=$rabbitApiReady at $RabbitMqApiUrl, AMQP=$rabbitAmqpReady at ${RabbitMqHost}:5672."
    if ($SkipIfRabbitMqUnavailable) {
        Write-Host "[e2e] skip: $message"
        exit 77
    }
    throw $message
}

if ($WorkerCount -le 0 -or $DurationSeconds -le 0 -or $PublishRatePerSecond -le 0 -or
    $ImageSizeBytes -le 0 -or $AlgorithmDelayMs -lt 0 -or $WorkerTaskThreads -le 0 -or
    $WorkerReadyTimeoutSeconds -lt 0 -or $TimeoutMs -lt 0 -or $HttpPort -le 0 -or
    $ImageStoreCapacityBytes -le 0) {
    throw 'Invalid E2E numeric parameters.'
}
if ([string]::IsNullOrWhiteSpace($HttpListenAddress) -or
    [string]::IsNullOrWhiteSpace($HttpEndpoint) -or
    [string]::IsNullOrWhiteSpace($HttpRoute)) {
    throw 'HTTP parameters must not be empty.'
}

$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$masterScript = Join-Path $scriptDir 'run_task_flow_master.ps1'
$workerScript = Join-Path $scriptDir 'run_task_flow_worker.ps1'
$examplesDir = Join-Path $BuildDir 'examples\task_flow'
$launcherRoot = Join-Path $examplesDir 'amqp_task_flow_local_launcher'
$masterLauncherLog = Join-Path $launcherRoot 'master_launcher.log'
$masterLauncherErrLog = Join-Path $launcherRoot 'master_launcher.err.log'
$workerLauncherLog = Join-Path $launcherRoot 'worker_launcher.log'
$workerLauncherErrLog = Join-Path $launcherRoot 'worker_launcher.err.log'

$masterLauncher = $null
$workerLauncher = $null

try {
    if (Test-Path -LiteralPath $launcherRoot) {
        Remove-Item -LiteralPath $launcherRoot -Recurse -Force
    }
    New-Item -ItemType Directory -Force -Path $launcherRoot | Out-Null

    Write-Host "[e2e] build dir: $BuildDir"
    Write-Host "[e2e] worker count: $WorkerCount"
    Write-Host "[e2e] HTTP endpoint: $HttpEndpoint$HttpRoute"

    $masterLauncher = Start-Process -FilePath 'powershell' `
        -ArgumentList @(
            '-ExecutionPolicy', 'Bypass',
            '-File', $masterScript,
            '-BuildDir', $BuildDir,
            '-Configuration', $Configuration,
            '-RabbitMqApiUrl', $RabbitMqApiUrl,
            '-RabbitMqHost', $RabbitMqHost,
            '-RabbitMqAdminUser', $RabbitMqAdminUser,
            '-RabbitMqAdminPass', $RabbitMqAdminPass,
            '-RabbitMqVHost', $RabbitMqVHost,
            '-RabbitMqMasterUser', $RabbitMqMasterUser,
            '-RabbitMqMasterPass', $RabbitMqMasterPass,
            '-RabbitMqWorkerUser', $RabbitMqWorkerUser,
            '-RabbitMqWorkerPass', $RabbitMqWorkerPass,
            '-ExpectedWorkerCount', "$WorkerCount",
            '-DurationSeconds', "$DurationSeconds",
            '-PublishRatePerSecond', "$PublishRatePerSecond",
            '-ImageSizeBytes', "$ImageSizeBytes",
            '-AlgorithmDelayMs', "$AlgorithmDelayMs",
            '-MasterWritePublishThreads', $MasterWritePublishThreads,
            '-MasterResultThreads', $MasterResultThreads,
            '-WorkerTaskThreads', "$WorkerTaskThreads",
            '-HttpListenAddress', $HttpListenAddress,
            '-HttpPort', "$HttpPort",
            '-HttpRoute', $HttpRoute,
            '-ImageStoreCapacityBytes', "$ImageStoreCapacityBytes",
            '-WorkerReadyTimeoutSeconds', "$WorkerReadyTimeoutSeconds",
            '-TimeoutMs', "$TimeoutMs",
            '-Restart'
        ) `
        -RedirectStandardOutput $masterLauncherLog `
        -RedirectStandardError $masterLauncherErrLog `
        -PassThru

    Start-Sleep -Seconds 3
    if ($masterLauncher.HasExited) {
        throw "Local master launcher exited early with code $($masterLauncher.ExitCode). See $masterLauncherErrLog"
    }

    $workerLauncher = Start-Process -FilePath 'powershell' `
        -ArgumentList @(
            '-ExecutionPolicy', 'Bypass',
            '-File', $workerScript,
            '-BuildDir', $BuildDir,
            '-Configuration', $Configuration,
            '-RabbitMqHost', $RabbitMqHost,
            '-RabbitMqVHost', $RabbitMqVHost,
            '-RabbitMqWorkerUser', $RabbitMqWorkerUser,
            '-RabbitMqWorkerPass', $RabbitMqWorkerPass,
            '-WorkerHostId', 'local',
            '-LocalWorkerCount', "$WorkerCount",
            '-WorkerTaskThreads', "$WorkerTaskThreads",
            '-AlgorithmDelayMs', "$AlgorithmDelayMs",
            '-HttpEndpoint', $HttpEndpoint,
            '-HttpRoute', $HttpRoute,
            '-TimeoutMs', "$TimeoutMs",
            '-OutputRoot', (Join-Path $launcherRoot 'workers'),
            '-Restart'
        ) `
        -RedirectStandardOutput $workerLauncherLog `
        -RedirectStandardError $workerLauncherErrLog `
        -PassThru

    Start-Sleep -Seconds 3
    if ($workerLauncher.HasExited) {
        throw "Local worker launcher exited early with code $($workerLauncher.ExitCode). See $workerLauncherErrLog"
    }

    Wait-ProcessOrThrow -Process $masterLauncher -TimeoutMs $TimeoutMs -Name 'Local master launcher'
    Wait-ProcessOrThrow -Process $workerLauncher -TimeoutMs $TimeoutMs -Name 'Local worker launcher'

    Write-Host "[e2e] local master launcher log: $masterLauncherLog"
    Write-Host "[e2e] local master launcher err log: $masterLauncherErrLog"
    Write-Host "[e2e] local worker launcher log: $workerLauncherLog"
    Write-Host "[e2e] local worker launcher err log: $workerLauncherErrLog"
} finally {
    foreach ($process in @($masterLauncher, $workerLauncher)) {
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
