# FLTorrent - Arquitectura del Proyecto

## 📐 Visión General

FLTorrent está diseñado con una arquitectura modular y orientada a objetos que separa claramente las responsabilidades:

```
┌─────────────────────────────────────────┐
│           Interfaz FLTK (UI)            │
│          (main.cpp, widgets)            │
└──────────────┬──────────────────────────┘
               │
               ▼
┌─────────────────────────────────────────┐
│         TorrentManager (Facade)         │
│      - Coordina operaciones             │
│      - Sistema de callbacks             │
└──────┬──────────────────────────────────┘
       │
       ├──────▶ TorrentSession
       │        (Wrapper de libtorrent)
       │
       ├──────▶ TorrentItem
       │        (Modelo de torrent individual)
       │
       └──────▶ SettingsManager
                (Configuración global)
```

## 📦 Componentes Principales

### 1. TorrentSession
**Archivo:** `TorrentSession.h/cpp`

**Responsabilidad:** Wrapper de bajo nivel sobre libtorrent-rasterbar

**Características:**
- Inicialización y configuración de la sesión de libtorrent
- Operaciones básicas: añadir/eliminar torrents
- Procesamiento de alertas de libtorrent
- Obtención de estadísticas globales

**API Principal:**
```cpp
bool initialize();
bool addTorrentFile(const std::string& file, const std::string& path);
bool addMagnetLink(const std::string& magnet, const std::string& path);
std::vector<lt::torrent_handle> getTorrents();
void processAlerts();
```

---

### 2. TorrentItem
**Archivo:** `TorrentItem.h/cpp`

**Responsabilidad:** Modelo de datos para un torrent individual

**Características:**
- Encapsula toda la información de un torrent
- Cachea datos para rendimiento
- Proporciona métodos de formateo (tamaño, velocidad, tiempo)
- Estados predefinidos: Queued, Checking, Downloading, Seeding, Paused, Error

**Datos que maneja:**
- Información básica: nombre, hash, ruta
- Progreso: tamaño total, descargado, progreso %
- Velocidades: download/upload rate
- Peers: número de peers y seeds
- Tiempos: ETA, tiempo añadido, tiempo completado
- Ratio de subida

**API Principal:**
```cpp
void update(); // Actualiza desde libtorrent
std::string getName();
double getProgress();
int getDownloadRate();
State getState();
std::string formatSize(int64_t bytes);
```

---

### 3. TorrentManager
**Archivo:** `TorrentManager.h/cpp`

**Responsabilidad:** Gestor central que coordina todo

**Características:**
- Fachada principal para la UI
- Mantiene lista de TorrentItems
- Sistema de callbacks para notificar cambios
- Sincronización automática con libtorrent
- Operaciones de alto nivel sobre torrents

**Sistema de Callbacks:**
```cpp
using TorrentAddedCallback = std::function<void(TorrentItem*)>;
using TorrentRemovedCallback = std::function<void(const std::string& hash)>;
using TorrentUpdatedCallback = std::function<void(TorrentItem*)>;
using StatsUpdatedCallback = std::function<void()>;
using ErrorCallback = std::function<void(const std::string& error)>;
```

**API Principal:**
```cpp
bool initialize();
bool addTorrentFile(const std::string& file, const std::string& path);
void removeTorrent(const std::string& hash, bool deleteFiles);
void pauseTorrent(const std::string& hash);
void resumeTorrent(const std::string& hash);
std::vector<TorrentItem*> getAllTorrents();
void update(); // Llamar regularmente desde UI
```

---

### 4. SettingsManager
**Archivo:** `SettingsManager.h/cpp`

**Responsabilidad:** Gestión de configuración persistente

**Características:**
- Patrón Singleton
- Carga/guarda configuración en archivo INI
- Valores por defecto razonables
- Configuración multiplataforma (Windows/Linux)

**Categorías de Configuración:**
- **General:** ruta de descarga, inicio con sistema
- **Red:** límites de velocidad, puerto, conexiones
- **BitTorrent:** DHT, PEX, LSD, UPnP
- **UI:** posición y tamaño de ventana
- **Avanzado:** user agent, configuraciones personalizadas

**API Principal:**
```cpp
static SettingsManager& instance(); // Singleton
bool load();
bool save();
std::string getDefaultSavePath();
int getMaxDownloadRate();
bool getDHTEnabled();
// ... muchos más getters/setters
```

---

## 🔄 Flujo de Datos

### Añadir un Torrent:
```
UI → TorrentManager::addTorrentFile()
   → TorrentSession::addTorrentFile()
   → libtorrent añade el torrent
   → TorrentManager::syncTorrents()
   → Crea nuevo TorrentItem
   → Callback: onTorrentAdded(item)
   → UI actualiza la lista
```

### Actualización Periódica:
```
Timer → TorrentManager::update()
      → TorrentSession::processAlerts()
      → TorrentManager::syncTorrents()
      → Para cada TorrentItem::update()
      → Callback: onTorrentUpdated(item)
      → UI actualiza velocidades/progreso
```

### Configuración:
```
UI Settings Dialog → SettingsManager::setMaxDownloadRate(500)
                   → SettingsManager::save()
                   → Archivo INI actualizado
```

---

## 🎯 Ventajas de esta Arquitectura

1. **Separación de Responsabilidades:** Cada clase tiene un propósito claro
2. **Testeable:** Cada componente puede ser testeado independientemente
3. **Extensible:** Fácil añadir nuevas funcionalidades
4. **Callbacks:** La UI reacciona a cambios sin polling constante
5. **Configuración Persistente:** Los settings sobreviven reinicios
6. **Abstracción:** La UI no necesita conocer detalles de libtorrent

---

## 🚀 Próximos Pasos

### Componentes UI (Fase 3):
- `MainWindow` - Ventana principal
- `TorrentList` - Widget tabla de torrents
- `TorrentDetailsPanel` - Panel de detalles
- `AddTorrentDialog` - Diálogo para añadir torrents
- `SettingsDialog` - Diálogo de configuración

### Funcionalidades Adicionales (Fase 4):
- Sistema de categorías/etiquetas
- Filtros de torrents
- Búsqueda integrada
- Sistema de RSS feeds
- Planificador de velocidad
- Soporte para trackers privados

---

## 📝 Notas de Implementación

- **Thread Safety:** TorrentManager debe ser thread-safe para callbacks desde libtorrent
- **Actualización UI:** Los callbacks se ejecutan desde el thread de libtorrent, la UI debe sincronizarse
- **Rendimiento:** TorrentItem cachea datos para evitar llamadas constantes a libtorrent
- **Memoria:** Uso de unique_ptr para gestión automática de memoria
