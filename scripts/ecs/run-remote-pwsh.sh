#!/usr/bin/env bash
set -euo pipefail

SSH_ALIAS="${SSH_ALIAS:-win-home}"
REMOTE_SCRIPT="${REMOTE_SCRIPT:-H:/Installers/module_context_remote.ps1}"

local_script=""
if [[ $# -gt 0 ]]; then
  local_script="$1"
else
  local_script="$(mktemp)"
  trap 'rm -f "${local_script}"' EXIT
  cat >"${local_script}"
fi

scp -O -p -o Compression=no "${local_script}" "${SSH_ALIAS}:${REMOTE_SCRIPT}" >/dev/null

cat <<PS1 | ssh "${SSH_ALIAS}" 'powershell.exe -NoLogo -NoProfile -ExecutionPolicy Bypass -Command -'
& 'C:\Program Files\PowerShell\7\pwsh.exe' -NoLogo -NoProfile -ExecutionPolicy Bypass -File '${REMOTE_SCRIPT}'
exit \$LASTEXITCODE
PS1
