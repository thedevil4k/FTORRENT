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
constexpr size_t MAX_EXECUTABLES = 10000;
constexpr std::uint64_t MAX_HASH_BYTES = 4ULL * 1024 * 1024 * 1024;

std::filesystem::path pathFromUtf8(const std::string& text)
{
#if defined(__cpp_char8_t)
    return std::filesystem::path(
        std::u8string(reinterpret_cast<const char8_t*>(text.data()), text.size()));
#else
    return std::filesystem::u8path(text);
#endif
}

struct ArchiveCloser {
    void operator()(archive* value) const
    {
        if (value) archive_read_free(value);
    }
};

using ArchivePtr = std::unique_ptr<archive, ArchiveCloser>;

std::string archiveError(archive* value, const char* fallback)
{
    const char* message = value ? archive_error_string(value) : nullptr;
    return message ? std::string(message) : std::string(fallback);
}

bool openArchive(archive* value, const std::string& path, std::string& error)
{
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
    const int status = archive_read_open_filename(value, path.c_str(), 64 * 1024);
#endif
    if (status != ARCHIVE_OK) {
        error = archiveError(value, "could not open archive");
        return false;
    }
    return true;
}

std::string entryName(archive_entry* entry)
{
    const char* name = archive_entry_pathname_utf8(entry);
    if (!name) name = archive_entry_pathname(entry);
    return name ? std::string(name) : std::string();
}

// A hardlink entry carries no data of its own. Hashing it would produce the
// digest of an empty stream, which is not the file the user thinks they picked,
// so links are never offered as executables.
bool isRegularFileEntry(archive_entry* entry)
{
    return archive_entry_filetype(entry) == AE_IFREG &&
           archive_entry_hardlink(entry) == nullptr;
}

bool isEncrypted(archive_entry* entry)
{
#if ARCHIVE_VERSION_NUMBER >= 3004000
    return archive_entry_is_encrypted(entry) != 0;
#else
    (void)entry;
    return false;
#endif
}

#endif // FTORRENT_HAVE_LIBARCHIVE

} // namespace

bool ArchiveInspector::isAvailable()
{
#ifdef FTORRENT_HAVE_LIBARCHIVE
    return true;
#else
    return false;
#endif
}

