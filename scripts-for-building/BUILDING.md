# FTorrent Building Guide

This guide provides step-by-step instructions on how to compile FTorrent and create installable packages for Windows and Linux.

All scripts mentioned here are located in the `scripts-for-building/` directory.

---

## 🛠️ Prerequisites

### Windows
- **Visual Studio 2019 or 2022** with "Desktop development with C++" workload.
- **CMake** 3.15 or higher.
- **vcpkg** (recommended for satisfying dependencies).
  - Run `.\install-vcpkg-deps.ps1` in the project root to install dependencies.

### Linux
- **C++ Compiler** (GCC 7+ or Clang 6+).
- **CMake** 3.15 or higher.
- **Development Libraries**: FLTK, libtorrent-rasterbar, zlib, libpng, libjpeg.
  - You can use our setup script: `bash ./scripts-for-building/setup-linux.sh`.

---

## 🏗️ 1. Compiling from Source

### Windows
Run the following script to compile the project:
```powershell
.\scripts-for-building\build-win.ps1
```
*The executable will be generated in `build_windows/bin/`.*

### Linux
Run the following script to compile the project:
```bash
bash ./scripts-for-building/build-linux.sh
```
*The executable will be generated in `build_linux/bin/`.*

---

## 📦 2. Creating Installers and Packages

### Windows (EXE Installer)
To create the Windows setup program:
```powershell
.\scripts-for-building\create-win-installer.ps1
```
*The `.exe` installer will be located in the `build_windows/` folder.*

### Linux (Debian/Ubuntu .deb)
To create a Debian package:
```bash
bash ./scripts-for-building/create-linux-deb.sh
```
*The `.deb` file will be located in the `build_linux/` folder.*

### Linux (Fedora/CentOS .rpm)
To create an RPM package:
```bash
bash ./scripts-for-building/create-linux-rpm.sh
```
*The `.rpm` file will be located in the `build_linux/` folder.*

---

## 💡 Troubleshooting
- **Missing Dependencies on Linux**: Run `bash ./scripts-for-building/setup-linux.sh` to ensure all libraries are installed.
- **CMake Cache Issues**: If you experience errors after updating code or dependencies, delete the `build_windows/` or `build_linux/` folders and run the scripts again.
