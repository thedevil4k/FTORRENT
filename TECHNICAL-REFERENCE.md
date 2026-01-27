# 📄 FTorrent Technical Reference

This document provides a detailed description of FTorrent's software architecture, its main classes, and how they interact with each other.

## 🏗️ General Architecture

FTorrent follows a modular design pattern where the network logic (libtorrent) is decoupled from the user interface (FLTK).

### Main Components

1.  **Application Layer (`main.cpp`)**: Point of entry that initializes resources, loads configuration, and launches the main window.
2.  **Management Layer (`TorrentManager`)**: The "brain" of the application. It manages the torrent list and acts as a mediator between the UI and the network session.
3.  **Network Layer (`TorrentSession`)**: A technical wrapper over `libtorrent-rasterbar`. It handles the actual session, alerts, and low-level network operations.
4.  **Data Layer (`TorrentItem`)**: Represents the status and information of a single torrent.
5.  **UI Layer (`MainWindow`, `TorrentListWidget`, etc.)**: Manages the visual presentation and user interaction.

---

## 🛠️ Class Details

### 1. `TorrentManager`
It is the functional core of the program.
- **Responsibilities:**
    - Initializing and stopping the torrent session.
    - Synchronizing the libtorrent state with `TorrentItem` objects.
    - Notifying the UI via callbacks when events occur (torrent added, updated, error).
    - Providing global statistics (total upload/download speed).

### 2. `TorrentSession`
Handles the complexity of `libtorrent`.
- **Features:**
    - Configures network settings (DHT, PeX, LSD, UPnP).
    - Processes libtorrent system alerts.
    - Handles the persistence of torrent handles.

### 3. `SettingsManager`
Singleton in charge of persistence.
- **Functionality:**
    - Saves and loads `.ini` files.
    - Stores download paths, speed limits, and window state.

### 4. `Resources`
Static asset manager.
- **Function:** Centralizes the loading of XPM icons embedded in the code to ensure portability without depending on external image files.

---

## 🔄 Data Flow

1.  **Adding a Torrent:**
    - `MainWindow` captures the file path.
    - `TorrentManager::addTorrentFile()` is invoked.
    - `TorrentSession` orders `libtorrent` to start the download.
    - `TorrentManager` receives a success alert and creates a `TorrentItem`.
    - The UI is updated via the `onTorrentAdded` callback.

2.  **Update Cycle:**
    - The FLTK event loop periodically calls `TorrentManager::update()` via timers.
    - `TorrentManager` queries `libtorrent` for status.
    - `TorrentItem` objects are updated with new speeds and progress.
    - The UI reflects these changes in the `TorrentListWidget`.

---

## 💎 Resource Management (XPM)

To maintain a zero-dependency portable executable, FTorrent uses **embedded XPM resources**:
- **Icons:** All UI icons (Add, Magnet, Pause, Resume, Remove, Settings) are stored as static `char*` arrays in `Icons.h`.
- **Packaging:** The `Resources` class handles the conversion of these XPM arrays into `Fl_RGB_Image` objects at runtime.
- **Advantage:** Eliminates the need for an `assets/` folder and prevents "missing icon" errors during distribution.

---

## ⚡ Multi-threading Architecture

FTorrent is designed to be completely non-blocking:
1.  **Main Thread (UI):** exclusively handles FLTK events and window rendering.
2.  **Worker Thread (Alerts):** A dedicated thread polls `libtorrent` for alerts every 100ms.
3.  **I/O Threads:** Internal `libtorrent` threads handle disk and network operations.
4.  **Synchronization:** Uses `std::mutex` and `std::atomic` to safely share data between the alert processor and the UI controllers.

## 🛠️ Technologies Used

- **Language:** C++20
- **GUI Toolkit:** FLTK 1.3.x (Fast Light Toolkit)
- **Torrent Engine:** libtorrent-rasterbar 2.0.x
- **Build System:** CMake 3.15+
- **Dependencies:** Boost (via libtorrent), vcpkg (package manager)
