#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Set up a CTRM clone for development:
#   - use the tracked git hooks in .githooks/
#   - keep the history linear (rebase on pull, fast-forward merges only)
# Safe to run more than one time.

set -eu

root=$(git rev-parse --show-toplevel)
cd "$root"

chmod +x .githooks/* 2>/dev/null || true

git config core.hooksPath .githooks
git config pull.rebase true
git config merge.ff only
git config rebase.autoStash true

echo "CTRM development setup is complete:"
echo "  core.hooksPath    = $(git config core.hooksPath)"
echo "  pull.rebase       = $(git config pull.rebase)"
echo "  merge.ff          = $(git config merge.ff)"
echo "  rebase.autoStash  = $(git config rebase.autoStash)"

for tool in cmake ninja clang-format; do
    if ! command -v "$tool" >/dev/null 2>&1; then
        echo "Note: $tool is not on PATH. See the Development section in README.md." >&2
    fi
done
