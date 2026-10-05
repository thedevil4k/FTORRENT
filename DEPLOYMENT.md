# 📦 FTorrent Deployment Guide

This guide provides instructions for building FTorrent as a distributable package for Windows, Linux, and macOS.

## 🚀 Unified Deployment Strategy

FTorrent uses **CMake** and **CPack** to generate native installers automatically.

### Prerequisites (All Platforms)
- **CMake 3.15+**
- **vcpkg** (Cross-platform package manager)
- **Compiler** (MSVC on Windows, GCC/Clang on Linux/macOS)

---

## 🪟 Windows Deployment

### 1. Build Architectures (32-bit & 64-bit)

Open a terminal (PowerShell or CMD) and run:

**For 64-bit (x64):**
```powershell
mkdir build64
cd build64
cmake .. -DCMAKE_TOOLCHAIN_FILE=C:/vcpkg/scripts/buildsystems/vcpkg.cmake -DVCPKG_TARGET_TRIPLET=x64-windows
cmake --build . --config Release
```

**For 32-bit (x86):**
```powershell
mkdir build32
cd build32
cmake .. -DCMAKE_TOOLCHAIN_FILE=C:/vcpkg/scripts/buildsystems/vcpkg.cmake -DVCPKG_TARGET_TRIPLET=x86-windows
cmake --build . --config Release
```

### 2. Generate Installer (EXE)
FTorrent uses **NSIS** to create professional installers.

From the `build` directory:
```powershell
cpack -C Release
```
This will generate:
- `FTorrent-0.1.0-win64.exe` (Installer)
- `FTorrent-0.1.0-win64.zip` (Portable version)

---

## 🐧 Linux Deployment

FTorrent supports `.deb` (Debian/Ubuntu) and `.rpm` (Fedora/RedHat) packages.

### 1. Install Dependencies
You can use the helper script:
```bash
./scripts/linux/setup/setup-linux.sh
```

### 2. Build and Package
You can use the automated scripts:

**Debian/Ubuntu:**
```bash
./scripts/linux/installers/create-linux-deb.sh
```

**Fedora/RedHat:**
```bash
./scripts/linux/installers/create-linux-rpm.sh
```

Or manually:
```bash
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
cmake --build .
cpack
```

This will generate:
- `FTorrent-0.1.0-Linux.deb`
- `FTorrent-0.1.0-Linux.rpm`
- `FTorrent-0.1.0-Linux.tar.gz`

### 3. Flatpak
The Flathub manifest is `io.github.thedevil4k.FTorrent.json` at the repo root, and
CI builds `io.github.thedevil4k.FTorrent.flatpak` from it. Both are reproducible
locally, and the bundle can be checked before it is uploaded:

```bash
bash ./scripts/flatpak/build-flatpak.sh
bash ./scripts/flatpak/test-flatpak.sh
```

---

## 🍎 macOS Deployment

### 1. Build
```bash
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
cmake --build .
```

### 2. Generate Disk Image (DMG)
```bash
cpack
```
This will generate `FTorrent-0.1.0-Darwin.dmg`.

---

## 🛡️ Distribution Security

### Code Signing
- **Windows**: Use `signtool.exe` with a certificate to avoid "SmartScreen" warnings.
- **macOS**: Use `codesign` and `notarization` to allow running without gatekeeper issues.

## 📊 Summary of Packages

| Platform | Format | Technology | Result |
|----------|--------|------------|--------|
| **Windows** | .exe | NSIS | Professional Installer |
| **Windows** | .zip | Portable | No installation required |
| **Debian/Ubuntu** | .deb | dpkg | Native Linux Package |
| **Fedora/SUSE** | .rpm | rpmbuild | Native Linux Package |
| **macOS** | .dmg | DragNDrop | Application Bundle |
| **Universal Linux** | Flatpak | Flatpak | Sandbox Distribution |

---

## 🔧 Automation (CI/CD)

For automated builds, use **GitHub Actions**. A sample workflow is available in `.github/workflows/build-deploy.yml`.
