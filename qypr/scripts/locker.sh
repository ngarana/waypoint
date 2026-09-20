#!/usr/bin/env bash
# locker.sh - Standalone locker launcher (kept for backward compatibility).
#
# Identical to lock.sh: builds if needed, then locks the session.
#
# Usage:
#   ./locker.sh

set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BIN="$ROOT/build/qypr-lock"

[ -x "$BIN" ] || "$ROOT/scripts/build.sh"

exec "$BIN" "$@"
