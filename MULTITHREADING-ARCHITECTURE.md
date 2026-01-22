# FTorrent Multithreading Architecture

## Overview

FTorrent implements a **multi-core, multi-threaded architecture** designed for stability, speed, and non-blocking performance. The design separates concerns between UI rendering, torrent management, and I/O operations across multiple threads.

## Architecture Diagram

```
┌─────────────────────────────────────────────────────────────────┐
│                          UI THREAD (Main)                        │
│  ┌──────────────┐  ┌──────────────┐  ┌──────────────┐          │
│  │  MainWindow  │──│ TorrentList  │──│  Dialogs     │          │
│  └──────┬───────┘  └──────────────┘  └──────────────┘          │
│         │                                                         │
│         │ Thread-safe callbacks                                  │
└─────────┼─────────────────────────────────────────────────────────┘
          │
          ▼
┌─────────────────────────────────────────────────────────────────┐
│              TORRENT MANAGER (Thread-Safe Facade)                │
│  ┌──────────────────────────────────────────────────────────┐   │
│  │  Command Queue (Mutex-Protected)                         │   │
│  │  - Add torrent commands                                  │   │
│  │  - Remove torrent commands                               │   │
│  │  - Pause/Resume commands                                 │   │
│  └──────────────────────────────────────────────────────────┘   │
│  ┌──────────────────────────────────────────────────────────┐   │
│  │  Data Storage (Mutex-Protected)                          │   │
│  │  - Torrent list (m_torrents)                             │   │
│  │  - Callbacks (m_callbacks*)                              │   │
│  └──────────────────────────────────────────────────────────┘   │
└─────┬────────────────────────┬────────────────────────┬──────────┘
      │                        │                        │
      ▼                        ▼                        ▼
┌─────────────┐      ┌──────────────────┐    ┌──────────────────┐
│   WORKER    │      │   I/O THREAD     │    │  LIBTORRENT      │
│   THREAD    │      │   POOL           │    │  INTERNAL        │
│             │      │   (2-8 threads)  │    │  THREADS         │
│ - Alert     │      │                  │    │                  │
│   processing│      │ - File ops       │    │ - Network I/O    │
│ - Torrent   │      │ - Torrent        │    │ - Disk I/O       │
│   sync      │      │   creation       │    │ - DHT            │
│ - 100ms     │      │ - Verification   │    │ - Tracker comms  │
│   interval  │      │                  │    │                  │
└─────────────┘      └──────────────────┘    └──────────────────┘
```

## Threading Model

### 1. **UI Thread (Main Thread)**
- **Responsibility**: FLTK event loop, rendering, user interaction
- **Operations**:
  - Handle user input (button clicks, menu selections)
  - Render UI components
  - Call TorrentManager methods (all thread-safe)
  - Receive callbacks from TorrentManager (thread-safe)
- **Blocking**: Never blocked by backend operations

### 2. **Worker Thread**
- **Responsibility**: Libtorrent alert processing and torrent synchronization
- **Operations**:
  - Process libtorrent alerts every 100ms
  - Sync torrent list with libtorrent session
  - Detect new/removed torrents
  - Update torrent states
- **Lifecycle**: Started on `initialize()`, stopped on `shutdown()`
- **Thread-Safe**: All operations protected by `m_torrentsMutex`

### 3. **I/O Thread Pool**
- **Responsibility**: Heavy I/O operations that would block the UI
- **Size**: 2-8 threads (based on `std::thread::hardware_concurrency()`)
- **Future Use**:
  - Torrent file creation
  - File verification
  - Large file I/O operations
  - Metadata processing
- **Note**: Currently reserved for future enhancements

### 4. **Libtorrent Internal Threads**
- **Responsibility**: Managed entirely by libtorrent library
- **Operations**:
  - Network socket I/O
  - Disk read/write
  - DHT operations
  - Tracker communication
  - Peer connection management

## Synchronization Strategy

### Mutexes

1. **`m_torrentsMutex`** (Torrent List Protection)
   - Protects: `m_torrents` vector
   - Used by: All methods that read/write torrent list
   - Lock Order: Always acquired first if multiple mutexes needed

2. **`m_callbacksMutex`** (Callback Protection)
   - Protects: All callback function pointers
   - Used by: Callback setters and invocations
   - Prevents race conditions during callback updates

3. **`m_commandMutex`** (Command Queue Protection)
   - Protects: `m_commandQueue`
   - Used by: Async operations (future use)
   - Works with `m_commandCV` condition variable

### Atomic Variables

1. **`m_initialized`**
   - Type: `std::atomic<bool>`
   - Purpose: Fast lock-free check if manager is ready
   - Read by: All threads, written by: UI thread only

2. **`m_running`**
   - Type: `std::atomic<bool>`
   - Purpose: Signal worker thread to stop
   - Read by: Worker thread, written by: UI thread

### Safe Patterns

#### Pattern 1: Query Operations
```cpp
std::vector<TorrentItem*> TorrentManager::getAllTorrents() {
    std::lock_guard<std::mutex> lock(m_torrentsMutex);
    // Safe to access m_torrents here
    return result;
}
```

