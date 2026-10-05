#!/bin/bash
# FTorrent Flatpak Bundle Smoke Test
#
# Installs the built bundle into the user's flatpak installation -- no root
# needed, and the runtimes the host already has are reused -- then checks that
# every shared library FTorrent needs resolves inside the sandbox and that the
# desktop integration files landed where Flathub expects them. The app is
# uninstalled again either way, so a failing run leaves nothing behind.
#
# Usage:
#   bash scripts/flatpak/test-flatpak.sh [bundle] [--launch]
#
# --launch also starts the app for a few seconds. It is the only check that
# catches a library the linker resolved but the sandbox cannot load, and it
# needs a display; on a headless machine wrap it in xvfb-run:
#   xvfb-run -a bash scripts/flatpak/test-flatpak.sh --launch

set -e

SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )"
BASE_DIR="$( cd "$SCRIPT_DIR/../.." &> /dev/null && pwd )"

APP_ID=io.github.thedevil4k.FTorrent
BUNDLE="$BASE_DIR/$APP_ID.flatpak"
LAUNCH=0
for arg in "$@"; do
    case "$arg" in
        --launch) LAUNCH=1 ;;
        -*) echo "Unknown option: $arg" >&2; exit 2 ;;
        *)  BUNDLE="$arg" ;;
    esac
done

die() { echo "Error: $*" >&2; exit 1; }

command -v flatpak >/dev/null 2>&1 || die "flatpak is not installed"
[ -f "$BUNDLE" ] || die "bundle not found: $BUNDLE (build it with scripts/flatpak/build-flatpak.sh)"

# Uninstall on the way out even when a check fails, so a failed smoke test
# does not leave a copy installed that would shadow the user's own.
installed=0
cleanup() {
    if [ "$installed" = 1 ]; then
        flatpak uninstall --user --noninteractive "$APP_ID" >/dev/null 2>&1 || true
    fi
}
trap cleanup EXIT

echo "--- FTorrent Flatpak Bundle Smoke Test ---"
echo "Bundle: $BUNDLE"
echo

flatpak install --user --bundle --noninteractive --assumeyes "$BUNDLE"
installed=1

echo
echo "== metadata =="
flatpak info --user "$APP_ID" | sed -n '1,9p'

echo
echo "== every shared library resolves inside the sandbox =="
missing="$(flatpak run --user --command=sh "$APP_ID" -c 'ldd /app/bin/FTorrent' 2>/dev/null |
           grep 'not found' || true)"
if [ -n "$missing" ]; then
    echo "$missing"
    die "the sandbox cannot load every library FTorrent needs"
fi
echo "  all libraries resolved"

echo
echo "== the desktop integration files are where Flathub expects them =="
flatpak run --user --command=sh "$APP_ID" -c '
    for f in \
        /app/bin/assets/ftorrent.png \
        /app/share/applications/io.github.thedevil4k.FTorrent.desktop \
        /app/share/icons/hicolor/128x128/apps/ftorrent.png
    do
        if [ -f "$f" ]; then echo "  ok       $f"; else echo "  MISSING  $f"; exit 1; fi
    done
    # CMake installs the AppStream file as .appdata.xml, the name it has in the
    # repo, and flatpak-builder renames it to the modern .metainfo.xml on the
    # way into the sandbox. Either name means Flathub gets what it requires.
    if [ -f /app/share/metainfo/io.github.thedevil4k.FTorrent.metainfo.xml ] ||
       [ -f /app/share/metainfo/io.github.thedevil4k.FTorrent.appdata.xml ]; then
        echo "  ok       /app/share/metainfo/io.github.thedevil4k.FTorrent.xml"
    else
        echo "  MISSING  /app/share/metainfo/io.github.thedevil4k.FTorrent.{metainfo,appdata}.xml"
        exit 1
    fi'

# Flathub rejects a submission whose AppStream metadata does not validate, and
# that is a five-second check compared to the build it would otherwise send.
if command -v appstreamcli >/dev/null 2>&1; then
    echo
    echo "== AppStream metadata validates =="
    if out="$(appstreamcli validate --no-net "$BASE_DIR/$APP_ID.appdata.xml" 2>&1)"; then
        echo "  valid"
    else
        printf '%s\n' "$out"
        die "AppStream metadata does not validate (see above)"
    fi
fi

if [ "$LAUNCH" = 1 ]; then
    echo
    echo "== starts and stays up =="
    [ -n "$DISPLAY$WAYLAND_DISPLAY" ] ||
        die "--launch needs a display; on a headless machine run: xvfb-run -a $0 --launch"
    log="$(mktemp)"
    set +e
    timeout 10 flatpak run --user "$APP_ID" >"$log" 2>&1
    status=$?
    set -e
    if [ "$status" = 124 ]; then
        # 124 is timeout's own status: the app was still running when the
        # window closed, which is the pass here.
        echo "  the app was still running after 10s"
        rm -f "$log"
    else
        cat "$log"
        rm -f "$log"
        die "the app exited with status $status instead of staying up"
    fi
fi

echo
echo "Smoke test passed."
