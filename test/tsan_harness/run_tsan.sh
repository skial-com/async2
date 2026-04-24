#!/bin/bash
# Build + run the async2 TSan harness.
#
# Runs each scenario for a fixed duration and prints a single-line summary
# per scenario. TSan race reports (if any) are emitted to stderr inline.
#
# Usage:
#   ./run_tsan.sh               # all scenarios, 5s each
#   ./run_tsan.sh 10            # all scenarios, 10s each
#   ./run_tsan.sh 10 locked_queue
#
# The build dir is test/build-tsan-harness so it doesn't collide with the
# regular test build.

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
TEST_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
BUILD_DIR="$TEST_DIR/build-tsan-harness"

SECONDS_PER="${1:-5}"
SCENARIO="${2:-all}"

mkdir -p "$BUILD_DIR"

# Explicit -S/-B so we ignore any stale in-source CMakeCache in test/.
if [ ! -f "$BUILD_DIR/CMakeCache.txt" ] || \
   ! grep -q ASYNC2_BUILD_TSAN_HARNESS "$BUILD_DIR/CMakeCache.txt"; then
    cmake -S "$TEST_DIR" -B "$BUILD_DIR" \
        -DASYNC2_BUILD_TSAN_HARNESS=ON \
        -DCMAKE_BUILD_TYPE=RelWithDebInfo
fi

JOBS="$(nproc)"
cmake --build "$BUILD_DIR" --target async2_tsan_harness -j "$JOBS"

# halt_on_error=0: keep running past races to collect all of them
# history_size=7: larger per-thread history for better backtraces
export TSAN_OPTIONS="halt_on_error=0 history_size=7 second_deadlock_stack=1"

echo ""
echo "=== Running harness (seconds_per=$SECONDS_PER, scenario=$SCENARIO) ==="
"$BUILD_DIR/async2_tsan_harness" "$SCENARIO" "$SECONDS_PER"
