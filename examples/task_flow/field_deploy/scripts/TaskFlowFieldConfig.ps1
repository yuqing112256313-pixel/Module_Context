function Import-TaskFlowFieldConfig {
    param([string]$PackageRoot)

    if ([string]::IsNullOrWhiteSpace($PackageRoot)) {
        $PackageRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
    }

    $configPath = Join-Path $PackageRoot 'config\TaskFlowFieldConfig.psd1'
    if (-not (Test-Path -LiteralPath $configPath -PathType Leaf)) {
        throw "Task Flow field config not found: $configPath"
    }

    return Import-PowerShellDataFile -LiteralPath $configPath
}

function Get-TaskFlowString {
    param(
        [AllowNull()][string]$Value,
        [AllowNull()][string]$Fallback
    )

    if (-not [string]::IsNullOrWhiteSpace($Value)) {
        return $Value
    }
    return $Fallback
}

function Get-TaskFlowInt {
    param(
        [int]$Value,
        [int]$Fallback
    )

    if ($Value -ne 0) {
        return $Value
    }
    return $Fallback
}

function Get-TaskFlowNonNegativeInt {
    param(
        [int]$Value,
        [int]$Fallback
    )

    if ($Value -ge 0) {
        return $Value
    }
    return $Fallback
}

function Get-TaskFlowLong {
    param(
        [long]$Value,
        [long]$Fallback
    )

    if ($Value -ne 0) {
        return $Value
    }
    return $Fallback
}

function Get-TaskFlowApiUrl {
    param(
        [string]$MasterHost,
        [int]$ManagementPort,
        [string]$ApiPath
    )

    if ([string]::IsNullOrWhiteSpace($ApiPath)) {
        $ApiPath = '/api'
    }
    if (-not $ApiPath.StartsWith('/')) {
        $ApiPath = '/' + $ApiPath
    }

    return 'http://{0}:{1}{2}' -f $MasterHost, $ManagementPort, $ApiPath
}

function Get-TaskFlowHttpEndpoint {
    param(
        [string]$MasterHost,
        [int]$HttpPort
    )

    return 'http://{0}:{1}' -f $MasterHost, $HttpPort
}

function Get-TaskFlowWorkerMap {
    param([object[]]$Nodes)

    $map = @{}
    foreach ($node in @($Nodes)) {
        if ($null -eq $node) {
            continue
        }
        $hostAddress = [string]$node.Host
        $workerId = [string]$node.Id
        if ([string]::IsNullOrWhiteSpace($hostAddress) -or
            [string]::IsNullOrWhiteSpace($workerId)) {
            continue
        }
        $map[$hostAddress] = $workerId
    }
    return $map
}
