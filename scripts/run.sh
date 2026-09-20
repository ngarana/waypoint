#!/usr/bin/env bash
# run.sh - Development preview.
#
# Renders the lock screen to PNG WITHOUT locking the session, so you can
# iterate on the look safely. Writes <out> and <out>-idle.png.
#
# Usage:
#   ./run.sh [out.png]

set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
OUT="${1:-/tmp/qypr-preview.png}"

"$ROOT/scripts/build.sh" >/dev/null
"$ROOT/build/qypr-lock" --preview "$OUT"

echo "Preview written: $OUT  (idle: ${OUT%.png}-idle.png)"
command -v xdg-open >/dev/null 2>&1 && xdg-open "$OUT" >/dev/null 2>&1 || true
