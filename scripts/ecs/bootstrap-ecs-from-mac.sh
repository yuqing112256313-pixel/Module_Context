#!/usr/bin/env bash
set -euo pipefail

SSH_ALIAS="${SSH_ALIAS:-aliyun-win}"
REPO_URL="${REPO_URL:-https://github.com/yuqing112256313-pixel/Module_Context.git}"
REMOTE_WORKDIR="${REMOTE_WORKDIR:-C:\\work\\Module_Context}"
REMOTE_INSTALLERS="${REMOTE_INSTALLERS:-C:\\Installers}"
CACHE_DIR="${CACHE_DIR:-/tmp/module-context-ecs-tools}"

mkdir -p "${CACHE_DIR}"

curl_args=(-L --fail --connect-timeout 20 --max-time 600)

if [[ -n "${LOCAL_HTTP_PROXY:-}" ]]; then
  curl_args+=(--proxy "${LOCAL_HTTP_PROXY}")
elif [[ -n "${HTTPS_PROXY:-}" || -n "${HTTP_PROXY:-}" ]]; then
  :
elif command -v networksetup >/dev/null 2>&1; then
  proxy_info="$(networksetup -getwebproxy Wi-Fi 2>/dev/null || true)"
  if grep -q '^Enabled: Yes' <<<"${proxy_info}"; then
    proxy_host="$(awk -F': ' '/^Server:/ {print $2}' <<<"${proxy_info}")"
    proxy_port="$(awk -F': ' '/^Port:/ {print $2}' <<<"${proxy_info}")"
    if [[ -n "${proxy_host}" && -n "${proxy_port}" ]]; then
      curl_args+=(--proxy "http://${proxy_host}:${proxy_port}")
    fi
  fi
fi

download_latest_asset() {
  local repo="$1"
  local pattern="$2"
  local api="https://api.github.com/repos/${repo}/releases/latest"
  local metadata asset_name asset_url

  metadata="$(curl "${curl_args[@]}" -s "${api}")"
  read -r asset_name asset_url < <(
    RELEASE_METADATA="${metadata}" python3 - "$pattern" <<'PY'
import json
import os
import re
import sys

pattern = re.compile(sys.argv[1])
data = json.loads(os.environ["RELEASE_METADATA"])
for asset in data.get("assets", []):
    if pattern.match(asset["name"]):
        print(asset["name"], asset["browser_download_url"])
        break
else:
    raise SystemExit(f"no asset matched {pattern.pattern}")
PY
  )

  local output="${CACHE_DIR}/${asset_name}"
  if [[ ! -s "${output}" ]]; then
    echo "Downloading ${asset_name} via Mac network..." >&2
    curl "${curl_args[@]}" -o "${output}" "${asset_url}"
  fi
  echo "${output}"
}

remote_ps() {
  local script_file="$1"
  scp "${script_file}" "${SSH_ALIAS}:C:/Windows/Temp/module_context_bootstrap.ps1" >/dev/null
  ssh "${SSH_ALIAS}" 'cmd /d /c "chcp 65001 >NUL && powershell.exe -NoLogo -NoProfile -ExecutionPolicy Bypass -File C:\Windows\Temp\module_context_bootstrap.ps1"'
}

echo "Ensuring remote installer directory..."
tmp_script="$(mktemp)"
trap 'rm -f "${tmp_script}"' EXIT
cat >"${tmp_script}" <<PS1
New-Item -ItemType Directory -Force -Path "${REMOTE_INSTALLERS}" | Out-Null
Write-Host "Remote installer directory ready: ${REMOTE_INSTALLERS}"
PS1
remote_ps "${tmp_script}"

echo "Installing/updating PowerShell 7..."
pwsh_msi="$(download_latest_asset PowerShell/PowerShell '^PowerShell-.*-win-x64\.msi$')"
scp "${pwsh_msi}" "${SSH_ALIAS}:C:/Installers/$(basename "${pwsh_msi}")" >/dev/null
cat >"${tmp_script}" <<PS1
\$ErrorActionPreference = "Stop"
\$installer = "C:\\Installers\\$(basename "${pwsh_msi}")"
\$pwsh = "C:\\Program Files\\PowerShell\\7\\pwsh.exe"
if (-not (Test-Path \$pwsh)) {
    \$process = Start-Process -FilePath "msiexec.exe" -ArgumentList "/i", \$installer, "/qn", "ADD_PATH=1", "DISABLE_TELEMETRY=1", "/norestart" -Wait -PassThru
    if (\$process.ExitCode -ne 0) { throw "PowerShell installer failed with exit code \$($process.ExitCode)." }
}
& \$pwsh --version
PS1
remote_ps "${tmp_script}"

echo "Ensuring Git and CMake are visible on PATH..."
cat >"${tmp_script}" <<'PS1'
$ErrorActionPreference = "Stop"
$git = "C:\Program Files\Git\cmd\git.exe"
$cmake = "C:\Program Files\CMake\bin\cmake.exe"
if (-not (Test-Path $git)) { throw "Git is missing. Run scripts\ecs\Install-GitForWindows.ps1 or install it from Mac cache first." }
if (-not (Test-Path $cmake)) { throw "CMake is missing. Run scripts\ecs\Install-CMakeForWindows.ps1 or install it from Mac cache first." }
& $git --version
& $cmake --version | Select-Object -First 1
PS1
remote_ps "${tmp_script}"

echo "Ensuring remote checkout exists and is current..."
cat >"${tmp_script}" <<PS1
\$ErrorActionPreference = "Stop"
\$git = "C:\\Program Files\\Git\\cmd\\git.exe"
\$repoUrl = "${REPO_URL}"
\$workDir = "${REMOTE_WORKDIR}"
\$parent = Split-Path -Parent \$workDir
New-Item -ItemType Directory -Force -Path \$parent | Out-Null
if (Test-Path (Join-Path \$workDir ".git")) {
    & \$git -C \$workDir fetch origin
    & \$git -C \$workDir checkout main
    & \$git -C \$workDir pull --ff-only origin main
} else {
    if (Test-Path \$workDir) { throw "\$workDir exists but is not a Git checkout." }
    & \$git clone --branch main \$repoUrl \$workDir
}
PS1
remote_ps "${tmp_script}"

echo "Installing UTF-8 profile and Git encoding configuration..."
cat >"${tmp_script}" <<PS1
\$ErrorActionPreference = "Stop"
\$scriptPath = Join-Path "${REMOTE_WORKDIR}" "scripts\\ecs\\Initialize-EcsEncoding.ps1"
& "C:\\Program Files\\PowerShell\\7\\pwsh.exe" -NoLogo -NoProfile -ExecutionPolicy Bypass -File \$scriptPath
PS1
remote_ps "${tmp_script}"

echo "ECS bootstrap complete."
