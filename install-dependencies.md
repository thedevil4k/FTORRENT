# Installing Dependencies for FTorrent

## Prerequisites
- Git installed
- Visual Studio 2019 or later (with C++ tools)
- CMake 3.15 or higher

## Method 1: Using vcpkg (Recommended)

### Step 1: Install vcpkg

If you don't have vcpkg installed yet:

```powershell
# Navigate to a location to install vcpkg (e.g., C:\)
cd C:\
git clone https://github.com/microsoft/vcpkg.git
cd vcpkg
.\bootstrap-vcpkg.bat
```

### Step 2: Integrate vcpkg with CMake

```powershell
.\vcpkg integrate install
```

### Step 3: Install dependencies

```powershell
# Install FLTK
.\vcpkg install fltk:x64-windows

# Install libtorrent-rasterbar
.\vcpkg install libtorrent:x64-windows
```

**Note:** The installation process may take several minutes, especially for libtorrent.

### Step 4: Configure CMake to use vcpkg

When running cmake, you need to specify the vcpkg toolchain:

```powershell
cd d:\FTorrent
mkdir build
cd build
cmake .. -DCMAKE_TOOLCHAIN_FILE=C:/vcpkg/scripts/buildsystems/vcpkg.cmake
cmake --build . --config Release
```

## Method 2: Manual Configuration (Alternative)

If you already have the libraries installed manually:

1. Configure environment variables:
   - `FLTK_DIR`: Path to FLTK installation
   - `LibtorrentRasterbar_DIR`: Path to libtorrent installation

2. Run CMake normally:
```powershell
cd d:\FTorrent
mkdir build
cd build
cmake ..
cmake --build . --config Release
```

## Verification

After compiling, you should have:
- `build/Release/FTorrent.exe` (or `build/Debug/FTorrent.exe`)

Run the program to verify it works correctly.

## Troubleshooting

### Error: "Could not find FLTK"
- Verify that vcpkg has correctly installed FLTK.
- Ensure you are using the vcpkg toolchain in cmake.

### Error: "Could not find LibtorrentRasterbar"
- Verify the installation with: `vcpkg list | findstr libtorrent`
- Reinstall if necessary: `vcpkg remove libtorrent:x64-windows` and then `vcpkg install libtorrent:x64-windows`

### Compilation Error with Boost
- libtorrent depends on Boost, which vcpkg will install automatically.
- If there are problems, reinstall: `vcpkg install boost:x64-windows`
