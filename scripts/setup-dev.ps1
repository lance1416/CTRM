# SPDX-License-Identifier: GPL-3.0-or-later
#
# Set up a CTRM clone for development on Windows (PowerShell).
# Same settings as scripts/setup-dev.sh. Git for Windows runs the POSIX sh hooks.

$ErrorActionPreference = 'Stop'

$root = git rev-parse --show-toplevel
if ($LASTEXITCODE -ne 0) { throw 'Run this script inside the CTRM git repository.' }
Set-Location $root

git config core.hooksPath .githooks
git config pull.rebase true
git config merge.ff only
git config rebase.autoStash true

Write-Host 'CTRM development setup is complete:'
foreach ($key in 'core.hooksPath', 'pull.rebase', 'merge.ff', 'rebase.autoStash') {
    Write-Host ("  {0,-17} = {1}" -f $key, (git config $key))
}

foreach ($tool in 'cmake', 'ninja', 'clang-format') {
    if (-not (Get-Command $tool -ErrorAction SilentlyContinue)) {
        Write-Warning "$tool is not on PATH. See the Development section in README.md."
    }
}
