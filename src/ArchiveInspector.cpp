#include "ArchiveInspector.h"

#include "FileHash.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <memory>

#ifdef FTORRENT_HAVE_LIBARCHIVE
#include <archive.h>
#include <archive_entry.h>
#endif

namespace {

#ifdef FTORRENT_HAVE_LIBARCHIVE

// A pathological archive must not turn a click into an unbounded scan or an
// unbounded decompression. These limits are deliberately far above normal
// installers; they exist to stop a hostile archive, not to judge a big game.
constexpr int MAX_ENTRIES = 100000;

std::filesystem::path pathFromUtf8(const std::string& text) {
#if defined(__cpp_char8_t)
    return std::filesystem::path(std::u8string(
        reinterpret_cast<const char8_t*>(text.data()), text.size()));
#else
    return std::filesystem::u8path(text);
#endif
}

struct ArchiveCloser {
    void operator()(archive* value) const {
        if (value) archive_read_free(value);
    }
};

using ArchivePtr = std::unique_ptr<archive, ArchiveCloser>;

std::string archiveError(archive* value, const char* fallback) {
    const char* message = value ? archive_error_string(value) : nullptr;
    return message ? std::string(message) : std::string(fallback);
}

bool openArchive(archive* value, const std::string& path, std::string& error) {
    // Only the uncompressed stream is needed: ZIP and RAR do their compression
    // inside the format reader. Registering every filter would also make
    // libarchive consider external helper programs for no benefit.
    archive_read_support_filter_none(value);
    archive_read_support_format_rar(value);
#if ARCHIVE_VERSION_NUMBER >= 3004000
    archive_read_support_format_rar5(value);
#endif
    archive_read_support_format_zip(value);

#ifdef _WIN32
    const int status = archive_read_open_filename_w(
        value, pathFromUtf8(path).c_str(), 64 * 1024);
#else
    const int status =
        archive_read_open_filename(value, path.c_str(), 64 * 1024);
#endif
    if (status != ARCHIVE_OK) {
        error = archiveError(value, "could not open archive");
        return false;
    }
    return true;
}

// An archive that exists only as the bytes the pre-download check fetched: the
// first and last window of a file of a known size. Between them the stream
// reads as zeros, which is what the file looks like to a header walk, and no
// part of it is ever written anywhere. On NTFS the alternative -- a file of the
// real size with a hole -- is not free: extending a normal file zero-fills the
// gap, so listing a 21 GB RAR meant writing 21 GB first.
struct WindowStream {
    std::int64_t size = 0;
    std::string head; // At offset 0
    std::string tail; // At offset size - tail.size()
    std::int64_t pos = 0;
    char zeros[64 * 1024] = {};
    // When set, bytes outside the two windows are fetched on demand instead of
    // read as zeros. This is what lets libarchive walk a RAR 1.5-4.x archive:
    // it skips the packed data and asks only for the next header.
    const ArchiveInspector::RangeFetcher* fetch = nullptr;
    std::string scratch;
    std::string error;
};

la_ssize_t windowRead(archive*, void* client, const void** buffer) {
    WindowStream* stream = static_cast<WindowStream*>(client);
    if (stream->size <= 0 || stream->pos >= stream->size) return 0;

    const std::int64_t remaining = stream->size - stream->pos;
    const std::int64_t headSize =
        static_cast<std::int64_t>(stream->head.size());
    if (stream->pos < headSize) {
        const std::int64_t available = headSize - stream->pos;
        const la_ssize_t take = static_cast<la_ssize_t>(
            available < remaining ? available : remaining);
        *buffer = stream->head.data() + stream->pos;
        stream->pos += take;
        return take;
    }

    const std::int64_t tailSize =
        static_cast<std::int64_t>(stream->tail.size());
    const std::int64_t tailStart = stream->size - tailSize;
    if (stream->pos >= tailStart) {
        const std::int64_t available = stream->size - stream->pos;
        const la_ssize_t take = static_cast<la_ssize_t>(available);
        *buffer = stream->tail.data() + (stream->pos - tailStart);
        stream->pos += take;
        return take;
    }

    // The gap: fetch it when the caller can, otherwise hand out zeros. The
    // two-window check has no fetcher and relies on the zeros; a RAR 1.5-4.x
    // walk has one and only pays for the pieces holding a header.
    if (stream->fetch && *stream->fetch) {
        const std::int64_t gap = tailStart - stream->pos;
        const std::int64_t take = gap < static_cast<std::int64_t>(64 * 1024)
                                      ? gap
                                      : static_cast<std::int64_t>(64 * 1024);
        std::string out;
        std::string error;
        if (!(*stream->fetch)(stream->pos, static_cast<std::size_t>(take), out,
                              error) ||
            out.empty()) {
            stream->error = error.empty()
                                ? "the archive bytes could not be fetched"
                                : error;
            return -1;
        }
        if (static_cast<std::int64_t>(out.size()) > take) {
            out.resize(static_cast<std::size_t>(take));
        }
        stream->scratch = std::move(out);
        *buffer = stream->scratch.data();
        stream->pos += static_cast<std::int64_t>(stream->scratch.size());
        return static_cast<la_ssize_t>(stream->scratch.size());
    }

    const std::int64_t gap = tailStart - stream->pos;
    const la_ssize_t take = static_cast<la_ssize_t>(
        gap < static_cast<std::int64_t>(sizeof(stream->zeros))
            ? gap
            : static_cast<std::int64_t>(sizeof(stream->zeros)));
    *buffer = stream->zeros;
    stream->pos += take;
    return take;
}

la_int64_t windowSkip(archive*, void* client, la_int64_t request) {
    WindowStream* stream = static_cast<WindowStream*>(client);
    if (request <= 0) return 0;
    const std::int64_t remaining = stream->size - stream->pos;
    const std::int64_t take = request < remaining ? request : remaining;
    stream->pos += take > 0 ? take : 0;
    return take;
}

la_int64_t windowSeek(archive*, void* client, la_int64_t offset, int whence) {
    WindowStream* stream = static_cast<WindowStream*>(client);
    std::int64_t target = offset;
    if (whence == SEEK_CUR) target = stream->pos + offset;
    else if (whence == SEEK_END) target = stream->size + offset;
    if (target < 0) target = 0;
    if (target > stream->size) target = stream->size;
    stream->pos = target;
    return stream->pos;
}

std::string entryName(archive_entry* entry) {
    const char* name = archive_entry_pathname_utf8(entry);
    if (!name) name = archive_entry_pathname(entry);
    return name ? std::string(name) : std::string();
}

// A hardlink entry carries no data of its own. Hashing it would produce the
// digest of an empty stream, which is not the file the user thinks they picked,
// so links are never offered as executables.
bool isRegularFileEntry(archive_entry* entry) {
    return archive_entry_filetype(entry) == AE_IFREG &&
           archive_entry_hardlink(entry) == nullptr;
}

bool isEncrypted(archive_entry* entry) {
#if ARCHIVE_VERSION_NUMBER >= 3004000
    return archive_entry_is_encrypted(entry) != 0;
#else
    (void)entry;
    return false;
#endif
}

// libarchive's own words for "these headers need a password": RAR says "RAR
// encryption support unavailable", ZIP says "Passphrase required". Only used
// when nothing at all could be listed, so it can never turn a readable archive
// into an encrypted one.
bool looksEncrypted(const std::string& message) {
    std::string lower = message;
    std::transform(
        lower.begin(), lower.end(), lower.begin(),
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return lower.find("encrypt") != std::string::npos ||
           lower.find("passphrase") != std::string::npos ||
           lower.find("password") != std::string::npos;
}

// The .exe rule lived here once, as a second, narrower answer next to
// SecurityAnalyzer's. The pre-download check lists every name and lets the
// analyzer decide what can run, so the reader no longer carries a predicate.

// The header walk, shared by a file on disk and by the two-window stream. The
// reader must already be open.
bool readOpenArchive(archive* reader, std::vector<ArchiveInspector::Entry>& out,
                     std::string& error, const std::atomic<bool>* cancelled) {
    archive_entry* entry = nullptr;
    int entryIndex = 0;
    int status = ARCHIVE_OK;

    while ((status = archive_read_next_header(reader, &entry)) == ARCHIVE_OK) {
        ++entryIndex;

        if (cancelled && cancelled->load()) {
            error = "cancelled";
            break;
        }
        if (entryIndex > MAX_ENTRIES) {
            error = "archive has too many entries";
            break;
        }

        if (isRegularFileEntry(entry)) {
            const std::string name = entryName(entry);
            if (!name.empty()) {
                ArchiveInspector::Entry found;
                found.index = entryIndex;
                found.path = name;

                const la_int64_t size = archive_entry_size(entry);
                if (archive_entry_size_is_set(entry) && size >= 0) {
                    found.size = static_cast<std::uint64_t>(size);
                    found.sizeKnown = true;
                }
                found.encrypted = isEncrypted(entry);
                out.push_back(std::move(found));
            }
        }

        if (archive_read_data_skip(reader) != ARCHIVE_OK) {
            error = archiveError(reader, "could not skip archive data");
            break;
        }
    }

    if (error.empty() && status != ARCHIVE_EOF) {
        error = archiveError(reader, "could not read archive");
    }
    return error.empty();
}

#endif // FTORRENT_HAVE_LIBARCHIVE

} // namespace

bool ArchiveInspector::isAvailable() {
#ifdef FTORRENT_HAVE_LIBARCHIVE
    return true;
#else
    return false;
#endif
}

std::string ArchiveInspector::extensionLower(const std::string& path) {
    const size_t separator = path.find_last_of("/\\");
    const size_t dot = path.find_last_of('.');
    if (dot == std::string::npos ||
        (separator != std::string::npos && dot < separator)) {
        return {};
    }

    std::string extension = path.substr(dot);
    std::transform(
        extension.begin(), extension.end(), extension.begin(),
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return extension;
}

bool ArchiveInspector::canListBeforeDownload(const std::string& path) {
    const std::string extension = extensionLower(path);
    return extension == ".rar" || extension == ".zip";
}

// Shared by both listings: a file on disk and the two-window stream.
bool ArchiveInspector::readEntries(const std::string& archivePath,
                                   std::vector<Entry>& out, std::string& error,
                                   const std::atomic<bool>* cancelled) {
#ifndef FTORRENT_HAVE_LIBARCHIVE
    (void)archivePath;
    (void)out;
    (void)cancelled;
    error = "archive support is not available in this build";
    return false;
#else
    out.clear();
    if (archivePath.empty()) {
        error = "empty archive path";
        return false;
    }
    if (cancelled && cancelled->load()) {
        error = "cancelled";
        return false;
    }

    ArchivePtr archive(archive_read_new());
    if (!archive) {
        error = "could not create the archive reader";
        return false;
    }
    if (!openArchive(archive.get(), archivePath, error)) {
        return false;
    }
    return readOpenArchive(archive.get(), out, error, cancelled);
#endif
}

ArchiveInspector::ListingAll ArchiveInspector::listEntriesFromWindows(
    std::int64_t fileSize, const std::string& head, const std::string& tail,
    const std::atomic<bool>* cancelled, RangeFetcher fetch) {
    ListingAll result;
#ifndef FTORRENT_HAVE_LIBARCHIVE
    (void)fileSize;
    (void)head;
    (void)tail;
    (void)cancelled;
    (void)fetch;
    result.error = "archive support is not available in this build";
    return result;
#else
    if (fileSize <= 0) {
        result.error = "the file is empty";
        return result;
    }
    if (static_cast<std::int64_t>(head.size()) > fileSize ||
        static_cast<std::int64_t>(tail.size()) > fileSize) {
        result.error = "the fetched bytes do not fit the file";
        return result;
    }
    if (cancelled && cancelled->load()) {
        result.error = "cancelled";
        return result;
    }

    ArchivePtr archive(archive_read_new());
    if (!archive) {
        result.error = "could not create the archive reader";
        return result;
    }
    archive_read_support_filter_none(archive.get());
    archive_read_support_format_rar(archive.get());
#if ARCHIVE_VERSION_NUMBER >= 3004000
    archive_read_support_format_rar5(archive.get());
#endif
    archive_read_support_format_zip(archive.get());

    WindowStream stream;
    stream.size = fileSize;
    stream.head = head;
    stream.tail = tail;
    if (fetch) stream.fetch = &fetch;
    archive_read_set_seek_callback(archive.get(), &windowSeek);
    if (archive_read_open2(archive.get(), &stream, nullptr, &windowRead,
                           &windowSkip, nullptr) != ARCHIVE_OK) {
        result.error =
            stream.error.empty()
                ? archiveError(archive.get(), "could not open archive")
                : stream.error;
        return result;
    }
    if (!readOpenArchive(archive.get(), result.entries, result.error,
                         cancelled)) {
        // A failed fetch says more than libarchive's "truncated header": the
        // caller stopped it on purpose (a piece budget, a timeout) or the
        // network gave up, and that is the fact the user needs.
        if (!stream.error.empty()) result.error = stream.error;
        if (result.entries.empty() && looksEncrypted(result.error)) {
            result.encryptedHeaders = true;
        }
        return result;
    }
    // libarchive can also treat a failed read as the end of the archive and
    // report success with whatever it read so far. That happened on a real
    // 21 GB RAR 4 archive: 26 entries and a clean end, right after a piece
    // timed out. The fetch failure is the truth, so the listing is partial.
    if (!stream.error.empty()) {
        result.error = stream.error;
        return result;
    }
    if (result.entries.empty()) {
        // An archive whose headers are encrypted opens with no entries and no
        // error on some paths; libarchive's message is the only clue.
        const char* message = archive_error_string(archive.get());
        if (message && looksEncrypted(message)) {
            result.error = message;
            result.encryptedHeaders = true;
            return result;
        }
    }
    result.ok = true;
    return result;
#endif
}

ArchiveInspector::ListingAll
ArchiveInspector::listEntries(const std::string& archivePath,
                              const std::atomic<bool>* cancelled) {
    ListingAll result;
    if (!readEntries(archivePath, result.entries, result.error, cancelled)) {
        return result;
    }
    result.ok = true;
    return result;
}
