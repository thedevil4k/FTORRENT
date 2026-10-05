#!/bin/bash
# FTorrent Flatpak Bundle Build Script
#
# Builds io.github.thedevil4k.FTorrent.flatpak from the manifest at the repo
# root: the same manifest, and the same bundle name, the CI job uses. Being
# able to run that job here is what makes a red build-flatpak reproducible on
# a workstation, and what lets a Flathub submission be tried before pushing.
#
# Usage:
#   bash scripts/flatpak/build-flatpak.sh [extra flatpak-builder options...]
#
# The build is long -- boost, libtorrent, FLTK and the app are all compiled
# from source -- but it caches. Downloaded sources and the per-module build
# cache live in $BUILD_DIR/state, and --force-clean only wipes the intermediate
# tree under $BUILD_DIR/app, which flatpak-builder refuses to reuse once a run
# has been interrupted. So a second run picks up where the last one stopped;
# delete $BUILD_DIR to start over completely. It all sits under build_flatpak/,
# which git already ignores, so the checkout stays clean.

set -e

SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )"
BASE_DIR="$( cd "$SCRIPT_DIR/../.." &> /dev/null && pwd )"

APP_ID=io.github.thedevil4k.FTorrent
MANIFEST="$BASE_DIR/$APP_ID.json"
BUILD_DIR="${BUILD_DIR:-$BASE_DIR/build_flatpak}"
BUNDLE="$BASE_DIR/$APP_ID.flatpak"

die() { echo "Error: $*" >&2; exit 1; }

[ -f "$MANIFEST" ] || die "manifest not found: $MANIFEST"

# Read the runtime pair out of the manifest itself, so this script cannot
# drift from what the build actually targets.
RUNTIME="$(sed -n 's/.*"runtime": "\([^"]*\)".*/\1/p' "$MANIFEST" | head -1)"
SDK="$(sed -n 's/.*"sdk": "\([^"]*\)".*/\1/p' "$MANIFEST" | head -1)"
RUNTIME_VERSION="$(sed -n 's/.*"runtime-version": "\([^"]*\)".*/\1/p' "$MANIFEST" | head -1)"
[ -n "$RUNTIME" ] && [ -n "$SDK" ] && [ -n "$RUNTIME_VERSION" ] ||
    die "could not read runtime, sdk and runtime-version from $MANIFEST"

command -v flatpak >/dev/null 2>&1 ||
    die "flatpak is not installed"
command -v flatpak-builder >/dev/null 2>&1 ||
    die "flatpak-builder is not installed (Debian/Ubuntu: apt install flatpak-builder, Arch: pacman -S flatpak-builder)"

# The SDK is what compiles the modules; the runtime is what they link against
# and what the bundle carries by reference, so both have to be present before
# anything is downloaded.
flatpak info "$RUNTIME//$RUNTIME_VERSION" >/dev/null 2>&1 ||
    die "$RUNTIME//$RUNTIME_VERSION is not installed; run: flatpak install flathub $RUNTIME//$RUNTIME_VERSION"
flatpak info "$SDK//$RUNTIME_VERSION" >/dev/null 2>&1 ||
    die "$SDK//$RUNTIME_VERSION is not installed; run: flatpak install flathub $SDK//$RUNTIME_VERSION"

echo "--- FTorrent Flatpak Bundle Build ---"
echo "Manifest  : $MANIFEST"
echo "Runtime   : $RUNTIME//$RUNTIME_VERSION"
echo "SDK       : $SDK//$RUNTIME_VERSION"
echo "Build dir : $BUILD_DIR"
echo

mkdir -p "$BUILD_DIR"

flatpak-builder \
    --force-clean \
    --repo="$BUILD_DIR/repo" \
    --state-dir="$BUILD_DIR/state" \
    "$BUILD_DIR/app" \
    "$MANIFEST" \
    "$@"

# The bundle is what gets uploaded to Flathub, so it is exported with the same
# name and to the same place the CI job uses.
flatpak build-bundle "$BUILD_DIR/repo" "$BUNDLE" "$APP_ID"

echo
echo "Bundle built: $BUNDLE"
echo
echo "Try it with: bash scripts/flatpak/test-flatpak.sh --launch"