#### Pattern 2: Modification Operations
```cpp
void TorrentManager::removeTorrent(const std::string& hash, bool deleteFiles) {
    std::lock_guard<std::mutex> lock(m_torrentsMutex);
    // Modify m_torrents safely
    m_torrents.erase(it);
}
```

#### Pattern 3: Callback Invocation
```cpp
void TorrentManager::notifyTorrentAdded(TorrentItem* item) {
    std::lock_guard<std::mutex> lock(m_callbacksMutex);
    if (m_onTorrentAdded) {
        m_onTorrentAdded(item);
    }
}
```

## Performance Optimizations

### 1. Lock Granularity
- **Fine-grained locking**: Separate mutexes for different data structures
- **Short critical sections**: Locks held for minimal time
- **Try-lock in update()**: Skips update if queue is busy (non-blocking)

### 2. Atomic Operations
- Fast boolean checks without mutex overhead
- `m_initialized.load()` and `m_running.load()` are lock-free

### 3. Worker Thread Interval
- 100ms update interval balances responsiveness and CPU usage
- Prevents busy-waiting with `std::this_thread::sleep_for()`

### 4. Batch Updates
- `syncTorrents()` processes all changes in one pass
- reduces number of callbacks and UI updates

### 5. Copy-on-Read for UI
- `getAllTorrents()` returns a copy, not references
- UI can safely iterate without holding locks

## Async API (Future Enhancement)

The architecture supports asynchronous operations via `std::future`:

```cpp
// Non-blocking torrent addition
auto future = manager->addTorrentFileAsync(path, savePath);

// UI remains responsive, check later
if (future.wait_for(0) == std::future_status::ready) {
    bool success = future.get();
}
```

**Benefits**:
- UI never blocks on I/O
- Progress indicators possible
- Cancel operations (future extension)

## Thread Safety Guarantees

### ✅ **Safe Operations** (Can be called from any thread)
- `addTorrentFile()` / `addMagnetLink()`
- `removeTorrent()`, `pauseTorrent()`, `resumeTorrent()`
- `getTorrent()`, `getAllTorrents()`, `getTorrentCount()`
- `getTotalDownloadRate()`, `getTotalUploadRate()`
- All callback setters

### ⚠️ **UI Thread Only** (FLTK requirement)
- `setOnTorrent*()` callbacks **invocation** must happen in UI thread
- Use `Fl::awake()` or `Fl::lock()`/`Fl::unlock()` if calling from worker threads

### 🔒 **Internal Only** (Called with mutex held)
- `findTorrent()` - Requires `m_torrentsMutex`
- `syncTorrents()` - Requires `m_torrentsMutex`

## Best Practices

### 1. Always Use Lock Guards
```cpp
// ❌ BAD
m_torrentsMutex.lock();
// ... risky code ...
m_torrentsMutex.unlock();

// ✅ GOOD
std::lock_guard<std::mutex> lock(m_torrentsMutex);
// ... exception-safe ...
```

### 2. Avoid Nested Locks
```cpp
// ❌ BAD (deadlock risk)
lock(m_torrentsMutex);
lock(m_callbacksMutex);

// ✅ GOOD (single lock per operation)
{
    lock(m_torrentsMutex);
    // ...
}
{
    lock(m_callbacksMutex);
    // ...
}
```

### 3. Keep Callbacks Fast
```cpp
// Callbacks execute in worker thread context
manager->setOnTorrentAdded([](TorrentItem* item) {
    // ✅ Fast: Update UI (FLTK is mostly thread-safe with Fl::awake)
    Fl::awake([item]() { /* update UI */ });
    
    // ❌ Slow: Don't do heavy work here
    // processLargeDataset(item);
});
```

## Scalability

### Current Limits
- **Torrents**: Thousands (limited by available memory)
- **Worker Thread**: Single thread, 100ms interval
- **I/O Threads**: 2-8 threads (reserved for future)

### Future Enhancements
1. **Multi-level command queue** for priority operations
2. **Thread pool for torrent creation** (currently synchronous)
3. **Background file verification** (parallel processing)
4. **Async disk I/O** for large file operations

## Debugging

### Logging
- Worker thread logs: "Worker thread started/stopped"
- Initialization logs: "TorrentManager initialized with N I/O threads"
- Error logs: All errors printed to `std::cerr`

### Deadlock Prevention
- **Lock ordering**: Always acquire mutexes in the same order
- **Timeout locks**: Use `std::try_to_lock` for non-critical sections
- **No recursive locks**: Avoid `std::recursive_mutex` (complexity)

### Race Condition Testing
1. Run with ThreadSanitizer (TSan): `g++ -fsanitize=thread`
2. Use Helgrind (Valgrind): `valgrind --tool=helgrind`
3. Stress test: Add/remove many torrents rapidly

## Conclusion

FTorrent's multithreading architecture provides:
- ✅ **Non-blocking UI**: All heavy operations run in background
- ✅ **Thread-safe**: Mutexes protect all shared data
- ✅ **Scalable**: Multi-core utilization for I/O-bound tasks
- ✅ **Stable**: Exception-safe RAII locks, atomic flags
- ✅ **Fast**: Lock-free reads, minimal critical sections

The design balances **performance**, **safety**, and **maintainability** for a responsive BitTorrent client.
