# 🌪️ FTorrent

A modern, minimalist, and ultra-lightweight BitTorrent client built with **C++20**, **FLTK**, and **libtorrent-rasterbar**.

![Version](https://img.shields.io/badge/version-1.0.0-blue?style=for-the-badge)
![License](https://img.shields.io/badge/license-MIT-green?style=for-the-badge)
![Platform](https://img.shields.io/badge/platform-Windows%20%7C%20Linux-lightgrey?style=for-the-badge)

FTorrent is designed for users seeking efficiency without sacrificing power. It uses a low-consumption graphical toolkit to ensure that 100% of your resources are dedicated to what matters: your downloads.

---

## 📚 Documentation

We have prepared detailed documentation to help you:

-   📖 **[User Guide](USER-GUIDE.md)**: How to use the program, configure downloads, and optimize your network.
-   🛠️ **[Technical Reference](TECHNICAL-REFERENCE.md)**: Details about the architecture, classes, and data flow for developers.
-   🏗️ **[Architecture](ARCHITECTURE.md)**: In-depth vision of the system design.
-   ⚡ **[Multi-Threading Architecture](MULTITHREADING-ARCHITECTURE.md)**: Details about the multi-core and multi-threaded system.
-   🚀 **[Compilation and Packaging Guide](scripts/BUILDING.md)**: Detailed instructions to compile or create your own installable packages.
-   🚀 **[Compilation Guide](COMPILE-GUIDE.md)**: Step-by-step instructions to compile from source code.

---

## ✨ Key Features

-   🚀 **Native Performance:** Zero heavy frameworks, just pure C++.
-   💪 **Elite Engine:** Powered by `libtorrent-rasterbar` (the same engine as qBittorrent and Deluge).
-   ⚡ **Multi-Core/Multi-Thread:** Optimized architecture to take advantage of all available CPU cores.
-   🎯 **Zero UI Blocking:** Always responsive interface thanks to dedicated worker threads.
-   🎨 **Clean Design:** Intuitive interface inspired by classics but optimized for the present.
-   📦 **Fully Portable:** No heavy external runtime dependencies.
-   🔧 **Expert Configuration:** Granular control over DHT, PEX, LSD and network limits.
-   💎 **System Tray:** Minimize to tray and manage downloads from the background.
-   📉 **Real-time Limits:** Apply bandwidth limits instantly without restarting.
-   📊 **Resource Monitor:** Integrated RAM and CPU usage display.

---

## 🛠️ Quick Requirements

### Do you have everything you need?
Run our check script:
```powershell
.\scripts\windows\setup\check-requirements.ps1
```

### System Requirements
-   **Windows:** 10 or higher + Visual Studio 2019/2022.
-   **Linux:** GCC 7+ / Clang 6+ and CMake 3.15+.
-   **Dependencies:** FLTK 1.3.x and libtorrent 2.0.x (managed via vcpkg).

---

## 🚀 Compilation and Packaging

Building FTorrent is simple. We provide specialized scripts for each platform:

- **Windows**: Use `scripts/windows/compilation/` and `scripts/windows/installers/`.
- **Linux**: Use `scripts/linux/compilation/` and `scripts/linux/installers/`.

For a complete step-by-step manual, please see the **[Building Guide](scripts/BUILDING.md)**.

---

## 🤝 Contributing

Want to improve FTorrent? You're welcome! Check our [Technical Reference](TECHNICAL-REFERENCE.md) to understand how the pieces fit together before sending a Pull Request.

---

## 📝 License

This project is distributed under the **MIT** license. Feel free to use, modify and share it.

---
*Made with ❤️ for the BitTorrent community.*
