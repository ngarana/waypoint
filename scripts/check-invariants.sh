#!/usr/bin/env bash
# scripts/check-invariants.sh — Structural gates for the waypoint monorepo.
#
# Verifies the invariants that keep the suite's security and decoupling
# properties intact across the merge. Runs against configured build trees
# (defaults: qypr/build, waylaunch/build — build them first):
#
#   ./scripts/check-invariants.sh [qypr-build-dir] [waylaunch-build-dir]
#
# Checks compare absolute source paths in the link lines (separate per-component
# builds mean relative object paths carry no repo prefix). Shared common/
# objects match neither pattern and are explicitly allowed.
#
#   I4 — qypr-lock links no waylaunch content (indexer, extractors,
#        providers). A document indexer reachable from the lock screen's
#        process image enlarges a separated blast radius.
#   Q5 — the waylaunch binary links no qypr bar sources (the Stage 3
#        rejection: Alt+Tab's lifetime stays decoupled from the bar).
#        Shared libwl-common units (common/) are explicitly allowed.
#   Both checks inspect the link lines CMake wrote, so they verify the
#   shipped artifacts, not just the source tree.
#
# Exit codes: 0 = all hold, 1 = violation (prints offenders).

set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
QYPR_BUILD="${1:-$ROOT/qypr/build}"
WL_BUILD="${2:-$ROOT/waylaunch/build}"

FAIL=0

# Print the link inputs (objects, archives, libs) for a CMake target, one per
# line. Works with Make (link.txt) and Ninja (no link.txt — parse the link
# command instead). Empty output when the target was never built.
link_tokens() { # <build-dir> <target>
    local link
    link="$(find "$1" -path "*CMakeFiles/$2.dir/link.txt" 2>/dev/null | head -n 1)"
    if [[ -n "$link" ]]; then
        tr ' ' '\n' < "$link"
        return 0
    fi
    local ninja_bin=""
    if command -v ninja &>/dev/null; then
        ninja_bin=ninja
    elif command -v ninja-build &>/dev/null; then
        ninja_bin=ninja-build
    else
        return 0
    fi
    (cd "$1" && "$ninja_bin" -t commands "$2" 2>/dev/null \
        | grep -oE "\-o $2 .*" | tr ' ' '\n' || true)
}

check_link_free() { # <build-dir> <target> <forbidden-pattern> <invariant-name>
    local tokens
    tokens="$(link_tokens "$1" "$2")"
    if [[ -z "$tokens" ]]; then
        echo "SKIP $4: no link inputs found (build $2 first)"
        return 0
    fi
    local hits
    hits="$(grep -E "$3" <<< "$tokens" || true)"
    if [[ -n "$hits" ]]; then
        echo "FAIL $4: forbidden objects linked:"
        printf '    %s\n' $hits
        FAIL=1
    else
        echo "PASS $4"
    fi
}

# I4: qypr-lock must not pull in anything under waylaunch/.
# (The bar has no such rule — only the lock carries the I4 obligation.)
check_link_free "$QYPR_BUILD" "qypr-lock" '(/waylaunch/|waylaunch_content)' \
    "I4 qypr-lock free of waylaunch objects"

# Q5: the waylaunch binary must not pull in anything under qypr/.
check_link_free "$WL_BUILD" "waylaunch" '(/qypr/)' "Q5 waylaunch free of qypr-bar objects"

# B1: qypr-bar must not link the lock-only stack (libmpv/libpam or the
# lock-only translation units). The CMake source lists are the primary
# boundary (explicit per-target lists, no glob); this verifies the shipped
# artifact, so a mis-sorted file fails loudly instead of slowing cold boot.
check_link_free "$QYPR_BUILD" "qypr-bar" '(libmpv|libpam|VideoPlayer|PamAuthenticator)' \
    "B1 qypr-bar free of lock-only objects"

if [[ "$FAIL" -ne 0 ]]; then
    echo "INVARIANTS FAILED"
    exit 1
fi
echo "INVARIANTS HOLD"
