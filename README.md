# 🌪️ FLTorrent

Un cliente BitTorrent moderno, minimalista y ultra-ligero construido con **C++17**, **FLTK** y **libtorrent-rasterbar**.

![Version](https://img.shields.io/badge/version-0.1.0-blue?style=for-the-badge)
![License](https://img.shields.io/badge/license-MIT-green?style=for-the-badge)
![Platform](https://img.shields.io/badge/platform-Windows%20%7C%20Linux-lightgrey?style=for-the-badge)

FLTorrent está diseñado para usuarios que buscan eficiencia sin sacrificar potencia. Utiliza un kit de herramientas gráfico de bajo consumo para asegurar que el 100% de tus recursos se dediquen a lo que importa: tus descargas.

---

## 📚 Documentación

Hemos preparado documentación detallada para ayudarte:

-   📖 **[Guía del Usuario](USER-GUIDE.md)**: Cómo usar el programa, configurar descargas y optimizar tu red.
-   🛠️ **[Referencia Técnica](TECHNICAL-REFERENCE.md)**: Detalles sobre la arquitectura, clases y flujo de datos para desarrolladores.
-   🏗️ **[Arquitectura](ARCHITECTURE.md)**: Visión profunda del diseño del sistema.
-   🚀 **[Guía de Compilación](COMPILE-GUIDE.md)**: Instrucciones paso a paso para compilar desde el código fuente.

---

## ✨ Características Principales

-   🚀 **Rendimiento Nativo:** Cero frameworks pesados, solo C++ puro.
-   💪 **Motor de Élite:** Impulsado por `libtorrent-rasterbar` (el mismo motor que qBittorrent y Deluge).
-   🎨 **Diseño Limpio:** Interfaz intuitiva inspirada en los clásicos pero optimizada para el presente.
-   📦 **Totalmente Portable:** Sin dependencias externas pesadas en tiempo de ejecución.
-   🔧 **Configuración Experta:** Control granular sobre DHT, PEX, LSD y límites de red.

---

## 🛠️ Requisitos Rápidos

### ¿Tienes todo lo necesario?
Ejecuta nuestro script de comprobación:
```powershell
.\check-requirements.ps1
```

### Requisitos de Sistema
-   **Windows:** 10 o superior + Visual Studio 2019/2022.
-   **Linux:** GCC 7+ / Clang 6+ y CMake 3.15+.
-   **Dependencias:** FLTK 1.3.x y libtorrent 2.0.x (gestionadas vía vcpkg).

---

## 🚀 Instalación y Compilación

### Windows (vía vcpkg)
```powershell
# 1. Instalar dependencias
.\install-vcpkg-deps.ps1

# 2. Compilar proyecto
.\build.ps1
```

### Linux
```bash
sudo apt-get install libfltk1.3-dev libtorrent-rasterbar-dev
mkdir build && cd build
cmake ..
make -j$(nproc)
```

---

## 🗺️ Hoja de Ruta (Roadmap)

-   [x] **v0.1.0:** Arquitectura base e integración de motor.
-   [x] **v0.2.0:** Gestión de pares, archivos y trackers en tiempo real.
-   [ ] **v0.3.0:** Soporte para Dark Mode nativo y mejoras de seguridad.
-   [ ] **v1.0.0:** Primera versión estable multiplataforma (Flatpak/Installer).

---

## 🤝 Contribuir

¿Quieres mejorar FLTorrent? ¡Eres bienvenido! Revisa nuestra [Referencia Técnica](TECHNICAL-REFERENCE.md) para entender cómo encajan las piezas antes de enviar un Pull Request.

---

## 📝 Licencia

Este proyecto se distribuye bajo la licencia **MIT**. Siéntete libre de usarlo, modificarlo y compartirlo.

---
*Hecho con ❤️ para la comunidad BitTorrent.*
