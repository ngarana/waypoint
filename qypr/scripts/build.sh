#!/usr/bin/env bash
# build.sh - Configure and build qypr-lock (C++/CMake).
#
# Usage:
#   ./scripts/build.sh              # release build into ./build
#   ./scripts/build.sh -DCMAKE_BUILD_TYPE=Debug

set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

cmake -S "$ROOT" -B "$ROOT/build" -G Ninja "$@"
cmake --build "$ROOT/build"

echo "Built: $ROOT/build/qypr-lock"
