#!/usr/bin/env bash
# scripts/test-lint-fix.sh — Regression test for lint.sh --fix safety.
#
# Guards the three fixit hazards that once destroyed code (see FIX_DENY_CHECKS
# in scripts/lint.sh):
#   1. readability-identifier-naming renames (`#define private public` →
#      `#define PRIVATE public`; cross-file mock renames → link break).
#   2. modernize-use-ranges erase(remove_if(...), end()) miscompilation.
#   3. misc-use-internal-linkage moves breaking other users.
# plus a positive control: a safe fixit (braces) must still apply.
#
# Hermetic: builds the fixture in a mktemp dir with its own compile DB and
# .clang-tidy, drives scripts/lint.sh end-to-end, cleans up on exit.
# Needs clang-tidy on PATH (same prerequisite as lint.sh itself).

set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
LINT="$ROOT/scripts/lint.sh"

if ! command -v clang-tidy &>/dev/null; then
    echo "test-lint-fix: SKIP (clang-tidy not on PATH)"
    exit 0
fi

WORK="$(mktemp -d "${TMPDIR:-/tmp}/qypr-lint-fixture.XXXXXX")"
trap 'rm -rf "$WORK"' EXIT

cat > "$WORK/fix.cpp" <<'EOF'
#include <vector>
#include <algorithm>
#define private public
struct C {
    void go(std::vector<int>& v) {
        if (v.empty()) return;
        v.erase(std::remove_if(v.begin(), v.end(), [](int i) { return i > 0; }), v.end());
    }
};
int BadName = 1;
int* nullable() { return 0; }
int main() { C c; return BadName; }
EOF

cat > "$WORK/compile_commands.json" <<EOF
[{"directory":"$WORK","command":"c++ -std=c++20 -c fix.cpp","file":"fix.cpp"}]
EOF

cat > "$WORK/.clang-tidy" <<'EOF'
Checks: 'readability-identifier-naming,modernize-use-ranges,misc-use-internal-linkage,modernize-use-nullptr'
CheckOptions:
  - key: readability-identifier-naming.MacroDefinitionCase
    value: UPPER_CASE
  - key: readability-identifier-naming.VariableCase
    value: camelBack
EOF

cp "$WORK/fix.cpp" "$WORK/fix.orig.cpp"
export QYPR_LINT_FILES="$WORK/fix.cpp"
export QYPR_LINT_DB="$WORK"

fail() { echo "test-lint-fix: FAIL — $*"; exit 1; }

# 1. Check mode must REPORT the hazards (guards against a vacuous test: if
#    nothing is reported, "untouched" below would prove nothing).
report="$("$LINT" --staged --tidy-only 2>&1 || true)"
for check in readability-identifier-naming modernize-use-ranges misc-use-internal-linkage; do
    grep -qF "$check" <<< "$report" || fail "check mode did not report $check (fixture insensitive?)"
done
echo "test-lint-fix: check mode reports all three hazard families"

# 2. Fix mode must leave every hazard pattern byte-identical. (Exit code is
#    intentionally unchecked: remaining check-mode violations still fail the
#    gate by design — the assertions below are the verdict.)
"$LINT" --staged --fix >/dev/null 2>&1 || true
grep -qF "#define private public" "$WORK/fix.cpp" || fail "macro hack was renamed"
grep -qF "std::remove_if(v.begin(), v.end()" "$WORK/fix.cpp" || fail "erase-remove was rewritten"
grep -qF "int BadName = 1;" "$WORK/fix.cpp" || fail "global was renamed/moved"
echo "test-lint-fix: hazard patterns untouched by --fix"

# 3. ...while a safe fixit still applies (the deny-list must not neuter --fix).
# (Matched loosely: clang-format runs before tidy in --fix mode and may
# reflow the line around the rewritten token.)
grep -qF "return nullptr;" "$WORK/fix.cpp" \
    || fail "safe nullptr fixit was not applied"
echo "test-lint-fix: safe fixit (nullptr) still applied"

echo "test-lint-fix: PASS"
