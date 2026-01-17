# 📄 Referencia Técnica de FLTorrent

Este documento proporciona una descripción detallada de la arquitectura de software de FLTorrent, sus clases principales y cómo interactúan entre sí.

## 🏗️ Arquitectura General

FLTorrent sigue un patrón de diseño modular donde la lógica de red (libtorrent) está desacoplada de la interfaz de usuario (FLTK).

### Componentes Principales

1.  **Capa de Aplicación (`main.cpp`)**: Punto de entrada que inicializa los recursos, carga la configuración y lanza la ventana principal.
2.  **Capa de Gestión (`TorrentManager`)**: El "cerebro" de la aplicación. Gestiona la lista de torrents y actúa como mediador entre la UI y la sesión de red.
3.  **Capa de Red (`TorrentSession`)**: Un wrapper técnico sobre `libtorrent-rasterbar`. Maneja la sesión real, las alertas y las operaciones de red de bajo nivel.
4.  **Capa de Datos (`TorrentItem`)**: Representa el estado y la información de un único torrent.
5.  **Capa de UI (`MainWindow`, `TorrentListWidget`, etc.)**: Gestiona la presentación visual y la interacción con el usuario.

---

## 🛠️ Detalle de Clases

### 1. `TorrentManager`
Es el núcleo funcional del programa.
- **Responsabilidades:**
    - Inicializar y detener la sesión de torrents.
    - Sincronizar el estado de libtorrent con los objetos `TorrentItem`.
    - Notificar a la UI mediante callbacks cuando ocurren eventos (torrent añadido, actualizado, error).
    - Proporcionar estadísticas globales (velocidad total de subida/bajada).

### 2. `TorrentSession`
Maneja la complejidad de `libtorrent`.
- **Características:**
    - Configura ajustes de red (DHT, PeX, LSD, UPnP).
    - Procesa las alertas del sistema de libtorrent.
    - Maneja la persistencia de los handles de los torrents.

### 3. `SettingsManager`
Singleton encargado de la persistencia.
- **Funcionalidad:**
    - Guarda y carga archivos `.ini`.
    - Almacena rutas de descarga, límites de velocidad y estado de la ventana.

### 4. `Resources`
Gestor de activos estáticos.
- **Función:** Centraliza la carga de iconos en formato XPM embebidos en el código para asegurar la portabilidad sin depender de archivos de imagen externos.

---

## 🔄 Flujo de Datos

1.  **Añadir Torrent:**
    - `MainWindow` captura la ruta del archivo.
    - `TorrentManager::addTorrentFile()` es invocado.
    - `TorrentSession` ordena a `libtorrent` iniciar la descarga.
    - `TorrentManager` recibe una alerta de éxito y crea un `TorrentItem`.
    - La UI se actualiza mediante el callback `onTorrentAdded`.

2.  **Ciclo de Actualización:**
    - El bucle de eventos de FLTK llama periódicamente (mediante timers) a `TorrentManager::update()`.
    - `TorrentManager` consulta el estado a `libtorrent`.
    - Los `TorrentItem` se actualizan con nuevas velocidades y progresos.
    - La UI refleja estos cambios en el `TorrentListWidget`.

---

## 🛠️ Tecnologías Utilizadas

- **Lenguaje:** C++17
- **GUI Toolkit:** FLTK 1.3.x (Fast Light Toolkit)
- **Motor Torrent:** libtorrent-rasterbar 2.0.x
- **Build System:** CMake 3.15+
- **Dependencias:** Boost (vía libtorrent), vcpkg (gestor de paquetes)
