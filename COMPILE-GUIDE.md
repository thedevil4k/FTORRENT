# 🔨 Guía de Compilación de FLTorrent

## 📋 Scripts de Compilación Disponibles

FLTorrent incluye varios scripts para facilitar la compilación:

### 1. **compile.bat** (Recomendado) ⭐
Script completo con validación, copia de DLLs y creación de distribución portable.

**Uso:**
```batch
# Compilar a la ruta por defecto (D:\fltorrent-compilation)
compile.bat

# Compilar a una ruta personalizada
compile.bat C:\MisAplicaciones\FLTorrent
```

**Características:**
- ✅ Validación de vcpkg
- ✅ Configuración automática con CMake
- ✅ Compilación en modo Release
- ✅ Copia automática del .exe
- ✅ Copia de todas las DLLs necesarias
- ✅ Creación de README.txt
- ✅ Detección y reporte de errores
- ✅ Opción de abrir carpeta al finalizar

---

### 2. **quick-compile.bat** (Rápido)
Script simplificado para compilaciones rápidas.

**Uso:**
```batch
# Compilar a la ruta por defecto
quick-compile.bat

# Compilar a ruta personalizada
quick-compile.bat D:\MiBuild
```

**Características:**
- ✅ Compilación rápida
- ✅ Menos validaciones
- ✅ Abre la carpeta automáticamente
- ⚠️ Menos mensajes informativos

---

### 3. **build.ps1** (PowerShell)
Script PowerShell avanzado (requiere PowerShell 5.0+).

**Uso:**
```powershell
# Compilar en modo Release
.\build.ps1

# Compilar en modo Debug
.\build.ps1 -BuildType Debug

# Especificar ruta de vcpkg
.\build.ps1 -VcpkgRoot "C:\mi-vcpkg"
```

---

## 🚀 Proceso de Compilación Paso a Paso

### Opción A: Usando Scripts (Recomendado)

1. **Asegúrate que las dependencias estén instaladas:**
   ```powershell
   # Si aún no lo has hecho:
   .\install-vcpkg-deps.ps1
   ```

2. **Ejecuta el script de compilación:**
   ```batch
   compile.bat D:\fltorrent-compilation
   ```

3. **Espera a que termine** (1-5 minutos la primera vez)

4. **¡Listo!** Tu ejecutable estará en `D:\fltorrent-compilation\FLTorrent.exe`

---

### Opción B: Manual

1. **Crear directorio de build:**
   ```batch
   mkdir build
   cd build
   ```

2. **Configurar con CMake:**
   ```batch
   cmake .. -DCMAKE_TOOLCHAIN_FILE=C:/vcpkg/scripts/buildsystems/vcpkg.cmake -DCMAKE_BUILD_TYPE=Release
   ```

3. **Compilar:**
   ```batch
   cmake --build . --config Release
   ```

4. **El ejecutable estará en:**
   ```
   build\Release\FLTorrent.exe
   ```

5. **Copiar DLLs necesarias:**
   ```batch
   xcopy /Y C:\vcpkg\installed\x64-windows\bin\*.dll Release\
   ```

---

## 📦 Estructura de la Carpeta de Compilación

Después de ejecutar `compile.bat`, tendrás:

```
D:\fltorrent-compilation\
├── FLTorrent.exe              # Ejecutable principal
├── README.txt                 # Instrucciones
├── fltk.dll                   # FLTK library
├── libtorrent.dll             # LibTorrent library
├── boost_*.dll                # Boost libraries
├── zlib1.dll                  # Compression
├── libssl-*.dll               # SSL/TLS
├── libcrypto-*.dll            # Crypto
└── [otras DLLs necesarias]    # Dependencias adicionales
```

---

## ⚙️ Configuración de Rutas

### Cambiar la Ruta de vcpkg

Si vcpkg no está en `C:\vcpkg`, edita el script `compile.bat`:

```batch
REM Cambiar esta línea:
set "VCPKG_ROOT=C:\vcpkg"

REM Por tu ruta:
set "VCPKG_ROOT=D:\MiVcpkg"
```

### Cambiar la Ruta de Salida por Defecto

En `compile.bat`, línea 18:

```batch
REM Cambiar:
set "OUTPUT_DIR=D:\fltorrent-compilation"

REM Por ejemplo:
set "OUTPUT_DIR=C:\Programas\FLTorrent"
```

---

## 🐛 Solución de Problemas

### Error: "vcpkg not found"

**Problema:** Script no encuentra vcpkg

**Solución:**
1. Verifica que vcpkg esté instalado en `C:\vcpkg`
2. O edita `VCPKG_ROOT` en el script
3. O pasa la ruta correcta al ejecutar

### Error: "CMake configuration failed"

**Problema:** Dependencias no instaladas

