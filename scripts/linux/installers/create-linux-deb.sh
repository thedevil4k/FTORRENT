#!/bin/bash
# FTorrent Debian Package Generation Script
# This script generates the .deb package for Debian/Ubuntu systems.

set -e

SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )"
BASE_DIR="$SCRIPT_DIR/../../.."
BUILD_DIR="${BUILD_DIR:-$BASE_DIR/build_linux}"

echo "--- FTorrent Debian (.deb) Package Generation ---"

# Ensure build exists and is configured
if [ ! -d "$BUILD_DIR" ] || [ ! -f "$BUILD_DIR/CMakeCache.txt" ]; then
    echo "Project not configured or build dir missing. Running setup and build..."
    bash "$SCRIPT_DIR/../setup/setup-linux.sh"
    bash "$SCRIPT_DIR/../compilation/build-linux.sh"
fi

cd "$BUILD_DIR"

echo "Generating .deb package..."
cpack -G DEB

echo ""
echo "Package generated in: $BUILD_DIR"
ls -lh *.deb || echo "No .deb file found. Check build output."
