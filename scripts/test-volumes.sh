#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Run the file system tests on an exFAT volume and on a case-sensitive APFS volume (macOS
# only, no admin rights). The script makes two disk images, attaches them, runs the tests with
# TMPDIR on each volume, then detaches the images. The tests make their temporary folders in
# TMPDIR, so they use the volume.
#
# Usage:
#   scripts/test-volumes.sh [build-dir]    Default build dir: build/dev.
#
# Covers AC13.9 (weak identity on exFAT, core part) and AC12.4 (case-only rename on a
# case-sensitive volume). Exit code 0 when all tests pass on both volumes.

set -eu

build=${1:-build/dev}

if ! command -v hdiutil >/dev/null 2>&1; then
    echo "test-volumes: hdiutil not found (macOS only). Nothing tested."
    exit 0
fi
if [ ! -d "$build/tests" ]; then
    echo "test-volumes: no tests in $build. Build first." >&2
    exit 2
fi

tests="core/tst_nativefs core/tst_itemsource core/tst_safety core/tst_executor
core/tst_batchservice core/tst_undo app/tst_app_itemadder app/tst_app_acceptance"

work=$(mktemp -d "${TMPDIR:-/tmp}/ctrm-volumes.XXXXXX")
mounts=""

cleanup() {
    for m in $mounts; do
        hdiutil detach -quiet -force "$m" 2>/dev/null || true
    done
    rm -rf "$work"
}
trap cleanup EXIT INT TERM

failed=0

# run_on <label> <filesystem name for hdiutil>
run_on() {
    label=$1
    image="$work/$label.dmg"
    mount="$work/$label"
    mkdir -p "$mount"
    hdiutil create -quiet -size 256m -fs "$2" -volname "ctrm-$label" "$image"
    hdiutil attach -quiet -nobrowse -mountpoint "$mount" "$image"
    mounts="$mounts $mount"
    mkdir -p "$mount/tmp"
    echo "== $label ($2)"
    for t in $tests; do
        if [ ! -x "$build/tests/$t" ]; then
            echo "   $t: not built" >&2
            failed=1
            continue
        fi
        log="$work/$label-$(basename "$t").log"
        if TMPDIR="$mount/tmp" QT_QPA_PLATFORM=offscreen "$build/tests/$t" >"$log" 2>&1; then
            result=$(grep -E '^Totals:' "$log" || echo "Totals: ?")
            echo "   $t: $result"
        else
            echo "   $t: FAILED"
            grep -E '^(FAIL!|   Loc:|   Actual|   Expected)' "$log" | sed 's/^/      /'
            failed=1
        fi
        grep -E '^SKIP' "$log" | sed 's/^/      /' || true
    done
    hdiutil detach -quiet "$mount"
    mounts=""
}

run_on exfat ExFAT
run_on apfs-cs "Case-sensitive APFS"

exit $failed
