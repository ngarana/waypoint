#!/usr/bin/env bash
# scripts/lint.sh — Run clang-format and clang-tidy checks for qypr.
#
# Modes:
#   ./scripts/lint.sh                 # check whole src/ + tests/ tree
#   ./scripts/lint.sh --fix           # auto-format + apply safe tidy fixits
#   ./scripts/lint.sh --format-only   # only clang-format check
#   ./scripts/lint.sh --tidy-only     # only clang-tidy
#   ./scripts/lint.sh --staged        # check files listed in $QYPR_LINT_FILES
#                                     # (called by the pre-commit hook)
#   ./scripts/lint.sh -j8             # tidy analysis with 8 parallel jobs
#                                     # (default: nproc)
#
# Check mode enforces the strict gate: clang-tidy runs with
# --warnings-as-errors='*' (AGENT.md rule 2), so any warning fails the run.
# Without this the tree silently re-accumulates findings while the gate
# stays green.
#
# The pre-commit hook calls:
#   QYPR_LINT_FILES="file1.cpp file2.hpp …" ./scripts/lint.sh --staged
#
# --fix auto-applies safe fixits only: checks with known-unsafe rewrites
# (identifier renames, ranges erase-remove, internal-linkage moves) are
# excluded from fixing but still reported — see FIX_DENY_CHECKS below and
# scripts/test-lint-fix.sh.
#
# Exit codes:  0 = clean,  1 = violations or missing tool
#
# Prerequisites:
#   clang-format and clang-tidy must be on PATH.
#   clang-tidy also needs compile_commands.json.  The script looks for it at
#   <root>/compile_commands.json or <root>/build/compile_commands.json and
#   auto-generates it into ./build if neither exists.

set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
FIX=0
FORMAT_ONLY=0
TIDY_ONLY=0
STAGED=0
JOBS="${NPROC:-$(nproc 2>/dev/null || echo 4)}"

# ---- argument parsing -------------------------------------------------------
for arg in "$@"; do
    case "$arg" in
        --fix)          FIX=1 ;;
        --format-only)  FORMAT_ONLY=1 ;;
        --tidy-only)    TIDY_ONLY=1 ;;
        --staged)       STAGED=1 ;;
        -j*)            JOBS="${arg#-j}" ;;
        --jobs=*)       JOBS="${arg#--jobs=}" ;;
        -h|--help)
            sed -n '2,22p' "$0" | sed 's/^# \?//'
            exit 0 ;;
        *)
            echo "Unknown option: $arg" >&2; exit 1 ;;
    esac
done

if ! [[ "$JOBS" =~ ^[0-9]+$ ]] || [[ "$JOBS" -lt 1 ]]; then
    echo "Invalid jobs count: $JOBS (use -jN with N >= 1)" >&2; exit 1
fi

# ---- colours ----------------------------------------------------------------
if [[ -t 1 || -t 2 ]]; then
    RED='\033[0;31m'; YELLOW='\033[0;33m'; GREEN='\033[0;32m'; RESET='\033[0m'
else
    RED=''; YELLOW=''; GREEN=''; RESET=''
fi
red()    { printf "${RED}%s${RESET}\n"    "$*"; }
yellow() { printf "${YELLOW}%s${RESET}\n" "$*"; }
green()  { printf "${GREEN}%s${RESET}\n"  "$*"; }
header() { printf "\n${YELLOW}=== %s ===${RESET}\n" "$*"; }

PASS=0; FAIL=0

