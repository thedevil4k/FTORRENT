#ifndef ARCHIVEINSPECTOR_H
#define ARCHIVEINSPECTOR_H

#include <atomic>
#include <cstdint>
#include <functional>
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

    // Every regular file the index exposes, not only the executables. Used by
    // the pre-download archive check, which analyzes all the names.
    struct ListingAll {
        bool ok = false;
        // True when the archive could not be opened at all because its headers
        // are encrypted. Nothing can be listed then, but the user must be told
        // the archive is password-protected rather than broken.
        bool encryptedHeaders = false;
        std::string error;
        std::vector<Entry> entries;
    };

    static bool isAvailable();

    /** @brief True for the archive formats the pre-download analysis accepts. */
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
     * @brief List every regular file without decompressing any data.
     *
     * Works on a file that only carries the archive's index, which is what the
     * first-plus-last-piece preflight produces: the entries are read from the
     * headers, and their data is skipped as usual.
     */
    static ListingAll listEntries(const std::string& archivePath,
                                  const std::atomic<bool>* cancelled = nullptr);

    /**
     * @brief List every regular file from the bytes of an archive the caller
     *        already holds or can fetch on demand.
     *
     * The archive never touches the disk: 'head' is served at offset 0, 'tail'
     * at the end of a file of 'fileSize' bytes, and the gap between them reads
     * as zeros unless a 'fetch' callback is supplied. With one, libarchive can
     * walk a RAR 1.5-4.x header chain and ask only for the pieces that hold a
     * header, while skipping the packed data. That also avoids writing a file
     * of the real size first (on NTFS extending a normal file zero-fills the
     * gap, so a 21 GB RAR meant 21 GB of writes).
     */
    // Asks for a byte range the windows do not cover. The pre-download check
    // backs this with the pieces of the torrent, so a RAR 1.5-4.x archive can
    // be walked header by header: libarchive skips the compressed data and only
    // the pieces holding a header are ever fetched. Returning false with a
    // non-empty 'error' stops the listing and that error is reported.
    using RangeFetcher = std::function<bool(
        std::int64_t offset, std::size_t length, std::string& out,
        std::string& error)>;

    static ListingAll listEntriesFromWindows(
        std::int64_t fileSize, const std::string& head, const std::string& tail,
        const std::atomic<bool>* cancelled = nullptr,
        RangeFetcher fetch = {});

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

    // One loop, two filters: .exe only, or every regular file.
    static bool readEntries(const std::string& archivePath,
                            bool executablesOnly,
                            std::vector<Entry>& out, std::string& error,
                            const std::atomic<bool>* cancelled);
};

#endif // ARCHIVEINSPECTOR_H
