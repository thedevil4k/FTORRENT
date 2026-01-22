#!/bin/bash
# FTorrent Linux Build Script
# This script compiles the project using CMake and Make/Ninja.

set -e

SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )"
BASE_DIR="$SCRIPT_DIR/.."
BUILD_DIR="$BASE_DIR/build_linux"

echo "--- FTorrent Linux Build ---"

mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"

echo "Configuring project..."
cmake .. -DCMAKE_BUILD_TYPE=Release

echo "Starting build..."
cmake --build . --config Release -j$(nproc)

echo ""
echo "Build finished. Executable is in: $BUILD_DIR/bin"
