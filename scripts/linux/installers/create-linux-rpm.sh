#!/bin/bash
# FTorrent RPM Package Generation Script
# This script generates the .rpm package for Fedora/RHEL/CentOS systems.

set -e

SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )"
BASE_DIR="$SCRIPT_DIR/../../.."
BUILD_DIR="${BUILD_DIR:-$BASE_DIR/build_linux}"

echo "--- FTorrent RPM (.rpm) Package Generation ---"

# Ensure build exists and is configured
if [ ! -d "$BUILD_DIR" ] || [ ! -f "$BUILD_DIR/CMakeCache.txt" ]; then
    echo "Project not configured or build dir missing. Running setup and build..."
    bash "$SCRIPT_DIR/../setup/setup-linux.sh"
    bash "$SCRIPT_DIR/../compilation/build-linux.sh"
fi

cd "$BUILD_DIR"

echo "Generating .rpm package..."
cpack -G RPM

echo ""
echo "Package generated in: $BUILD_DIR"
ls -lh *.rpm || echo "No .rpm file found. Check build output."
