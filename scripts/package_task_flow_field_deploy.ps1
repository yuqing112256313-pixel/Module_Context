param(
    [string]$ToolchainsRoot = 'C:\toolchains',
    [string]$BuildDir = '',
    [string]$OutputRoot = '',
    [string]$Configuration = 'Debug',
    [switch]$SkipBuild
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

function Require-File {
    param(
        [string]$Path,
        [string]$Label
    )

    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
        throw "$Label not found: $Path"
    }
    return (Resolve-Path -LiteralPath $Path).Path
}

function Require-Directory {
    param(
        [string]$Path,
        [string]$Label
    )

    if (-not (Test-Path -LiteralPath $Path -PathType Container)) {
        throw "$Label not found: $Path"
    }
    return (Resolve-Path -LiteralPath $Path).Path
}

function Copy-RequiredFile {
    param(
        [string]$Source,
        [string]$DestinationDirectory
    )

    $resolvedSource = Require-File -Path $Source -Label 'required file'
    New-Item -ItemType Directory -Force -Path $DestinationDirectory | Out-Null
    Copy-Item -LiteralPath $resolvedSource -Destination $DestinationDirectory -Force
}

function Copy-PowerShellScriptFile {
    param(
        [string]$Source,
        [string]$DestinationDirectory
    )

    $resolvedSource = Require-File -Path $Source -Label 'PowerShell script'
    New-Item -ItemType Directory -Force -Path $DestinationDirectory | Out-Null
    $destination = Join-Path $DestinationDirectory (Split-Path -Leaf $resolvedSource)
    $text = [System.IO.File]::ReadAllText($resolvedSource, [System.Text.Encoding]::UTF8)
    $utf8Bom = New-Object System.Text.UTF8Encoding -ArgumentList $true
    [System.IO.File]::WriteAllText($destination, $text, $utf8Bom)
}

function Copy-RequiredDirectory {
    param(
        [string]$Source,
        [string]$Destination
    )

    $resolvedSource = Require-Directory -Path $Source -Label 'required directory'
    if (Test-Path -LiteralPath $Destination) {
        Remove-Item -LiteralPath $Destination -Recurse -Force
    }
    New-Item -ItemType Directory -Force -Path (Split-Path -Parent $Destination) | Out-Null
    Copy-Item -LiteralPath $resolvedSource -Destination $Destination -Recurse -Force
}

function Copy-FieldScript {
    param(
        [string]$ScriptName,
        [string]$DestinationDirectory
    )

    Copy-PowerShellScriptFile `
        -Source (Join-Path $script:FieldDeployScriptRoot $ScriptName) `
        -DestinationDirectory $DestinationDirectory
}

function Write-Sha256File {
    param(
        [string[]]$Paths,
        [string]$OutputPath
    )

    $lines = @()
    foreach ($path in $Paths) {
        $hash = Get-FileHash -Algorithm SHA256 -LiteralPath $path
        $lines += ('{0}  {1}' -f $hash.Hash.ToLowerInvariant(), (Split-Path -Leaf $path))
    }
    Set-Content -LiteralPath $OutputPath -Value $lines -Encoding UTF8
}

$SourceRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
$FieldDeployRoot = Join-Path $SourceRoot 'examples\task_flow\field_deploy'
$FieldDeployScriptRoot = Join-Path $FieldDeployRoot 'scripts'

if ([string]::IsNullOrWhiteSpace($BuildDir)) {
    $BuildDir = Join-Path $ToolchainsRoot 'build\module-context-mingw-e2e-debug'
}
if ([string]::IsNullOrWhiteSpace($OutputRoot)) {
    $OutputRoot = Join-Path $ToolchainsRoot 'field-test-package'
}

$cmake = Require-File `
    -Path (Join-Path $ToolchainsRoot 'portable\cmake-4.3.2-windows-x86_64\bin\cmake.exe') `
    -Label 'cmake'
$ninjaDir = Require-Directory `
    -Path (Join-Path $ToolchainsRoot 'portable\ninja-1.13.2') `
    -Label 'ninja directory'
$mingwRoot = Require-Directory `
    -Path (Join-Path $ToolchainsRoot 'portable\llvm-mingw-20260421-ucrt-x86_64') `
    -Label 'llvm-mingw root'
