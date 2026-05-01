param(
    [string]$InstallRoot = ''
)

$ErrorActionPreference = 'Continue'

. (Join-Path $PSScriptRoot 'TaskFlowFieldConfig.ps1')

function Normalize-PathText {
    param([string]$Path)

    if ([string]::IsNullOrWhiteSpace($Path)) {
        return ''
    }
    try {
        return ([System.IO.Path]::GetFullPath($Path)).TrimEnd('\')
    } catch {
        return $Path.TrimEnd('\')
    }
}

function Test-PathUnderRoot {
    param(
        [string]$Path,
        [string]$Root
    )

    $normalizedPath = Normalize-PathText -Path $Path
    $normalizedRoot = Normalize-PathText -Path $Root
    if ([string]::IsNullOrWhiteSpace($normalizedPath) -or [string]::IsNullOrWhiteSpace($normalizedRoot)) {
        return $false
    }

    return $normalizedPath.Equals($normalizedRoot, [System.StringComparison]::OrdinalIgnoreCase) -or
        $normalizedPath.StartsWith($normalizedRoot + '\', [System.StringComparison]::OrdinalIgnoreCase)
}

function Stop-ProcessByIdSafe {
    param(
        [int]$ProcessId,
        [string]$Label
    )

    try {
        $process = Get-Process -Id $ProcessId -ErrorAction Stop
        Stop-Process -Id $ProcessId -Force -ErrorAction SilentlyContinue
        try {
            Wait-Process -Id $ProcessId -Timeout 10 -ErrorAction SilentlyContinue
        } catch {
        }
        try {
            $process.Dispose()
        } catch {
        }
        Write-Host "[stop] stopped $Label pid=$ProcessId"
    } catch {
    }
}

function Stop-TaskFlowHostProcesses {
    param([string]$Root)

    $names = @('mc_task_flow_master_host.exe', 'mc_task_flow_worker_host.exe')
    foreach ($name in $names) {
        $processes = @(Get-CimInstance Win32_Process -Filter "Name='$name'" -ErrorAction SilentlyContinue)
        foreach ($process in $processes) {
            $commandLine = [string]$process.CommandLine
            $executablePath = [string]$process.ExecutablePath
            $belongsToRoot =
                (Test-PathUnderRoot -Path $executablePath -Root $Root) -or
                ($commandLine.IndexOf($Root, [System.StringComparison]::OrdinalIgnoreCase) -ge 0)
            if ($belongsToRoot -or [string]::IsNullOrWhiteSpace($commandLine)) {
                Stop-ProcessByIdSafe -ProcessId ([int]$process.ProcessId) -Label $name
            }
        }
    }
}

function Stop-InstallRootErlangProcesses {
    param([string]$Root)

    $names = @('beam.smp.exe', 'erl.exe', 'erl_child_setup.exe', 'epmd.exe')
    foreach ($name in $names) {
        $processes = @(Get-CimInstance Win32_Process -Filter "Name='$name'" -ErrorAction SilentlyContinue)
        foreach ($process in $processes) {
            $commandLine = [string]$process.CommandLine
            $executablePath = [string]$process.ExecutablePath
            if ((Test-PathUnderRoot -Path $executablePath -Root $Root) -or
                ($commandLine.IndexOf($Root, [System.StringComparison]::OrdinalIgnoreCase) -ge 0)) {
                Stop-ProcessByIdSafe -ProcessId ([int]$process.ProcessId) -Label $name
            }
        }
    }
}

function Release-CurrentDirectoryIfNeeded {
    param([string]$Root)

    try {
        $current = (Get-Location).Path
        if (Test-PathUnderRoot -Path $current -Root $Root) {
            $parent = Split-Path -Parent (Normalize-PathText -Path $Root)
            if ([string]::IsNullOrWhiteSpace($parent)) {
                $parent = 'D:\98_Enzo'
            }
            if (-not (Test-Path -LiteralPath $parent -PathType Container)) {
                New-Item -ItemType Directory -Force -Path $parent | Out-Null
            }
            Set-Location -LiteralPath $parent
            Write-Host "[stop] PowerShell current directory moved to: $parent"
        }
    } catch {
        Write-Warning "Failed to move PowerShell current directory: $($_.Exception.Message)"
    }
}

$packageRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
try {
    $fieldConfig = Import-TaskFlowFieldConfig -PackageRoot $packageRoot
    if ([string]::IsNullOrWhiteSpace($InstallRoot)) {
        $InstallRoot = [string]$fieldConfig.Master.InstallRoot
    }
} catch {
    if ([string]::IsNullOrWhiteSpace($InstallRoot)) {
        $InstallRoot = 'D:\98_Enzo\TaskFlowMaster'
    }
}
Release-CurrentDirectoryIfNeeded -Root $InstallRoot
Stop-TaskFlowHostProcesses -Root $InstallRoot

$envFile = Join-Path $packageRoot 'runtime\master_env.ps1'
if (Test-Path -LiteralPath $envFile) {
    . $envFile
}

if (-not [string]::IsNullOrWhiteSpace($env:RABBITMQ_HOME)) {
    $ctl = Join-Path $env:RABBITMQ_HOME 'sbin\rabbitmqctl.bat'
    $diagnostics = Join-Path $env:RABBITMQ_HOME 'sbin\rabbitmq-diagnostics.bat'
    if ((Test-Path -LiteralPath $ctl) -and (Test-Path -LiteralPath $diagnostics)) {
        & $diagnostics ping 1>$null 2>$null
        if ($LASTEXITCODE -eq 0) {
            & $ctl stop
            Start-Sleep -Seconds 2
        } else {
            Write-Host '[stop] RabbitMQ is not running; skip stop'
        }
    }
}

Stop-InstallRootErlangProcesses -Root $InstallRoot

Write-Host '[stop] cleanup request complete'
