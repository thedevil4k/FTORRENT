#!/bin/bash
# FTorrent Linux Build Script
# This script compiles the project using CMake and Make/Ninja.

set -e

SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )"
BASE_DIR="$SCRIPT_DIR/../../.."
BUILD_DIR="${BUILD_DIR:-$BASE_DIR/build_linux}"

echo "--- FTorrent Linux Build ---"
echo "Build Directory: $BUILD_DIR"

if [ "$1" == "--clean" ]; then
    echo "Cleaning previous build..."
    rm -rf "$BUILD_DIR"
fi

mkdir -p "$BUILD_DIR"
cd "$BASE_DIR"

echo "[1/2] Configuring project..."
cmake -B "$BUILD_DIR" -S . \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX=/usr

echo "[2/2] Starting build..."
cmake --build "$BUILD_DIR" --config Release -j$(nproc)

echo ""
echo "=========================================="
echo "Build finished successfully!"
echo "Executable is in: $BUILD_DIR/FTorrent"
echo "=========================================="
