# FTorrent Building Guide

This guide provides step-by-step instructions on how to compile FTorrent and create installable packages for Windows and Linux.

All scripts mentioned here are located in the `scripts/` directory.

---

## 🛠️ Prerequisites

### Windows
- **Visual Studio 2019 or 2022** with "Desktop development with C++" workload.
- **Automated Setup**: Run `.\scripts\windows\setup-windows.bat` in the project root. This installs vcpkg, all libraries, and NSIS automatically.

### Linux
- **C++ Compiler** (GCC 11+ or Clang 14+).
- **CMake** 3.15 or higher.
- **Development Libraries**: FLTK, libtorrent-rasterbar, zlib, libpng, libjpeg.
  - You can use our setup script: `bash ./scripts/linux/setup/setup-linux.sh`.

---

## 🏗️ 1. Compiling from Source

### Windows
Run the setup script first (only once), then the build script:
```powershell
.\scripts\windows\setup-windows.bat
.\scripts\windows\compilation\build-win.ps1
```
*The executable will be generated in `build_windows/bin/`.*

### Linux
Run the following script to compile the project:
```bash
bash ./scripts/linux/compilation/build-linux.sh
```
*The executable will be generated in `build_linux/bin/`.*

---

## 📦 2. Creating Installers and Packages

### Windows (EXE Installer)
To create the Windows setup program:
```powershell
.\scripts\windows\installers\create-win-installer.ps1
```
*The `.exe` installer will be located in the `build_windows/` folder.*

### Linux (Debian/Ubuntu .deb)
To create a Debian package:
```bash
bash ./scripts/linux/installers/create-linux-deb.sh
```
*The `.deb` file will be located in the `build_linux/` folder.*

### Linux (Fedora/CentOS .rpm)
To create an RPM package:
```bash
bash ./scripts/linux/installers/create-linux-rpm.sh
```
*The `.rpm` file will be located in the `build_linux/` folder.*

### Linux (Arch/CachyOS .pkg.tar.zst)
To create an Arch package (requires `base-devel`, run as a regular user, not root):
```bash
bash ./scripts/linux/installers/create-linux-arch.sh
```
*The `.pkg.tar.zst` file will be located in the `build_linux/` folder. Install it with:*
```bash
sudo pacman -U build_linux/ftorrent-*.pkg.tar.zst
```

---

## 📦 3. Building the Flatpak Bundle

The Flathub manifest lives at the repo root (`io.github.thedevil4k.FTorrent.json`),
and the bundle CI publishes is built straight from it. Both can be reproduced
locally with flatpak-builder; the runtime and SDK the manifest names must be
installed first, and the script says so if they are missing.

```bash
bash ./scripts/flatpak/build-flatpak.sh
```
*Takes a while -- boost, libtorrent, FLTK and the app are all built from source --
but it caches in `build_flatpak/` (git-ignored), so later runs only rebuild what
changed. The bundle is written to `io.github.thedevil4k.FTorrent.flatpak`.*

Before uploading that bundle to Flathub, check it the way a user would receive it:

```bash
bash ./scripts/flatpak/test-flatpak.sh            # installs it, checks the sandbox, uninstalls it
bash ./scripts/flatpak/test-flatpak.sh --launch   # also starts the app for 10 seconds
```
*`--launch` needs a display; on a headless machine wrap it in `xvfb-run -a`. Add
`--system` where the app should go into the system-wide installation rather than
this user's -- that is what the CI job uses, running as root inside a container.*

Anything flatpak-builder accepts can be passed through, which is what the one
known environment quirk needs: where FUSE mounts are not permitted -- inside a
container, or a sandboxed terminal -- the build stops with
`Failure spawning rofiles-fuse` before it compiles anything, and asking for the
plain cache checkout fixes it:

```bash
bash ./scripts/flatpak/build-flatpak.sh --disable-rofiles-fuse
```

---

## 💡 Troubleshooting
- **Missing Dependencies on Linux**: Run `bash ./scripts/linux/setup/setup-linux.sh` to ensure all libraries are installed.
- **CMake Cache Issues**: If you experience errors after updating code or dependencies, delete the `build_windows/` or `build_linux/` folders and run the scripts again.