$mingwBin = Join-Path $mingwRoot 'bin'
$ninja = Require-File -Path (Join-Path $ninjaDir 'ninja.exe') -Label 'ninja'
$otpInstaller = Require-File `
    -Path (Join-Path $ToolchainsRoot 'downloads\otp_win64_27.3.4.11.exe') `
    -Label 'Erlang installer'
$rabbitZip = Require-File `
    -Path (Join-Path $ToolchainsRoot 'downloads\rabbitmq-server-windows-4.2.5.zip') `
    -Label 'RabbitMQ zip'
$cppHttplibLicense = Require-File `
    -Path (Join-Path $SourceRoot 'third_party\cpp-httplib\LICENSE') `
    -Label 'cpp-httplib LICENSE'

if (-not $SkipBuild) {
    $env:PATH = "$mingwBin;$ninjaDir;$(Split-Path -Parent $cmake);$env:PATH"
    & $cmake `
        -S $SourceRoot `
        -B $BuildDir `
        -G Ninja `
        "-DCMAKE_BUILD_TYPE=$Configuration" `
        -DMC_BUILD_TESTS=ON `
        -DMC_BUILD_AMQP_BUS_MODULE=ON `
        -DMC_BUILD_HTTP_TRANSPORT_MODULE=ON `
        -DMC_BUILD_SEMIPLUGIN_MANAGER_MODULE=ON `
        -DMC_BUILD_TASK_FLOW_E2E=ON
    if ($LASTEXITCODE -ne 0) {
        throw "cmake configure failed: $LASTEXITCODE"
    }

    & $cmake --build $BuildDir --target mc_task_flow_master_host mc_task_flow_worker_host amqp_bus http_transport semiplugin_manager tgv_etching_semiplugin
    if ($LASTEXITCODE -ne 0) {
        throw "cmake build failed: $LASTEXITCODE"
    }
}

$packageName = 'task-flow-{0}' -f (Get-Date -Format 'yyyyMMdd-HHmmss')
$packageRoot = Join-Path $OutputRoot $packageName
$stageRoot = Join-Path $packageRoot 'stage'
$masterRoot = Join-Path $stageRoot 'TaskFlowMaster'
$workerRoot = Join-Path $stageRoot 'TaskFlowWorker'
$masterBin = Join-Path $masterRoot 'build\examples\task_flow'
$workerBin = Join-Path $workerRoot 'build\examples\task_flow'
$buildTaskFlowDir = Join-Path $BuildDir 'examples\task_flow'
$buildAmqpBusDll = Join-Path $BuildDir 'modules\amqp_bus\amqp_bus.dll'
$buildHttpTransportDll = Join-Path $BuildDir 'modules\http_transport\http_transport.dll'
$buildSemipluginManagerDll = Join-Path $BuildDir 'modules\semiplugin_manager\semiplugin_manager.dll'
$buildTgvEtchingPluginDll = Join-Path $buildTaskFlowDir 'tgv_etching_semiplugin.dll'

New-Item -ItemType Directory -Force -Path $masterBin, $workerBin | Out-Null

foreach ($name in @(
    'mc_task_flow_master_host.exe',
    'libmc_core_framework.dll'
)) {
    Copy-RequiredFile -Source (Join-Path $buildTaskFlowDir $name) -DestinationDirectory $masterBin
}
Copy-RequiredFile -Source $buildAmqpBusDll -DestinationDirectory $masterBin
Copy-RequiredFile -Source $buildHttpTransportDll -DestinationDirectory $masterBin
foreach ($name in @('libc++.dll', 'libunwind.dll', 'libwinpthread-1.dll')) {
    $runtimePath = Join-Path $mingwBin $name
    if (Test-Path -LiteralPath $runtimePath -PathType Leaf) {
        Copy-RequiredFile -Source $runtimePath -DestinationDirectory $masterBin
    }
}

foreach ($name in @(
    'mc_task_flow_worker_host.exe',
    'libmc_core_framework.dll'
)) {
    Copy-RequiredFile -Source (Join-Path $buildTaskFlowDir $name) -DestinationDirectory $workerBin
}
Copy-RequiredFile -Source $buildAmqpBusDll -DestinationDirectory $workerBin
Copy-RequiredFile -Source $buildHttpTransportDll -DestinationDirectory $workerBin
Copy-RequiredFile -Source $buildSemipluginManagerDll -DestinationDirectory $workerBin
Copy-RequiredFile -Source $buildTgvEtchingPluginDll -DestinationDirectory $workerBin
foreach ($name in @('libc++.dll', 'libunwind.dll', 'libwinpthread-1.dll')) {
    $runtimePath = Join-Path $mingwBin $name
    if (Test-Path -LiteralPath $runtimePath -PathType Leaf) {
        Copy-RequiredFile -Source $runtimePath -DestinationDirectory $workerBin
    }
}
foreach ($runtimeDll in @(Get-ChildItem -LiteralPath $buildTaskFlowDir -Filter '*.dll' -File -ErrorAction SilentlyContinue)) {
    Copy-RequiredFile -Source $runtimeDll.FullName -DestinationDirectory $workerBin
}

$masterScripts = Join-Path $masterRoot 'scripts'
$workerScripts = Join-Path $workerRoot 'scripts'
New-Item -ItemType Directory -Force -Path $masterScripts, $workerScripts | Out-Null
Copy-PowerShellScriptFile `
    -Source (Join-Path $SourceRoot 'examples\task_flow\run_task_flow_master.ps1') `
    -DestinationDirectory $masterScripts
Copy-PowerShellScriptFile `
    -Source (Join-Path $SourceRoot 'examples\task_flow\run_task_flow_worker.ps1') `
    -DestinationDirectory $workerScripts

foreach ($name in @(
    'TaskFlowFieldConfig.ps1',
    'Install-MasterPrereqs.ps1',
    'Start-MasterTest.ps1',
    'Start-RabbitMq.ps1',
    'Stop-TestEnv.ps1'
)) {
    Copy-FieldScript -ScriptName $name -DestinationDirectory $masterScripts
}

foreach ($name in @(
    'TaskFlowFieldConfig.ps1',
    'Install-WorkerPrereqs.ps1',
    'Start-Worker.ps1',
    'Stop-Worker.ps1'
)) {
    Copy-FieldScript -ScriptName $name -DestinationDirectory $workerScripts
}

Copy-RequiredDirectory `
    -Source (Join-Path $FieldDeployRoot 'config') `
    -Destination (Join-Path $packageRoot 'config')
Copy-RequiredDirectory `
    -Source (Join-Path $FieldDeployRoot 'config') `
    -Destination (Join-Path $masterRoot 'config')
Copy-RequiredDirectory `
    -Source (Join-Path $FieldDeployRoot 'config') `
    -Destination (Join-Path $workerRoot 'config')

Copy-RequiredFile -Source (Join-Path $FieldDeployRoot 'README.md') -DestinationDirectory $packageRoot
Copy-RequiredFile -Source (Join-Path $FieldDeployRoot 'README.md') -DestinationDirectory $masterRoot
Copy-RequiredFile -Source (Join-Path $FieldDeployRoot 'README.md') -DestinationDirectory $workerRoot

$masterLicenses = Join-Path $masterRoot 'licenses'
$workerLicenses = Join-Path $workerRoot 'licenses'
New-Item -ItemType Directory -Force -Path $masterLicenses, $workerLicenses | Out-Null
Copy-Item -LiteralPath $cppHttplibLicense -Destination (Join-Path $masterLicenses 'cpp-httplib-LICENSE') -Force
Copy-Item -LiteralPath $cppHttplibLicense -Destination (Join-Path $workerLicenses 'cpp-httplib-LICENSE') -Force

$masterInstallers = Join-Path $masterRoot 'installers'
New-Item -ItemType Directory -Force -Path $masterInstallers | Out-Null
Copy-RequiredFile -Source $otpInstaller -DestinationDirectory $masterInstallers
Copy-RequiredFile -Source $rabbitZip -DestinationDirectory $masterInstallers

$masterZip = Join-Path $packageRoot 'TaskFlowMaster.zip'
$workerZip = Join-Path $packageRoot 'TaskFlowWorker.zip'
Compress-Archive -Path (Join-Path $masterRoot '*') -DestinationPath $masterZip -Force
Compress-Archive -Path (Join-Path $workerRoot '*') -DestinationPath $workerZip -Force
Remove-Item -LiteralPath $stageRoot -Recurse -Force

Write-Sha256File `
    -Paths @(
        $masterZip,
        $workerZip,
        (Join-Path $packageRoot 'README.md'),
        (Join-Path $packageRoot 'config\TaskFlowFieldConfig.psd1')
    ) `
    -OutputPath (Join-Path $packageRoot 'SHA256SUMS.txt')

Write-Host "[package] output: $packageRoot"
Write-Host "[package] master: $masterZip"
Write-Host "[package] worker: $workerZip"
