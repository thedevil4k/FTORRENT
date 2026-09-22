#!/bin/bash
# FTorrent Arch Linux Package Generation Script
# Generates the .pkg.tar.zst package for Arch-based systems (Arch Linux, CachyOS, Manjaro...).

set -e

SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )"
BASE_DIR="$( cd "$SCRIPT_DIR/../../.." &> /dev/null && pwd )"
BUILD_DIR="${BUILD_DIR:-$BASE_DIR/build_linux}"

echo "--- FTorrent Arch Linux Package Generation ---"

# Version source of truth: CMakeLists.txt
VERSION="$(sed -n 's/^project(FTorrent VERSION \([0-9.]*\).*/\1/p' "$BASE_DIR/CMakeLists.txt")"
if [ -z "$VERSION" ]; then
    echo "Error: could not read project version from CMakeLists.txt"
    exit 1
fi

# Ensure makepkg is available (runs distro setup if missing)
if ! command -v makepkg >/dev/null 2>&1; then
    echo "makepkg not found. Running setup..."
    bash "$SCRIPT_DIR/../setup/setup-linux.sh"
fi

mkdir -p "$BUILD_DIR"
BUILD_DIR="$( cd "$BUILD_DIR" &> /dev/null && pwd )"
WORK_DIR="$BUILD_DIR/archpkg"

# Prepare an isolated packaging dir with the correct pkgver
rm -rf "$WORK_DIR"
mkdir -p "$WORK_DIR"
cp "$BASE_DIR/packaging/arch/PKGBUILD" "$WORK_DIR/PKGBUILD"
sed -i "s/^pkgver=.*/pkgver=$VERSION/" "$WORK_DIR/PKGBUILD"

echo "Generating Arch package (ftorrent-$VERSION)..."
if [ "$(id -u)" -eq 0 ]; then
    # makepkg refuses to run as root (e.g. inside CI containers)
    BUILDER="${FTP_BUILDER:-ftorrent-builder}"
    if ! id "$BUILDER" >/dev/null 2>&1; then
        useradd -m "$BUILDER"
    fi
    chown -R "$BUILDER" "$WORK_DIR"
    su - "$BUILDER" -c "cd '$WORK_DIR' && FTP_SRC='$BASE_DIR' makepkg -f"
else
    (cd "$WORK_DIR" && FTP_SRC="$BASE_DIR" makepkg -f)
fi

mv "$WORK_DIR"/*.pkg.tar.zst "$BUILD_DIR/"

echo ""
echo "Package generated in: $BUILD_DIR"
ls -lh "$BUILD_DIR"/*.pkg.tar.zst || echo "No .pkg.tar.zst file found. Check build output."
