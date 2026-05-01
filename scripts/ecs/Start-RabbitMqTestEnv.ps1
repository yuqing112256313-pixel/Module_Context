param(
    [string]$ErlangHome = 'H:\Tools\Erlang\OTP-27.3.4.3',
    [string]$RabbitMQHome = 'H:\Tools\RabbitMQ\rabbitmq_server-4.2.5',
    [string]$TestRoot = 'H:\Codex\RabbitMQTestEnv',
    [string]$ServiceName = 'RabbitMQ_ModuleContext',
    [string]$NodeName = 'module_context_test@localhost',
    [int]$AmqpPort = 5672,
    [int]$ManagementPort = 15672,
    [int]$DistributionPort = 25672,
    [switch]$Stop
)

$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'

$utf8 = New-Object System.Text.UTF8Encoding $false
[Console]::InputEncoding = $utf8
[Console]::OutputEncoding = $utf8
$OutputEncoding = $utf8
& chcp.com 65001 > $null

$rabbitService = Join-Path $RabbitMQHome 'sbin\rabbitmq-service.bat'
$rabbitPlugins = Join-Path $RabbitMQHome 'sbin\rabbitmq-plugins.bat'
$baseDir = Join-Path $TestRoot 'base'
$configDir = Join-Path $TestRoot 'config'
$configBase = Join-Path $configDir 'rabbitmq'
$configFile = "$configBase.conf"

function Set-RabbitMqProcessEnvironment {
    $env:ERLANG_HOME = $ErlangHome
    $env:RABBITMQ_HOME = $RabbitMQHome
    $env:RABBITMQ_BASE = $baseDir
    $env:RABBITMQ_NODENAME = $NodeName
    $env:RABBITMQ_NODE_PORT = "$AmqpPort"
    $env:RABBITMQ_DIST_PORT = "$DistributionPort"
    $env:RABBITMQ_CONFIG_FILE = $configBase
    $env:RABBITMQ_SERVICENAME = $ServiceName
    $env:Path = "$ErlangHome\bin;$RabbitMQHome\sbin;$env:Path"
}

function Test-HttpBasic {
    param([string]$Uri)

    try {
        $basic = [Convert]::ToBase64String([Text.Encoding]::ASCII.GetBytes('guest:guest'))
        $response = Invoke-WebRequest `
            -UseBasicParsing `
            -Headers @{ Authorization = "Basic $basic" } `
            -Uri $Uri `
            -TimeoutSec 2
        return ($response.StatusCode -ge 200 -and $response.StatusCode -lt 300)
    } catch {
        return $false
    }
}

function Wait-RabbitMqReady {
    $apiReady = $false
    $amqpReady = $false
    for ($i = 1; $i -le 120; $i++) {
        $amqpReady = [bool](Get-NetTCPConnection `
            -LocalAddress 127.0.0.1 `
            -LocalPort $AmqpPort `
            -State Listen `
            -ErrorAction SilentlyContinue)
        $apiReady = Test-HttpBasic -Uri "http://127.0.0.1:$ManagementPort/api/overview"
        if ($amqpReady -and $apiReady) {
            return [pscustomobject]@{
                AmqpReady = $true
                ApiReady = $true
            }
        }
        Start-Sleep -Seconds 1
    }

    return [pscustomobject]@{
        AmqpReady = $amqpReady
        ApiReady = $apiReady
    }
}

if (-not (Test-Path -LiteralPath (Join-Path $ErlangHome 'bin\erl.exe'))) {
    throw "Erlang not found: $ErlangHome"
}
if (-not (Test-Path -LiteralPath $rabbitService)) {
    throw "RabbitMQ service script not found: $rabbitService"
}

Set-RabbitMqProcessEnvironment

if ($Stop) {
    $service = Get-Service -Name $ServiceName -ErrorAction SilentlyContinue
    if ($service -and $service.Status -ne 'Stopped') {
        & $rabbitService stop
        if ($LASTEXITCODE -ne 0) {
            throw "rabbitmq-service stop failed with exit code $LASTEXITCODE"
        }
    }
    [pscustomobject]@{
        ServiceName = $ServiceName
        ServiceStatus = (Get-Service -Name $ServiceName -ErrorAction SilentlyContinue).Status.ToString()
    } | ConvertTo-Json -Depth 3
    exit 0
}

New-Item -ItemType Directory -Force -Path $TestRoot, $baseDir, $configDir | Out-Null
@"
listeners.tcp.1 = 127.0.0.1:$AmqpPort
management.tcp.ip = 127.0.0.1
management.tcp.port = $ManagementPort
loopback_users.guest = true
"@ | Set-Content -LiteralPath $configFile -Encoding ASCII

$service = Get-Service -Name $ServiceName -ErrorAction SilentlyContinue
if (-not $service) {
    & $rabbitPlugins enable --offline rabbitmq_management
    if ($LASTEXITCODE -ne 0) {
        throw "rabbitmq-plugins enable failed with exit code $LASTEXITCODE"
    }

    & $rabbitService install
    if ($LASTEXITCODE -ne 0) {
        throw "rabbitmq-service install failed with exit code $LASTEXITCODE"
    }
    & sc.exe config $ServiceName start= demand | Out-Null
}

$service = Get-Service -Name $ServiceName
if ($service.Status -ne 'Running') {
    & $rabbitService start
    if ($LASTEXITCODE -ne 0) {
        throw "rabbitmq-service start failed with exit code $LASTEXITCODE"
    }
}

$ready = Wait-RabbitMqReady
$service = Get-Service -Name $ServiceName

[pscustomobject]@{
    ServiceName = $ServiceName
    ServiceStatus = $service.Status.ToString()
    StartType = $service.StartType.ToString()
    ErlangHome = $ErlangHome
    RabbitMQHome = $RabbitMQHome
    RabbitMQBase = $baseDir
    ConfigFile = $configFile
    NodeName = $NodeName
    Amqp = "127.0.0.1:$AmqpPort"
    ManagementApi = "http://127.0.0.1:$ManagementPort/api"
    AmqpReady = $ready.AmqpReady
    ApiReady = $ready.ApiReady
} | ConvertTo-Json -Depth 4
