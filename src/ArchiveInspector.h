#ifndef ARCHIVEINSPECTOR_H
#define ARCHIVEINSPECTOR_H

#include <atomic>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

/**
 * @brief Read-only listing of what an archive contains.
 *
 * The reader never writes an archive entry to disk and never executes one: a
 * path like "../../evil.exe" is only a display string. It is what backs the
 * pre-download check, which reads names, sizes and the encrypted flag from the
 * archive's headers without downloading the archive.
 *
 * libarchive is an optional build dependency. Without it isAvailable() is false
 * and the calls below report a clear error instead of pretending RAR support
 * exists.
 *
 * This class only answers "what names does this format's index expose?". The
 * rule for "can this file run?" lives in SecurityAnalyzer, so there is one
 * answer to that question in the whole program.
 */
class ArchiveInspector {
public:
    struct Entry {
        int index = 0;          // Position in the archive
        std::string path;       // Path as stored in the archive, display only
        std::uint64_t size = 0;
        bool sizeKnown = false;
        bool encrypted = false;
    };

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

    /**
     * @brief True when this format keeps its index where the check can read it.
     *
     * ZIP and RAR are the formats the pre-download check understands. This is
     * deliberately narrower than SecurityAnalyzer::isArchivePath(), which
     * classifies every archive extension for the risk analysis: a `.7z` is an
     * archive, but its index cannot be listed before downloading it yet.
     */
    static bool canListBeforeDownload(const std::string& path);

    /**
     * @brief List every regular file of an archive on disk.
     *
     * The archive's data is skipped, never decompressed.
     */
    static ListingAll listEntries(const std::string& archivePath,
                                  const std::atomic<bool>* cancelled = nullptr);

    // Asks for a byte range the two windows do not cover. The pre-download
    // check backs this with the pieces of the torrent, so a RAR 1.5-4.x archive
    // can be walked header by header: libarchive skips the compressed data and
    // only the pieces holding a header are ever fetched. Returning false with a
    // non-empty 'error' stops the listing and that error is reported.
    using RangeFetcher = std::function<bool(
        std::int64_t offset, std::size_t length, std::string& out,
        std::string& error)>;

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
    static ListingAll listEntriesFromWindows(
        std::int64_t fileSize, const std::string& head, const std::string& tail,
        const std::atomic<bool>* cancelled = nullptr,
        RangeFetcher fetch = {});

private:
    static std::string extensionLower(const std::string& path);

    static bool readEntries(const std::string& archivePath,
                            std::vector<Entry>& out, std::string& error,
                            const std::atomic<bool>* cancelled);
};

#endif // ARCHIVEINSPECTOR_H
