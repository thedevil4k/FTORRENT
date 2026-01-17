# Instalación de Dependencias para FLTorrent

## Requisitos Previos
- Git instalado
- Visual Studio 2019 o posterior (con herramientas de C++)
- CMake 3.15 o superior

## Método 1: Usando vcpkg (Recomendado)

### Paso 1: Instalar vcpkg

Si aún no tienes vcpkg instalado:

```powershell
# Navegar a una ubicación donde instalar vcpkg (ej: C:\)
cd C:\
git clone https://github.com/microsoft/vcpkg.git
cd vcpkg
.\bootstrap-vcpkg.bat
```

### Paso 2: Integrar vcpkg con CMake

```powershell
.\vcpkg integrate install
```

### Paso 3: Instalar las dependencias

```powershell
# Instalar FLTK
.\vcpkg install fltk:x64-windows

# Instalar libtorrent-rasterbar
.\vcpkg install libtorrent:x64-windows
```

**Nota:** El proceso de instalación puede tardar varios minutos, especialmente para libtorrent.

### Paso 4: Configurar CMake para usar vcpkg

Al ejecutar cmake, necesitas especificar la toolchain de vcpkg:

```powershell
cd d:\FLTORRENT
mkdir build
cd build
cmake .. -DCMAKE_TOOLCHAIN_FILE=C:/vcpkg/scripts/buildsystems/vcpkg.cmake
cmake --build . --config Release
```

## Método 2: Configuración Manual (Alternativa)

Si ya tienes las bibliotecas instaladas manualmente:

1. Configura las variables de entorno:
   - `FLTK_DIR`: Ruta a la instalación de FLTK
   - `LibtorrentRasterbar_DIR`: Ruta a la instalación de libtorrent

2. Ejecuta CMake normalmente:
```powershell
cd d:\FLTORRENT
mkdir build
cd build
cmake ..
cmake --build . --config Release
```

## Verificación

Después de compilar, deberías tener:
- `build/Release/FLTorrent.exe` (o `build/Debug/FLTorrent.exe`)

Ejecuta el programa para verificar que funciona correctamente.

## Resolución de Problemas

### Error: "Could not find FLTK"
- Verifica que vcpkg haya instalado correctamente FLTK
- Asegúrate de usar la toolchain de vcpkg en cmake

### Error: "Could not find LibtorrentRasterbar"
- Verifica la instalación con: `vcpkg list | findstr libtorrent`
- Reinstala si es necesario: `vcpkg remove libtorrent:x64-windows` y luego `vcpkg install libtorrent:x64-windows`

### Error de compilación con Boost
- libtorrent depende de Boost, que vcpkg instalará automáticamente
- Si hay problemas, reinstala: `vcpkg install boost:x64-windows`
