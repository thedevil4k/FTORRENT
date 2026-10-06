#!/bin/bash
# FTorrent Test Suite Runner
#
# Configures a build with the test programs enabled, compiles it and runs the
# suites -- the three things the CI test job does, in one command that cannot
# forget the option that turns the tests on.
#
# The suites render widgets offscreen and ask questions about pixels, but FLTK
# still opens a display while they run. On a machine without one the tests are
# wrapped in xvfb-run, which is what CI does, so a headless box runs the same
# tests as a desktop.
#
# Usage:
#   bash scripts/linux/tests/run-tests.sh [--clean] [--all] [ctest options...]
#
#   --clean   remove the build directory first, for a fresh configure
#   --all     include the tests labelled `network`, which the default run skips
#
# Anything else is handed to ctest, so -R <regex> picks suites, -V shows their
# output while they run, and so on. BUILD_DIR overrides where the build lives.

set -e

SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )"
BASE_DIR="$SCRIPT_DIR/../../.."
BUILD_DIR="${BUILD_DIR:-$BASE_DIR/build_tests}"

CLEAN=0
ALL=0
CTEST_ARGS=()
for arg in "$@"; do
    case "$arg" in
        --clean) CLEAN=1 ;;
        --all)   ALL=1 ;;
        -*)      echo "Unknown option: $arg" >&2; exit 2 ;;
        *)       CTEST_ARGS+=("$arg") ;;
    esac
done

die() { echo "Error: $*" >&2; exit 1; }

if [ "$CLEAN" = 1 ]; then
    echo "Cleaning previous build..."
    rm -rf "$BUILD_DIR"
fi

echo "--- FTorrent Test Suites ---"
echo "Build directory: $BUILD_DIR"

mkdir -p "$BUILD_DIR"
cd "$BASE_DIR"

echo "[1/3] Configuring (tests enabled)..."
cmake -B "$BUILD_DIR" -S . \
    -DCMAKE_BUILD_TYPE=Release \
    -DFTORRENT_BUILD_TESTS=ON

echo "[2/3] Building..."
cmake --build "$BUILD_DIR" --config Release -j"$(nproc)"

echo "[3/3] Running the suites..."

# A Wayland session counts as a display: FLTK 1.4 can draw on one, and the
# environment tests/CMakeLists.txt sets forces the X11 path only where the
# configuring shell could not see either.
RUNNER=()
if [ -z "${DISPLAY:-}" ] && [ -z "${WAYLAND_DISPLAY:-}" ]; then
    command -v xvfb-run >/dev/null 2>&1 ||
        die "no display and no xvfb-run to stand in for one. Install it (Debian/Ubuntu: apt install xvfb, Fedora: dnf install xorg-x11-server-Xvfb, Arch: pacman -S xorg-server-xvfb xorg-xauth) or run this from a graphical session"
    echo "No display found, so the suites run under Xvfb."
    RUNNER=(xvfb-run -a)
fi

TEST_ARGS=(--output-on-failure)
# `flows` searches trackers and downloads over the network; it carries the
# `network` label so this can leave it out by default, exactly as CI does.
if [ "$ALL" != 1 ]; then
    TEST_ARGS+=(-LE network)
fi
if [ ${#CTEST_ARGS[@]} -gt 0 ]; then
    TEST_ARGS+=("${CTEST_ARGS[@]}")
fi

"${RUNNER[@]}" ctest --test-dir "$BUILD_DIR" "${TEST_ARGS[@]}"

echo ""
echo "=========================================="
echo "All tests passed."
[ "$ALL" = 1 ] || echo "The network-labelled suites were skipped; --all runs them."
echo "=========================================="
