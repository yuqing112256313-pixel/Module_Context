#!/usr/bin/env bash
set -euo pipefail

SSH_ALIAS="${SSH_ALIAS:-win-home}"
REPO_URL="${REPO_URL:-https://github.com/yuqing112256313-pixel/Module_Context.git}"
REMOTE_WORKDIR="${REMOTE_WORKDIR:-H:\\Codex\\Module_Context}"
PRESET="${PRESET:-windows-vs2015-x64-debug}"
BRANCH="${BRANCH:-$(git branch --show-current)}"
REMOTE_GIT_HTTP_PROXY="${REMOTE_GIT_HTTP_PROXY:-}"

if [[ -z "${BRANCH}" ]]; then
  echo "Could not determine the current Git branch. Set BRANCH explicitly." >&2
  exit 1
fi

git push -u origin "${BRANCH}"

remote_script="$(mktemp)"
remote_launcher="$(mktemp)"
trap 'rm -f "${remote_script}" "${remote_launcher}"' EXIT

cat >"${remote_script}" <<PS1
\$ErrorActionPreference = "Stop"
\$utf8 = New-Object System.Text.UTF8Encoding \$false
[Console]::InputEncoding = \$utf8
[Console]::OutputEncoding = \$utf8
\$OutputEncoding = \$utf8
& chcp.com 65001 > \$null

\$repoUrl = "${REPO_URL}"
\$workDir = "${REMOTE_WORKDIR}"
\$branch = "${BRANCH}"
\$preset = "${PRESET}"
\$gitHttpProxy = "${REMOTE_GIT_HTTP_PROXY}"

function Find-Git {
    \$cmd = Get-Command git.exe -ErrorAction SilentlyContinue
    if (\$cmd) { return \$cmd.Source }
    foreach (\$path in @(
        "C:\\Program Files\\Git\\cmd\\git.exe",
        "C:\\Program Files\\Git\\bin\\git.exe",
        "E:\\Program Files\\Git\\cmd\\git.exe",
        "E:\\Program Files\\Git\\bin\\git.exe"
    )) {
        if (Test-Path \$path) { return \$path }
    }
    throw "git.exe was not found. Install Git for Windows first."
}

function Test-TcpPort {
    param(
        [string]\$HostName,
        [int]\$Port,
        [int]\$TimeoutMs = 500
    )

    \$client = New-Object System.Net.Sockets.TcpClient
    try {
        \$async = \$client.BeginConnect(\$HostName, \$Port, \$null, \$null)
        if (-not \$async.AsyncWaitHandle.WaitOne(\$TimeoutMs)) {
            return \$false
        }
        \$client.EndConnect(\$async)
        return \$client.Connected
    } catch {
        return \$false
    } finally {
        \$client.Close()
    }
}

function Get-GitProxyArguments {
    \$proxy = \$gitHttpProxy
    if ([string]::IsNullOrWhiteSpace(\$proxy)) {
        \$proxy = [Environment]::GetEnvironmentVariable("REMOTE_GIT_HTTP_PROXY")
    }
    if ([string]::IsNullOrWhiteSpace(\$proxy) -and
        (Test-TcpPort -HostName "127.0.0.1" -Port 7897 -TimeoutMs 500)) {
        \$proxy = "http://127.0.0.1:7897"
    }
    if ([string]::IsNullOrWhiteSpace(\$proxy)) {
        return @()
    }

    Write-Host "Using Git HTTP proxy: \$proxy"
    return @("-c", "http.proxy=\$proxy", "-c", "https.proxy=\$proxy")
}

function Invoke-Checked {
    param(
        [string]\$FilePath,
        [string[]]\$Arguments
    )

    & \$FilePath @Arguments
    if (\$LASTEXITCODE -ne 0) {
        throw "\$FilePath failed with exit code \$LASTEXITCODE."
    }
}

\$git = Find-Git
\$gitProxyArgs = Get-GitProxyArguments
\$parent = Split-Path -Parent \$workDir
New-Item -ItemType Directory -Force -Path \$parent | Out-Null

if (Test-Path (Join-Path \$workDir ".git")) {
    Invoke-Checked \$git (\$gitProxyArgs + @("-C", \$workDir, "fetch", "origin"))
    Invoke-Checked \$git (\$gitProxyArgs + @("-C", \$workDir, "checkout", \$branch))
    Invoke-Checked \$git (\$gitProxyArgs + @("-C", \$workDir, "pull", "--ff-only", "origin", \$branch))
} else {
    if (Test-Path \$workDir) {
        throw "\$workDir exists but is not a Git checkout."
    }
    Invoke-Checked \$git (\$gitProxyArgs + @("clone", "--branch", \$branch, \$repoUrl, \$workDir))
}

\$syncScript = Join-Path \$workDir "scripts\\ecs\\Sync-BuildTestFromGit.ps1"
\$pwsh = Get-Command pwsh.exe -ErrorAction SilentlyContinue
if (-not \$pwsh -and (Test-Path "C:\\Program Files\\PowerShell\\7\\pwsh.exe")) {
    \$psExe = "C:\\Program Files\\PowerShell\\7\\pwsh.exe"
} elseif (\$pwsh) {
    \$psExe = \$pwsh.Source
} else {
    \$psExe = "powershell.exe"
}

& \$psExe -NoLogo -NoProfile -ExecutionPolicy Bypass -File \$syncScript -RepoUrl \$repoUrl -WorkDir \$workDir -Branch \$branch -Preset \$preset -GitHttpProxy \$gitHttpProxy
exit \$LASTEXITCODE
PS1

cat >"${remote_launcher}" <<'CMD'
@echo off
chcp 65001 >NUL
if exist "C:\Program Files\PowerShell\7\pwsh.exe" (
  "C:\Program Files\PowerShell\7\pwsh.exe" -NoLogo -NoProfile -ExecutionPolicy Bypass -File "C:\Windows\Temp\module_context_push_verify.ps1"
) else (
  powershell.exe -NoLogo -NoProfile -ExecutionPolicy Bypass -File "C:\Windows\Temp\module_context_push_verify.ps1"
)
exit /b %ERRORLEVEL%
CMD

scp "${remote_script}" "${SSH_ALIAS}:C:/Windows/Temp/module_context_push_verify.ps1" >/dev/null
scp "${remote_launcher}" "${SSH_ALIAS}:C:/Windows/Temp/module_context_push_verify.cmd" >/dev/null
ssh "${SSH_ALIAS}" 'cmd /d /c C:\Windows\Temp\module_context_push_verify.cmd'
