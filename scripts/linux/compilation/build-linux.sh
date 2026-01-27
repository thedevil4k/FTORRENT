#!/bin/bash
# FTorrent Linux Build Script
# This script compiles the project using CMake and Make/Ninja.

set -e

SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )"
BASE_DIR="$SCRIPT_DIR/../../.."
BUILD_DIR="$BASE_DIR/build_linux"

echo "--- FTorrent Linux Build ---"
echo "Build Directory: $BUILD_DIR"

mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"

echo "[1/2] Configuring project..."
if cmake .. -DCMAKE_BUILD_TYPE=Release; then
    echo "Configuration successful."
else
    echo "Error: CMake configuration failed."
    exit 1
fi

echo "[2/2] Starting build..."
if cmake --build . --config Release -j$(nproc); then
    echo ""
    echo "Current directory contents:"
    ls -F
    echo ""
    echo "=========================================="
    echo "Build finished successfully!"
    echo "Executable is in: $BUILD_DIR/bin/FTorrent"
    echo "=========================================="
else
    echo "Error: Build failed."
    exit 1
fi
