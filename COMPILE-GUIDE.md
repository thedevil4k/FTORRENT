# 🔨 FTorrent Compilation Guide

## 📋 Available Compilation Scripts

FTorrent includes several scripts to facilitate compilation:

### 1. **compile.ps1** (Recommended) ⭐
Complete PowerShell script with validation, DLL copying, assets bundling, and portable distribution creation.

**Usage:**
```powershell
# Compile to default path (D:\FTorrent-compilation)
.\scripts\windows\compilation\compile.ps1

# Compile to a custom path
.\scripts\windows\compilation\compile.ps1 -OutputDir "C:\MyApps\FTorrent"
```

**Features:**
- ✅ vcpkg validation
- ✅ Automatic environment activation (Visual Studio)
- ✅ Automatic configuration with CMake
- ✅ Compilation in Release mode
- ✅ Automatic .exe and DLL copying
- ✅ Assets bundling (src/assets)
- ✅ README.txt creation with build info
- ✅ Colored output and error detection
- ✅ Option to open folder upon completion


---


## 🚀 Step-by-Step Compilation Process

### Option A: Using Scripts (Recommended)

1. **Ensure dependencies are installed:**
   ```powershell
   # If you haven't done so yet:
   .\scripts\windows\setup\install-vcpkg-deps.ps1
   ```

2. **Run the compilation script:**
   ```powershell
   .\scripts\windows\compilation\compile.ps1 -OutputDir "D:\FTorrent-compilation"
   ```

3. **Wait for it to finish** (1-5 minutes the first time)

4. **Done!** Your executable will be in `D:\FTorrent-compilation\FTorrent.exe`

---

### Option B: Manual

1. **Create build directory:**
   ```batch
   mkdir build
   cd build
   ```

2. **Configure with CMake:**
   ```batch
   cmake .. -DCMAKE_TOOLCHAIN_FILE=C:/vcpkg/scripts/buildsystems/vcpkg.cmake -DCMAKE_BUILD_TYPE=Release
   ```

3. **Compile:**
   ```batch
   cmake --build . --config Release
   ```

4. **The executable will be in:**
   ```
   build\Release\FTorrent.exe
   ```

5. **Copy necessary DLLs:**
   ```batch
   xcopy /Y C:\vcpkg\installed\x64-windows\bin\*.dll Release\
   ```

---

## 📦 Compilation Folder Structure

After running `compile.ps1`, you will have:

```
D:\FTorrent-compilation\
├── FTorrent.exe              # Main executable
├── README.txt                # Instructions
├── fltk.dll                  # FLTK library
├── libtorrent.dll            # LibTorrent library
├── boost_*.dll               # Boost libraries
├── zlib1.dll                 # Compression
├── libssl-*.dll              # SSL/TLS
├── libcrypto-*.dll           # Crypto
└── [other necessary DLLs]    # Additional dependencies
```

---

## ⚙️ Path Configuration

### Changing the vcpkg Path

If vcpkg is not in `C:\vcpkg`, you can pass it as a parameter:

```powershell
.\scripts\windows\compilation\compile.ps1 -VcpkgRoot "D:\MyVcpkg"
```

Or edit the `compile.ps1` script:

```batch
REM Change this line:
set "VCPKG_ROOT=C:\vcpkg"

REM To your path:
set "VCPKG_ROOT=D:\MyVcpkg"
```

### Changing the Default Output Path

In `compile.ps1`, you can use the `-OutputDir` parameter:

```powershell
.\scripts\windows\compilation\compile.ps1 -OutputDir "C:\Programs\FTorrent"
```

---

## 🐛 Troubleshooting

### Error: "vcpkg not found"

**Issue:** Script cannot find vcpkg

**Solution:**
1. Verify that vcpkg is installed in `C:\vcpkg`
2. Or edit `VCPKG_ROOT` in the script
3. Or pass the correct path when running

### Error: "CMake configuration failed"

**Issue:** Dependencies not installed

**Solution:**
```powershell
# Reinstall dependencies
cd C:\vcpkg
.\vcpkg install fltk:x64-windows libtorrent:x64-windows
```

### Error: "Build failed"

**Issue:** Compilation error

**Solution:**
1. Check the error messages
2. Verify that Visual Studio is installed
3. Run:
   ```batch
   # Clean and recompile
   cd build
   cmake --build . --config Release --clean-first
   ```

### Error: "Cannot run the .exe"

**Issue:** Missing DLLs

