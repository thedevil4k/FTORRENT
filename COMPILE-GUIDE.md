# 🔨 FTorrent Compilation Guide

## 📋 Available Compilation Scripts

FTorrent includes several scripts to facilitate compilation:

### 1. **compile.bat** (Recommended) ⭐
Complete script with validation, DLL copying, and portable distribution creation.

**Usage:**
```batch
# Compile to default path (D:\FTorrent-compilation)
compile.bat

# Compile to a custom path
compile.bat C:\MyApps\FTorrent
```

**Features:**
- ✅ vcpkg validation
- ✅ Automatic configuration with CMake
- ✅ Compilation in Release mode
- ✅ Automatic .exe copying
- ✅ Automatic copying of all necessary DLLs
- ✅ README.txt creation
- ✅ Error detection and reporting
- ✅ Option to open folder upon completion

---

### 2. **quick-compile.bat** (Fast)
Simplified script for quick compilations.

**Usage:**
```batch
# Compile to default path
quick-compile.bat

# Compile to custom path
quick-compile.bat D:\MyBuild
```

**Features:**
- ✅ Fast compilation
- ✅ Fewer validations
- ✅ Automatically opens the folder
- ⚠️ Fewer informational messages

---

### 3. **build.ps1** (PowerShell)
Advanced PowerShell script (requires PowerShell 5.0+).

**Usage:**
```powershell
# Compile in Release mode
.\build.ps1

# Compile in Debug mode
.\build.ps1 -BuildType Debug

# Specify vcpkg path
.\build.ps1 -VcpkgRoot "C:\my-vcpkg"
```

---

## 🚀 Step-by-Step Compilation Process

### Option A: Using Scripts (Recommended)

1. **Ensure dependencies are installed:**
   ```powershell
   # If you haven't done so yet:
   .\install-vcpkg-deps.ps1
   ```

2. **Run the compilation script:**
   ```batch
   compile.bat D:\FTorrent-compilation
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

After running `compile.bat`, you will have:

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

If vcpkg is not in `C:\vcpkg`, edit the `compile.bat` script:

```batch
REM Change this line:
set "VCPKG_ROOT=C:\vcpkg"

REM To your path:
set "VCPKG_ROOT=D:\MyVcpkg"
```

### Changing the Default Output Path

In `compile.bat`, line 18:

```batch
REM Change:
set "OUTPUT_DIR=D:\FTorrent-compilation"

REM For example:
set "OUTPUT_DIR=C:\Programs\FTorrent"
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
3. The `compile.bat` script does this automatically

---

## 🐧 Compilation on Linux

FTorrent is fully compatible with Linux. Follow these steps to compile on Ubuntu/Debian-based distributions:

### 1. Install System Dependencies

Run the following command to install the necessary build tools and libraries:

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
    libx11-dev \
    libxext-dev \
    libxft-dev \
    libxinerama-dev \
    libxcursor-dev \
    libxfixes-dev
```

### 2. Compilation Process

1. **Create build directory:**
   ```bash
   mkdir build && cd build
   ```

2. **Configure with CMake:**
   ```bash
   cmake .. -DCMAKE_BUILD_TYPE=Release
   ```

3. **Compile:**
   ```bash
   make -j$(nproc)
   ```

4. **Install (optional):**
   ```bash
   sudo make install
   ```

### 3. Execution

Once compiled, the executable will be in the `build` folder:
```bash
./FTorrent
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
- [ ] Dependencies installed (`.\install-vcpkg-deps.ps1`)
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

1. Run `compile.bat` with your desired path
2. The generated folder is fully portable
3. You can compress it into a .zip file
4. Distribute it however you like

### Create Installer (Future)

To create a professional installer:
- Use NSIS or InnoSetup
- Include .torrent extension associations
- Add to the Start menu
- Auto-updater

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
