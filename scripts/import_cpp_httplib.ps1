param(
    [string]$Version = 'v0.43.1',
    [string]$Proxy = 'http://127.0.0.1:7897',
    [string]$Destination = ''
)

$ErrorActionPreference = 'Stop'

if ([string]::IsNullOrWhiteSpace($Destination)) {
    $Destination = Join-Path (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path 'third_party\cpp-httplib'
}

New-Item -ItemType Directory -Force -Path $Destination | Out-Null

$oldHttpProxy = $env:HTTP_PROXY
$oldHttpsProxy = $env:HTTPS_PROXY
try {
    if (-not [string]::IsNullOrWhiteSpace($Proxy)) {
        $env:HTTP_PROXY = $Proxy
        $env:HTTPS_PROXY = $Proxy
    }

    $baseUrl = "https://raw.githubusercontent.com/yhirose/cpp-httplib/$Version"
    Invoke-WebRequest -Uri "$baseUrl/httplib.h" -OutFile (Join-Path $Destination 'httplib.h')
    Invoke-WebRequest -Uri "$baseUrl/LICENSE" -OutFile (Join-Path $Destination 'LICENSE')
} finally {
    $env:HTTP_PROXY = $oldHttpProxy
    $env:HTTPS_PROXY = $oldHttpsProxy
}

Write-Host "[import] cpp-httplib $Version imported to $Destination"
