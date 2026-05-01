param(
    [switch]$SkipGitConfig
)

$ErrorActionPreference = "Stop"

function Set-CurrentSessionUtf8 {
    $utf8 = New-Object System.Text.UTF8Encoding $false
    [Console]::InputEncoding = $utf8
    [Console]::OutputEncoding = $utf8
    $script:OutputEncoding = $utf8
    $global:OutputEncoding = $utf8
    & chcp.com 65001 > $null
}

function Install-ProfileUtf8Block {
    param([string]$ProfilePath)

    $profileDir = Split-Path -Parent $ProfilePath
    New-Item -ItemType Directory -Force -Path $profileDir | Out-Null

    $begin = "# >>> Module_Context UTF-8 bootstrap >>>"
    $end = "# <<< Module_Context UTF-8 bootstrap <<<"
    $block = @"
$begin
`$utf8 = New-Object System.Text.UTF8Encoding `$false
[Console]::InputEncoding = `$utf8
[Console]::OutputEncoding = `$utf8
`$OutputEncoding = `$utf8
& chcp.com 65001 > `$null
`$PSDefaultParameterValues['Out-File:Encoding'] = 'utf8'
`$PSDefaultParameterValues['Set-Content:Encoding'] = 'utf8'
`$PSDefaultParameterValues['Add-Content:Encoding'] = 'utf8'
# <<< Module_Context UTF-8 bootstrap <<<
"@

    $content = ""
    if (Test-Path $ProfilePath) {
        $content = Get-Content -Raw -Path $ProfilePath
    }

    $pattern = [regex]::Escape($begin) + "(?s).*?" + [regex]::Escape($end)
    if ($content -match $pattern) {
        $content = [regex]::Replace($content, $pattern, [System.Text.RegularExpressions.MatchEvaluator]{ param($m) $block })
    } elseif ($content.Trim().Length -gt 0) {
        $content = $content.TrimEnd() + "`r`n`r`n" + $block + "`r`n"
    } else {
        $content = $block + "`r`n"
    }

    Set-Content -Path $ProfilePath -Value $content -Encoding UTF8
}

Set-CurrentSessionUtf8

$pwshProfile = Join-Path $HOME "Documents\PowerShell\Microsoft.PowerShell_profile.ps1"
$winPsProfile = Join-Path $HOME "Documents\WindowsPowerShell\Microsoft.PowerShell_profile.ps1"
Install-ProfileUtf8Block -ProfilePath $pwshProfile
Install-ProfileUtf8Block -ProfilePath $winPsProfile

[Environment]::SetEnvironmentVariable("POWERSHELL_TELEMETRY_OPTOUT", "1", "User")
[Environment]::SetEnvironmentVariable("DOTNET_CLI_TELEMETRY_OPTOUT", "1", "User")

if (-not $SkipGitConfig) {
    $gitCmd = Get-Command git.exe -ErrorAction SilentlyContinue
    $gitPath = $null
    if ($gitCmd) {
        $gitPath = $gitCmd.Source
    } else {
        foreach ($path in @("C:\Program Files\Git\cmd\git.exe", "C:\Program Files\Git\bin\git.exe")) {
            if (Test-Path $path) {
                $gitPath = $path
                break
            }
        }
    }

    if ($gitPath) {
        & $gitPath config --global core.quotepath false
        & $gitPath config --global i18n.commitEncoding utf-8
        & $gitPath config --global i18n.logOutputEncoding utf-8
        & $gitPath config --global gui.encoding utf-8
    }
}

Write-Host "UTF-8 session/profile configuration installed."
Write-Host "中文输出测试: 模块上下文 ECS 构建环境"
