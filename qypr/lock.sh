#!/usr/bin/env bash
# lock.sh - Backward Compatibility Wrapper
# Redirects to scripts/lock.sh

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
exec "$SCRIPT_DIR/scripts/lock.sh" "$@"