std::string ArchiveInspector::extensionLower(const std::string& path)
{
    const size_t separator = path.find_last_of("/\\");
    const size_t dot = path.find_last_of('.');
    if (dot == std::string::npos ||
        (separator != std::string::npos && dot < separator)) {
        return {};
    }

    std::string extension = path.substr(dot);
    std::transform(extension.begin(), extension.end(), extension.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return extension;
}

bool ArchiveInspector::isArchivePath(const std::string& path)
{
    const std::string extension = extensionLower(path);
    return extension == ".rar" || extension == ".zip";
}

bool ArchiveInspector::isExecutableName(const std::string& path)
{
    return extensionLower(path) == ".exe";
}

ArchiveInspector::Listing ArchiveInspector::listExecutables(
    const std::string& archivePath,
    const std::atomic<bool>* cancelled)
{
    Listing result;

#ifndef FTORRENT_HAVE_LIBARCHIVE
    (void)archivePath;
    (void)cancelled;
    result.error = "archive support is not available in this build";
    return result;
#else
    if (archivePath.empty()) {
        result.error = "empty archive path";
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
    if (!openArchive(archive.get(), archivePath, result.error)) {
        return result;
    }

    archive_entry* entry = nullptr;
    int entryIndex = 0;
    int status = ARCHIVE_OK;

    while ((status = archive_read_next_header(archive.get(), &entry)) == ARCHIVE_OK) {
        ++entryIndex;

        if (cancelled && cancelled->load()) {
            result.error = "cancelled";
            break;
        }
        if (entryIndex > MAX_ENTRIES) {
            result.error = "archive has too many entries";
            break;
        }

        if (isRegularFileEntry(entry)) {
            const std::string name = entryName(entry);
            if (!name.empty() && isExecutableName(name)) {
                Entry executable;
                executable.index = entryIndex;
                executable.path = name;

                const la_int64_t size = archive_entry_size(entry);
                if (archive_entry_size_is_set(entry) && size >= 0) {
                    executable.size = static_cast<std::uint64_t>(size);
                    executable.sizeKnown = true;
                }
                executable.encrypted = isEncrypted(entry);
                result.executables.push_back(std::move(executable));

                if (result.executables.size() > MAX_EXECUTABLES) {
                    result.error = "archive has too many .exe files";
                    break;
                }
            }
        }

        if (archive_read_data_skip(archive.get()) != ARCHIVE_OK) {
            result.error = archiveError(archive.get(), "could not skip archive data");
            break;
        }
    }

    if (result.error.empty() && status != ARCHIVE_EOF) {
        result.error = archiveError(archive.get(), "could not read archive");
    }

    result.ok = result.error.empty();
    return result;
#endif
}

bool ArchiveInspector::hashExecutable(const std::string& archivePath,
                                      int entryIndex,
                                      const std::atomic<bool>* cancelled,
                                      std::string& digestHex,
                                      std::string& error)
{
    digestHex.clear();
    error.clear();

#ifndef FTORRENT_HAVE_LIBARCHIVE
    (void)archivePath;
    (void)entryIndex;
    (void)cancelled;
    error = "archive support is not available in this build";
    return false;
#else
    if (archivePath.empty()) {
        error = "empty archive path";
        return false;
    }
    if (entryIndex <= 0) {
        error = "invalid archive entry";
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

    archive_entry* entry = nullptr;
    int currentIndex = 0;
    int status = ARCHIVE_OK;
    bool found = false;

    while ((status = archive_read_next_header(archive.get(), &entry)) == ARCHIVE_OK) {
        ++currentIndex;

        if (cancelled && cancelled->load()) {
            error = "cancelled";
            return false;
        }
        if (currentIndex > MAX_ENTRIES) {
            error = "archive has too many entries";
            return false;
        }
        if (currentIndex != entryIndex) {
            if (archive_read_data_skip(archive.get()) != ARCHIVE_OK) {
                error = archiveError(archive.get(), "could not skip archive data");
                return false;
            }
            continue;
        }

        found = true;

        if (archive_entry_filetype(entry) != AE_IFREG) {
            error = "the selected entry is not a regular file";
            return false;
        }
        if (archive_entry_hardlink(entry) != nullptr) {
            error = "the selected entry is a link, not a file with its own data";
            return false;
        }
        if (!isExecutableName(entryName(entry))) {
            error = "the selected entry is not an .exe file";
            return false;
        }
        if (isEncrypted(entry)) {
            error = "the selected entry is password-protected";
            return false;
        }

        const la_int64_t declaredSize = archive_entry_size(entry);
        const bool declaredSizeKnown =
            archive_entry_size_is_set(entry) && declaredSize >= 0;
        if (declaredSizeKnown &&
            static_cast<std::uint64_t>(declaredSize) > MAX_HASH_BYTES) {
            error = "the selected .exe exceeds the hash size limit";
            return false;
        }

        FileHash::Sha256 hasher;
        char buffer[64 * 1024];
        std::uint64_t total = 0;
        la_ssize_t got = 0;

        while ((got = archive_read_data(archive.get(), buffer, sizeof(buffer))) > 0) {
            if (cancelled && cancelled->load()) {
                error = "cancelled";
                return false;
            }

            total += static_cast<std::uint64_t>(got);
            if (total > MAX_HASH_BYTES) {
                error = "the selected .exe exceeds the hash size limit";
                return false;
            }
            hasher.update(reinterpret_cast<const unsigned char*>(buffer),
                          static_cast<size_t>(got));
        }

        if (got < 0) {
            error = archiveError(archive.get(), "could not decompress the archive entry");
            return false;
        }

        // A header is only a promise. If the stream ends early, the digest
        // would describe a truncated file, not the program the header names.
        if (declaredSizeKnown &&
            total < static_cast<std::uint64_t>(declaredSize)) {
            error = "the selected .exe ended before its declared size";
            return false;
        }

        digestHex = hasher.finalHex();
        return true;
    }

    if (status != ARCHIVE_EOF && error.empty()) {
        error = archiveError(archive.get(), "could not read archive");
    }
    if (error.empty() && !found) {
        error = "the archive entry was not found";
    }
    return false;
#endif
}
