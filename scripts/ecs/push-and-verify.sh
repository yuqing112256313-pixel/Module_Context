#!/usr/bin/env bash
set -euo pipefail

SSH_ALIAS="${SSH_ALIAS:-aliyun-win}"
REPO_URL="${REPO_URL:-https://github.com/yuqing112256313-pixel/Module_Context.git}"
REMOTE_WORKDIR="${REMOTE_WORKDIR:-C:\\work\\Module_Context}"
PRESET="${PRESET:-windows-vs2015-x64-debug}"
BRANCH="${BRANCH:-$(git branch --show-current)}"

if [[ -z "${BRANCH}" ]]; then
  echo "Could not determine the current Git branch. Set BRANCH explicitly." >&2
  exit 1
fi

git push -u origin "${BRANCH}"

remote_script="$(mktemp)"
trap 'rm -f "${remote_script}"' EXIT

cat >"${remote_script}" <<PS1
\$ErrorActionPreference = "Stop"
\$repoUrl = "${REPO_URL}"
\$workDir = "${REMOTE_WORKDIR}"
\$branch = "${BRANCH}"
\$preset = "${PRESET}"

function Find-Git {
    \$cmd = Get-Command git.exe -ErrorAction SilentlyContinue
    if (\$cmd) { return \$cmd.Source }
    foreach (\$path in @("C:\\Program Files\\Git\\cmd\\git.exe", "C:\\Program Files\\Git\\bin\\git.exe")) {
        if (Test-Path \$path) { return \$path }
    }
    throw "git.exe was not found. Install Git for Windows first."
}

\$git = Find-Git
\$parent = Split-Path -Parent \$workDir
New-Item -ItemType Directory -Force -Path \$parent | Out-Null

if (Test-Path (Join-Path \$workDir ".git")) {
    & \$git -C \$workDir fetch origin
    & \$git -C \$workDir checkout \$branch
    & \$git -C \$workDir pull --ff-only origin \$branch
} else {
    if (Test-Path \$workDir) {
        throw "\$workDir exists but is not a Git checkout."
    }
    & \$git clone --branch \$branch \$repoUrl \$workDir
}

\$syncScript = Join-Path \$workDir "scripts\\ecs\\Sync-BuildTestFromGit.ps1"
& powershell -NoProfile -ExecutionPolicy Bypass -File \$syncScript -RepoUrl \$repoUrl -WorkDir \$workDir -Branch \$branch -Preset \$preset
exit \$LASTEXITCODE
PS1

ssh "${SSH_ALIAS}" 'powershell -NoProfile -ExecutionPolicy Bypass -Command -' <"${remote_script}"