**Solución:**
```powershell
# Reinstalar dependencias
cd C:\vcpkg
.\vcpkg install fltk:x64-windows libtorrent:x64-windows
```

### Error: "Build failed"

**Problema:** Error de compilación

**Solución:**
1. Revisa los mensajes de error
2. Verifica que Visual Studio esté instalado
3. Ejecuta:
   ```batch
   # Limpia y recompila
   cd build
   cmake --build . --config Release --clean-first
   ```

### Error: "No se puede ejecutar el .exe"

**Problema:** Faltan DLLs

**Solución:**
1. Asegúrate de ejecutar desde la carpeta de compilación
2. O copia todas las DLLs de `C:\vcpkg\installed\x64-windows\bin\`
3. El script `compile.bat` hace esto automáticamente

---

## 🐧 Compilación en Linux

FLTorrent es totalmente compatible con Linux. Sigue estos pasos para compilar en distribuciones basadas en Ubuntu/Debian:

### 1. Instalar Dependencias de Sistema

Ejecuta el siguiente comando para instalar las herramientas de compilación y las librerías necesarias:

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

### 2. Proceso de Compilación

1. **Crear directorio de build:**
   ```bash
   mkdir build && cd build
   ```

2. **Configurar con CMake:**
   ```bash
   cmake .. -DCMAKE_BUILD_TYPE=Release
   ```

3. **Compilar:**
   ```bash
   make -j$(nproc)
   ```

4. **Instalar (opcional):**
   ```bash
   sudo make install
   ```

### 3. Ejecución

Una vez compilado, el ejecutable estará en la carpeta `build`:
```bash
./FLTorrent
```

## 📊 Tiempos de Compilación

| Configuración | Primera Vez | Subsecuentes |
|---------------|-------------|--------------|
| **Clean Build** | 2-5 min | 1-2 min |
| **Incremental** | - | 10-30 seg |
| **Debug** | 3-6 min | 1-3 min |
| **Release** | 2-5 min | 1-2 min |

*Tiempos pueden variar según el hardware*

---

## 🎯 Modos de Compilación

### Release (Recomendado para uso)
```batch
cmake .. -DCMAKE_BUILD_TYPE=Release
cmake --build . --config Release
```
- ✅ Optimizado para velocidad
- ✅ Ejecutable más pequeño
- ✅ Sin símbolos de depuración

### Debug (Para desarrollo)
```batch
cmake .. -DCMAKE_BUILD_TYPE=Debug
cmake --build . --config Debug
```
- ✅ Símbolos de depuración
- ✅ Más fácil de depurar
- ⚠️ Ejecutable más grande y lento

---

## 📝 Checklist Previo a la Compilación

- [ ] vcpkg instalado en `C:\vcpkg`
- [ ] Visual Studio 2019+ instalado
- [ ] CMake 3.15+ instalado
- [ ] Dependencias instaladas (`.\install-vcpkg-deps.ps1`)
- [ ] vcpkg integrado (`vcpkg integrate install`)
- [ ] Git instalado (opcional pero recomendado)

---

## 🔄 Compilación Limpia

Si algo falla, intenta una compilación limpia:

```batch
# Eliminar build anterior
rd /s /q build

# Crear nuevo build
mkdir build
cd build

# Configurar y compilar de nuevo
cmake .. -DCMAKE_TOOLCHAIN_FILE=C:/vcpkg/scripts/buildsystems/vcpkg.cmake
cmake --build . --config Release
```

---

## 🌐 Compilación para Distribución

### Versión Portable

1. Ejecuta `compile.bat` con tu ruta deseada
2. La carpeta generada es completamente portable
3. Puedes comprimirla en .zip
4. Distribúyela como quieras

### Crear Instalador (Futuro)

Para crear un instalador profesional:
- Usar NSIS o InnoSetup
- Incluir registro de asociaciones .torrent
- Agregar al menú inicio
- Auto-updater

---

## ✅ Verificación de Compilación Exitosa

Después de compilar, verifica:

1. **Archivo .exe existe:**
   ```batch
   dir D:\fltorrent-compilation\FLTorrent.exe
   ```

2. **DLLs copiadas:**
   ```batch
   dir D:\fltorrent-compilation\*.dll
   ```

3. **Ejecutable funciona:**
   ```batch
   cd D:\fltorrent-compilation
   FLTorrent.exe
   ```

4. **Interfaz se abre correctamente**

---

## 📞 Soporte

Si tienes problemas:

1. Revisa esta guía
2. Verifica los requisitos
3. Revisa los logs de compilación
4. Busca el error específico en Google
5. Abre un issue en GitHub

---

**¡Compilación exitosa!** 🎉

Ahora puedes ejecutar FLTorrent desde:
```
D:\fltorrent-compilation\FLTorrent.exe
```
