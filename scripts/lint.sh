#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Run the CTRM lint checks. CI runs this script, so a clean local run means a
# clean CI lint job.
#
# Usage:
#   scripts/lint.sh                 Check all tracked files.
#   scripts/lint.sh --range A..B    Also check the commit messages in A..B.
#   scripts/lint.sh --staged        Check only the staged content (pre-commit).
#
# Checks:
#   - editorconfig-checker. clang-format owns the layout of C/C++ files, so for
#     C/C++ files the indent-size check is off (all other checks stay on).
#   - clang-format --dry-run --Werror on C/C++ files.
#   - shellcheck on the hooks and scripts (all-files mode; skipped if missing).
#   - Commit messages in a range (--range): the commit-msg hook, no merges.
#
# Tool overrides: EDITORCONFIG_CHECKER, CLANG_FORMAT, SHELLCHECK.

# Tool variables can hold a command with an argument ("xcrun clang-format").
# shellcheck disable=SC2086
set -u

# clang-format major version that CI uses. Other versions can format differently.
expected_cf_major=23
cxx_regex='\.(c|cc|cpp|cxx|h|hh|hpp|hxx|ipp|inl|mm)$'

mode=all
range=''
while [ $# -gt 0 ]; do
    case $1 in
        --staged) mode=staged ;;
        --range)
            [ $# -ge 2 ] || { echo 'lint: --range needs a value, for example main..HEAD' >&2; exit 2; }
            range=$2
            shift
            ;;
        -h | --help)
            sed -n '/^# Usage:/,/^# Tool overrides/p' "$0" | sed 's/^# \{0,1\}//'
            exit 0
            ;;
        *) echo "lint: unknown argument: $1" >&2; exit 2 ;;
    esac
    shift
done

root=$(git rev-parse --show-toplevel) || exit 2
cd "$root" || exit 2

status=0
fail() {
    printf 'lint: %s\n' "$1" >&2
    status=1
}

# Print the first tool that is available: an override, then the candidates.
find_tool() {
    override=$1
    shift
    if [ -n "$override" ]; then
        printf '%s\n' "$override"
        return 0
    fi
    for candidate in "$@"; do
        case $candidate in
            'xcrun '*)
                command -v xcrun >/dev/null 2>&1 &&
                    xcrun --find "${candidate#xcrun }" >/dev/null 2>&1 &&
                    { printf '%s\n' "$candidate"; return 0; }
                ;;
            *)
                command -v "$candidate" >/dev/null 2>&1 &&
                    { printf '%s\n' "$candidate"; return 0; }
                ;;
        esac
    done
    return 1
}

# Run a command on each line of stdin as arguments, in batches.
run_on_list() {
    tr '\n' '\0' | xargs -0 "$@"
}

# --- Select the files -------------------------------------------------------
if [ "$mode" = staged ]; then
    files=$(git diff --cached --name-only --diff-filter=ACMR)
    [ -n "$files" ] || exit 0
    # Check the staged content, not the work tree: copy the index to a temp dir.
    work=$(mktemp -d "${TMPDIR:-/tmp}/ctrm-lint.XXXXXX") || exit 2
    trap 'rm -rf "$work"' EXIT INT TERM
    { printf '%s\n' "$files"; echo .editorconfig; echo .clang-format; } | sort -u |
        run_on_list git checkout-index --prefix="$work/" -- 2>/dev/null
    cd "$work" || exit 2
else
    files=$(git ls-files)
fi

cxx_files=$(printf '%s\n' "$files" | grep -E "$cxx_regex")
other_files=$(printf '%s\n' "$files" | grep -Ev "$cxx_regex")

# --- editorconfig-checker ---------------------------------------------------
ec=$(find_tool "${EDITORCONFIG_CHECKER:-}" editorconfig-checker ec)
if [ -z "$ec" ]; then
    fail 'editorconfig-checker is necessary, but it is not installed.'
    printf '  Install: macOS: brew install editorconfig-checker\n' >&2
    printf '           Linux, Windows: pip install editorconfig-checker\n' >&2
    printf '  Or set EDITORCONFIG_CHECKER to the path of the tool.\n' >&2
else
    if [ -n "$other_files" ]; then
        printf '%s\n' "$other_files" | run_on_list $ec -no-color ||
            fail 'editorconfig-checker found problems (see above).'
    fi
    if [ -n "$cxx_files" ]; then
        printf '%s\n' "$cxx_files" | run_on_list $ec -no-color -disable-indent-size ||
            fail 'editorconfig-checker found problems in C/C++ files (see above).'
    fi
fi

# --- clang-format -----------------------------------------------------------
if [ -n "$cxx_files" ]; then
    cf=$(find_tool "${CLANG_FORMAT:-}" clang-format 'xcrun clang-format')
    if [ -z "$cf" ]; then
        fail 'clang-format is necessary to check C/C++ files, but it is not installed.'
        printf '  Install: macOS: brew install clang-format\n' >&2
        printf '           Linux, Windows: pip install clang-format==%s.*\n' "$expected_cf_major" >&2
        printf '  Or set CLANG_FORMAT to the path of the tool.\n' >&2
    else
        cf_major=$($cf --version | sed -n 's/.*version \([0-9][0-9]*\).*/\1/p' | head -n 1)
        if [ "$cf_major" != "$expected_cf_major" ]; then
            printf 'lint: warning: clang-format %s found; CI uses %s. Results can differ.\n' \
                "${cf_major:-unknown}" "$expected_cf_major" >&2
        fi
        if ! printf '%s\n' "$cxx_files" | run_on_list $cf --dry-run --Werror; then
            fail 'some C/C++ files are not formatted.'
            printf '  Fix: %s -i <file>... then run git add again.\n' "$cf" >&2
        fi
    fi
fi

# --- shellcheck (all-files mode only) ---------------------------------------
if [ "$mode" = all ]; then
    sh_files=$(printf '%s\n' "$files" | grep -E '^(\.githooks/[^/]+|scripts/.*\.sh)$')
    if [ -n "$sh_files" ]; then
        sc=$(find_tool "${SHELLCHECK:-}" shellcheck)
        if [ -z "$sc" ]; then
            echo 'lint: note: shellcheck is not installed; shell scripts are not checked.' >&2
        elif ! printf '%s\n' "$sh_files" | run_on_list $sc -s sh; then
            fail 'shellcheck found problems (see above).'
        fi
    fi
fi

# --- Commit messages --------------------------------------------------------
if [ -n "$range" ]; then
    if ! git rev-list --quiet "$range" 2>/dev/null; then
        fail "the commit range is not valid: $range"
    else
        merges=$(git rev-list --merges "$range")
        if [ -n "$merges" ]; then
            fail 'merge commits are not allowed (linear history):'
            printf '%s\n' "$merges" | sed 's/^/    /' >&2
        fi
        msg_file=$(mktemp "${TMPDIR:-/tmp}/ctrm-msg.XXXXXX") || exit 2
        for sha in $(git rev-list --no-merges --reverse "$range"); do
            git log -1 --format=%B "$sha" >"$msg_file"
            if ! sh .githooks/commit-msg "$msg_file" 2>/dev/null; then
                fail "commit $(git log -1 --format='%h %s' "$sha") has a bad message:"
                sh .githooks/commit-msg "$msg_file" 2>&1 | head -n 1 | sed 's/^/    /' >&2
            fi
        done
        rm -f "$msg_file"
    fi
fi

if [ "$status" -eq 0 ]; then
    echo "lint: all checks passed ($mode)."
fi
exit "$status"
