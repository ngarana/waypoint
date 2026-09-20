#!/usr/bin/env bash
# test.sh - Build + smoke test for qypr-lock.
#
# Verifies the project builds and the binary can render preview frames
# (exercises the full cairo/pango widget pipeline without locking).

set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
PASS=0
FAIL=0

check() {  # check <description> <test-command...>
    local desc="$1"; shift
    if "$@" >/dev/null 2>&1; then
        echo "PASS: $desc"; PASS=$((PASS + 1))
    else
        echo "FAIL: $desc"; FAIL=$((FAIL + 1))
    fi
}

echo "=== Build ==="
"$ROOT/scripts/build.sh"

echo ""
echo "=== Smoke Tests ==="
BIN="$ROOT/build/qypr-lock"
check "binary exists" test -x "$BIN"

TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT
"$BIN" --preview "$TMP/frame.png" >/dev/null 2>&1 || true
check "preview (revealed) rendered" test -s "$TMP/frame.png"
check "preview (idle) rendered" test -s "$TMP/frame-idle.png"

echo ""
echo "========================================"
echo " Passed: $PASS   Failed: $FAIL"
echo "========================================"
[ "$FAIL" -eq 0 ] || { echo "Some tests failed."; exit 1; }
echo "All tests passed!"