# ---- collect files ----------------------------------------------------------
if [[ "$STAGED" -eq 1 ]]; then
    # File list injected by the pre-commit hook via environment variable
    read -ra ALL_FILES <<< "${QYPR_LINT_FILES:-}"
    if [[ ${#ALL_FILES[@]} -eq 0 ]]; then
        green "lint: no staged C++ files to check."
        exit 0
    fi
    MODE_LABEL="staged (${#ALL_FILES[@]} file(s))"
else
    # Whole-tree scan
    mapfile -t ALL_FILES < <(
        find "$ROOT/src" "$ROOT/tests" \
            \( -name '*.cpp' -o -name '*.hpp' \) \
            -not -path '*/wayland-generated/*' \
            -not -path '*/build*/*' \
            | sort
    )
    MODE_LABEL="tree (${#ALL_FILES[@]} file(s))"
fi

echo "qypr lint  |  mode=$MODE_LABEL  |  jobs=$JOBS  |  fix=$FIX  |  strict=$((1 - FIX))"

# ---- 1. clang-format --------------------------------------------------------
if [[ "$TIDY_ONLY" -eq 0 ]]; then
    header "clang-format"

    if ! command -v clang-format &>/dev/null; then
        red "ERROR: clang-format not found."
        exit 1
    fi

    FORMAT_FAIL=()
    for f in "${ALL_FILES[@]}"; do
        if [[ "$FIX" -eq 1 ]]; then
            clang-format -i "$f"
        else
            if ! clang-format --dry-run --Werror "$f" 2>/dev/null; then
                FORMAT_FAIL+=("$f")
            fi
        fi
    done

    if [[ "$FIX" -eq 1 ]]; then
        green "✓ clang-format: reformatted in-place"
        PASS=$((PASS + 1))
    elif [[ ${#FORMAT_FAIL[@]} -eq 0 ]]; then
        green "✓ clang-format: all files properly formatted"
        PASS=$((PASS + 1))
    else
        red "✗ clang-format: ${#FORMAT_FAIL[@]} file(s) need reformatting:"
        for f in "${FORMAT_FAIL[@]}"; do printf '    %s\n' "${f#"$ROOT/"}"; done
        # Show what the pinned toolchain wants (first 40 diff lines per
        # file) — indispensable when local and CI formatters disagree.
        for f in "${FORMAT_FAIL[@]}"; do
            printf '%s\n' "--- diff ${f#"$ROOT/"} ---"
            diff -u "$f" <(clang-format "$f") | head -n 40 || true
        done
        yellow "  Fix with:  $0 --fix"
        FAIL=$((FAIL + 1))
    fi
fi

# ---- 2. clang-tidy ----------------------------------------------------------
if [[ "$FORMAT_ONLY" -eq 0 ]]; then
    header "clang-tidy"

    if ! command -v clang-tidy &>/dev/null; then
        red "ERROR: clang-tidy not found."
        exit 1
    fi

    # Locate or auto-generate compile_commands.json. QYPR_LINT_DB overrides
    # the search (used by scripts/test-lint-fix.sh for a hermetic fixture).
    DB=""
    if [[ -n "${QYPR_LINT_DB:-}" && -f "$QYPR_LINT_DB/compile_commands.json" ]]; then
        DB="$QYPR_LINT_DB"
    fi
    for candidate in "$ROOT/compile_commands.json" "$ROOT/build/compile_commands.json"; do
        if [[ -z "$DB" && -f "$candidate" ]]; then
            DB="$(dirname "$(realpath "$candidate")")"
            break
        fi
    done

    if [[ -z "$DB" ]]; then
        yellow "compile_commands.json not found – generating into ./build …"
        cmake -S "$ROOT" -B "$ROOT/build" -G Ninja \
            -DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
            -DCMAKE_BUILD_TYPE=Debug \
            -DCMAKE_CXX_COMPILER=clang++ \
            >/dev/null
        DB="$ROOT/build"
        green "Generated $DB/compile_commands.json"
    fi

    # clang-tidy processes .cpp translation units; headers are analysed through them.
    CPP_ONLY=()
    for f in "${ALL_FILES[@]}"; do
        [[ "$f" == *.cpp ]] || continue
        # Skip files not yet in the compile DB (newly created, not yet built).
        # clang-tidy prints its --help page for unknown files, which is noise.
        if ! grep -qF "$(basename "$f")" "$DB/compile_commands.json" 2>/dev/null; then
            yellow "  ⚠ skipping $(basename "$f") (not in compile DB — rebuild first)"
            continue
        fi
        CPP_ONLY+=("$f")
    done

    if [[ ${#CPP_ONLY[@]} -eq 0 ]]; then
        # Distinguish "no translation units in scope" (headers-only change:
        # nothing to analyse) from "every TU skipped: stale compile DB".
        # The latter must fail loudly — passing with zero coverage is how
        # findings re-accumulate behind a green gate.
        hadCpp=0
        for f in "${ALL_FILES[@]}"; do
            [[ "$f" == *.cpp ]] && { hadCpp=1; break; }
        done
        if [[ "$hadCpp" -eq 1 ]]; then
            red "✗ clang-tidy: every .cpp file skipped (not in compile DB — rebuild first)"
            FAIL=$((FAIL + 1))
        else
            yellow "  ⚠ no .cpp files to analyse (headers-only change)"
            PASS=$((PASS + 1))
        fi
    else
        TIDY_EXTRA=()
        # --fix only (never --fix-errors): clang-tidy must refuse to rewrite a
        # translation unit that does not compile. --fix-errors applies fixits on
        # top of a broken AST, which produces garbage edits (bogus `static`,
        # corrupted literals) — the exact failure that motivated this guard.
        #
        # Fixit deny-list (also --fix only): these checks REPORT normally, but
        # their fixits are never auto-applied — each has destroyed code before:
        # - readability-identifier-naming: renames cross-file entities (a decl
        #   renamed through one TU while its definition in another TU is out of
        #   the run → link break); mangles case-hack macros (`#define private
        #   public` → `#define PRIVATE public` via MacroDefinitionCase).
        # - modernize-use-ranges: rewrites erase(remove_if(...), end()) into
        #   erase(ranges::remove_if(...), end()), which does not compile
        #   (ranges::remove_if returns a subrange, not an iterator pair).
        # - misc-use-internal-linkage: moving a TU-external function into an
        #   anonymous namespace breaks its other users (link break).
        # CLI --checks globs append to the .clang-tidy list (verified: a bare
        # negative glob disables exactly that check), so check mode is
        # unaffected — the violations are still reported, just never rewritten.
        # See scripts/test-lint-fix.sh for the regression test.
        FIX_DENY_CHECKS="-readability-identifier-naming,-modernize-use-ranges,-misc-use-internal-linkage"
        if [[ "$FIX" -eq 1 ]]; then
            TIDY_EXTRA+=(--fix "--checks=$FIX_DENY_CHECKS")
        else
            # Check mode enforces the strict gate: every tidy warning fails
            # the run (AGENT.md rule 2). This is what keeps the tree from
            # re-accumulating findings while the gate stays green.
            TIDY_EXTRA+=(--warnings-as-errors='*')
        fi

        # Parallel analysis, throttled to $JOBS. One clang-tidy process per
        # translation unit; failing files and their logs are collected and
        # reported after all jobs finish.
        TIDY_TMP="$(mktemp -d)"
        for f in "${CPP_ONLY[@]}"; do
            (
                if ! out=$(clang-tidy -p "$DB" --quiet "${TIDY_EXTRA[@]}" "$f" 2>&1); then
                    safe="${f//\//_}"
                    printf '%s\n' "$f" > "$TIDY_TMP/$safe.fail"
                    printf '%s\n' "$out" > "$TIDY_TMP/$safe.log"
                fi
            ) &
            while [[ $(jobs -r | wc -l) -ge $JOBS ]]; do wait -n || true; done
        done
        wait

        if compgen -G "$TIDY_TMP/*.fail" > /dev/null; then
            red "✗ clang-tidy: errors in $(ls "$TIDY_TMP"/*.fail | wc -l) translation unit(s):"
            for fail in "$TIDY_TMP"/*.fail; do printf '    %s\n' "$(cat "$fail")"; done
            for log in "$TIDY_TMP"/*.log; do cat "$log"; done
            if [[ "$FIX" -eq 0 ]]; then
                yellow "  Auto-fix safe issues with:  $0 --fix"
            fi
            FAIL=$((FAIL + 1))
        else
            green "✓ clang-tidy: no errors in ${#CPP_ONLY[@]} translation unit(s)"
            PASS=$((PASS + 1))
        fi
        rm -rf "$TIDY_TMP"
    fi
fi

# ---- summary ----------------------------------------------------------------
echo ""
echo "────────────────────────────────────────"
if [[ "$FAIL" -gt 0 ]]; then
    red "FAIL  $PASS passed / $FAIL failed"
    exit 1
fi
green "PASS  $PASS check(s) passed, 0 failed"
exit 0
