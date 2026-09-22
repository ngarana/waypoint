#!/usr/bin/env bash
# scripts/check-doc-paths.sh — Fail on dangling source-path references in docs.
#
# Scans tracked markdown docs for backtick-quoted `<path>.<ext>` references
# and resolves each against the repo root and the doc's own directory (qypr/
# and waylaunch/ docs use component-relative paths). Known-historical
# references (phase logs, fixed bug tables) live in
# scripts/doc-paths-allow.txt, one substring per line.
#
# Exit codes: 0 = all resolve, 1 = offenders (printed).

set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
ALLOW="$ROOT/scripts/doc-paths-allow.txt"
FAIL=0

ref_paths() { # <md-file>: emit one candidate path per line
    grep -oE '`[^`]*`' "$1" | tr -d '`' | while IFS= read -r ref; do
        # Strip #L12-style anchors.
        ref="${ref%%#*}"
        # Expand "A.hpp/.cpp" shorthand into both files.
        if [[ "$ref" == *".hpp/.cpp" ]]; then
            echo "${ref%/.cpp}"
            echo "${ref%.hpp/.cpp}.cpp"
            continue
        fi
        if [[ "$ref" == *".hpp/.h" ]]; then
            echo "${ref%/.h}"
            echo "${ref%.hpp/.h}.h"
            continue
        fi
        case "$ref" in
            *"/"*.*) echo "$ref" ;;
        esac
    done
}

is_source_ref() { # <ref>: true for source-ish extensions
    case "$1" in
        *.hpp | *.cpp | *.h | *.py | *.sh | *.toml | *.sql | *.xml | *.in | \
            *.service | *.json | *.css | *.1) return 0 ;;
        *) return 1 ;;
    esac
}

allowed() { # <ref>: true when a suppression substring matches
    [[ -f "$ALLOW" ]] || return 1
    while IFS= read -r line || [[ -n "$line" ]]; do
        [[ -z "$line" || "$line" == \#* ]] && continue
        if [[ "$ref" == *"$line"* ]]; then return 0; fi
    done <"$ALLOW"
    return 1
}

while IFS= read -r doc; do
    dir="$(dirname "$doc")"
    while IFS= read -r ref; do
        # Shell snippets, home-dir paths, and globs — not repo references.
        case "$ref" in
            *[\ \$]* | *'&&'* | *'||'* | *';'* | '~'* | '/'* | *'*'*) continue ;;
        esac
        is_source_ref "$ref" || continue
        found=0
        # Layout roots: repo root, the doc's dir, its parent (docs/ pages use
        # component-relative paths), per-component src/ trees, and each
        # component root (for cross-component references).
        for base in "$ROOT" "$ROOT/$dir" "$ROOT/$dir/.." "$ROOT/$dir/src" \
                    "$ROOT/$dir/../src" "$ROOT/qypr" "$ROOT/waylaunch" "$ROOT/common"; do
            if [[ -e "$base/$ref" ]]; then found=1; break; fi
        done
        [[ "$found" -eq 1 ]] && continue
        if allowed "$ref"; then continue; fi
        echo "DANGLING $doc: $ref"
        FAIL=1
    done < <(ref_paths "$ROOT/$doc")
done < <(cd "$ROOT" && git ls-files '*.md')

if [[ "$FAIL" -ne 0 ]]; then
    echo "DOC PATHS FAILED (add intentional history to scripts/doc-paths-allow.txt)"
    exit 1
fi
echo "DOC PATHS HOLD"
