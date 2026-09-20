#!/usr/bin/env bash
# lock.sh - Lock the session with qypr-lock.
#
# Builds on first use, then runs the binary. Running it locks the session;
# successful PAM authentication unlocks and the process exits.
#
# Usage:
#   ./lock.sh

set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BIN="$ROOT/build/qypr-lock"

[ -x "$BIN" ] || "$ROOT/scripts/build.sh"

exec "$BIN" "$@"