**Solution:**
1. Ensure you run from the compilation folder
2. Or copy all DLLs from `C:\vcpkg\installed\x64-windows\bin\`
3. The `compile.ps1` script does this automatically

---

## 🐧 Compilation on Linux

FTorrent is fully compatible with Linux. You can use the provided helper scripts or compile manually.

### 1. Install System Dependencies

You can use the setup script to install all necessary dependencies for your distribution (Debian, Ubuntu, Fedora, Arch, etc.):

```bash
chmod +x scripts/linux/setup/setup-linux.sh
./scripts/linux/setup/setup-linux.sh
```

Or install manually (Ubuntu/Debian example):
```bash
sudo apt-get update
sudo apt-get install -y \
    build-essential \
    cmake \
    libfltk1.3-dev \
    libtorrent-rasterbar-dev \
    libpng-dev \
    libjpeg-dev \
    zlib1g-dev \
    rpm # Required for packaging
```

### 2. Compilation using Scripts (Recommended)

To compile the project easily:

```bash
chmod +x scripts/linux/compilation/build-linux.sh
./scripts/linux/compilation/build-linux.sh
```

The executable will be located in `build_linux/bin/FTorrent`.

### 3. Creating Installers (.deb / .rpm)

You can generate installable packages for Linux distributions.

**For Debian/Ubuntu (.deb):**
```bash
chmod +x scripts/linux/installers/create-linux-deb.sh
./scripts/linux/installers/create-linux-deb.sh
```

**For Fedora/RHEL/CentOS (.rpm):**
```bash
chmod +x scripts/linux/installers/create-linux-rpm.sh
./scripts/linux/installers/create-linux-rpm.sh
```

The packages will be generated in `build_linux`.

### 4. Manual Compilation

If you prefer to compile manually:

1. **Create build directory:**
   ```bash
   mkdir build_linux && cd build_linux
   ```

2. **Configure with CMake:**
   ```bash
   cmake .. -DCMAKE_BUILD_TYPE=Release
   ```

3. **Compile:**
   ```bash
   make -j$(nproc)
   ```

4. **Run:**
   ```bash
   ./bin/FTorrent
   ```

## 📊 Compilation Times

| Configuration | First Time | Subsequent |
|---------------|-------------|--------------|
| **Clean Build** | 2-5 min | 1-2 min |
| **Incremental** | - | 10-30 sec |
| **Debug** | 3-6 min | 1-3 min |
| **Release** | 2-5 min | 1-2 min |

*Times may vary based on hardware*

---

## 🎯 Compilation Modes

### Release (Recommended for usage)
```batch
cmake .. -DCMAKE_BUILD_TYPE=Release
cmake --build . --config Release
```
- ✅ Optimized for speed
- ✅ Smaller executable
- ✅ No debugging symbols

### Debug (For development)
```batch
cmake .. -DCMAKE_BUILD_TYPE=Debug
cmake --build . --config Debug
```
- ✅ Debugging symbols
- ✅ Easier to debug
- ⚠️ Larger and slower executable

---

## 📝 Pre-Compilation Checklist

- [ ] vcpkg installed in `C:\vcpkg`
- [ ] Visual Studio 2019+ installed
- [ ] CMake 3.15+ installed
- [ ] Dependencies installed (`.\scripts\windows\setup\install-vcpkg-deps.ps1`)
- [ ] vcpkg integrated (`vcpkg integrate install`)
- [ ] Git installed (optional but recommended)

---

## 🔄 Clean Compilation

If something fails, try a clean compilation:

```batch
# Remove previous build
rd /s /q build

# Create new build
mkdir build
cd build

# Configure and compile again
cmake .. -DCMAKE_TOOLCHAIN_FILE=C:/vcpkg/scripts/buildsystems/vcpkg.cmake
cmake --build . --config Release
```

---

## 🌐 Compilation for Distribution

### Portable Version

1. Run `compile.ps1` with your desired path
2. The generated folder is fully portable
3. You can compress it into a .zip file
4. Distribute it however you like

### Create Installer (Recommended for release)

To create a professional NSIS installer and a ZIP package:

1. **Run the installer script:**
   ```powershell
   .\scripts\windows\installers\create-win-installer.ps1
   ```

**Features:**
- ✅ **Self-Compiling:** Automatically recompiles the project before packaging to ensure the latest version.
- ✅ **NSIS Installer:** Creates a standard `.exe` installer with desktop and start menu shortcuts.
- ✅ **Portable ZIP:** Also generates a `.zip` file for manual distribution.
- ✅ **Asset Bundling:** Automatically includes all icons and necessary files.

---

## ✅ Successful Compilation Verification

After compiling, verify:

1. **.exe file exists:**
   ```batch
   dir D:\FTorrent-compilation\FTorrent.exe
   ```

2. **DLLs copied:**
   ```batch
   dir D:\FTorrent-compilation\*.dll
   ```

3. **Executable works:**
   ```batch
   cd D:\FTorrent-compilation
   FTorrent.exe
   ```

4. **Interface opens correctly**

---

## 📞 Support

If you have problems:

1. Review this guide
2. Verify requirements
3. Check compilation logs
4. Search for the specific error on Google
5. Open an issue on GitHub

---

**Successful compilation!** 🎉

Now you can run FTorrent from:
```
D:\FTorrent-compilation\FTorrent.exe
```
