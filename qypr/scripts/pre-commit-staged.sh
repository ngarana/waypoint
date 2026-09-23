#!/usr/bin/env bash
# pre-commit-staged.sh — pre-commit entry point for the qypr staged checks.
#
# pre-commit passes the staged filenames as arguments (git-root-relative);
# forward them to scripts/lint.sh --staged via QYPR_LINT_FILES. Paths with
# spaces are not supported (the repo has none).
set -euo pipefail

QYPR_LINT_FILES="$*" "$(dirname "${BASH_SOURCE[0]}")/lint.sh" --staged
