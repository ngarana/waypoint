#!/usr/bin/env bash
# run.sh - Backward Compatibility Wrapper
# Redirects to scripts/run.sh

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
exec "$SCRIPT_DIR/scripts/run.sh" "$@"
