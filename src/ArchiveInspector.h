#ifndef ARCHIVEINSPECTOR_H
#define ARCHIVEINSPECTOR_H

#include <atomic>
#include <cstdint>
#include <string>
#include <vector>

/**
 * @brief Read-only access to the executables inside an archive.
 *
 * The reader never writes an archive entry to disk and never executes one.
 * An entry selected by the user is decompressed into a fixed-size buffer and
 * fed to SHA-256 as it arrives, so a path like "../../evil.exe" has nowhere to
 * go: it is only a display string.
 *
 * libarchive is an optional build dependency. Without it isAvailable() is
 * false and the calls below report a clear error instead of pretending RAR
 * support exists.
 */
class ArchiveInspector {
public:
    struct Entry {
        int index = 0;          // Position in the archive, for a second pass
        std::string path;       // Path as stored in the archive, display only
        std::uint64_t size = 0;
        bool sizeKnown = false;
        bool encrypted = false;
    };

    struct Listing {
        bool ok = false;
        std::string error;
        std::vector<Entry> executables;
    };

    static bool isAvailable();

    /** @brief True for the archive formats the UI offers to inspect. */
    static bool isArchivePath(const std::string& path);

    /**
     * @brief List regular .exe entries without decompressing their data.
     *
     * @param archivePath Archive on disk.
     * @param cancelled   Optional stop flag.
     */
    static Listing listExecutables(const std::string& archivePath,
                                   const std::atomic<bool>* cancelled = nullptr);

    /**
     * @brief Hash one .exe entry by its position in the archive.
     *
     * The entry is decompressed in memory, chunk by chunk. No temporary file
     * is created. Password-protected entries and entries over the safety limit
     * are rejected.
     *
     * @param entryIndex  Entry::index returned by listExecutables().
     * @param digestHex   Receives 64 lowercase hex characters on success.
     * @param error       Receives a human-readable reason on failure.
     */
    static bool hashExecutable(const std::string& archivePath,
                               int entryIndex,
                               const std::atomic<bool>* cancelled,
                               std::string& digestHex,
                               std::string& error);

private:
    static bool isExecutableName(const std::string& path);
    static std::string extensionLower(const std::string& path);
};

#endif // ARCHIVEINSPECTOR_H
