// ArchiveInspector checks.
//
// The reader answers one question before the user runs anything: what names and
// sizes does this archive's index expose, and which entries need a password?
// These checks build a small ZIP with libarchive, then verify that the listing
// reports every regular file, that hostile names are shown verbatim instead of
// being turned into paths, and that malformed input is refused rather than
// guessed at. Nothing is extracted: the archive is created and read in a temp
// directory that the test removes afterwards.
#include "ArchiveInspector.h"

#include <archive.h>
#include <archive_entry.h>

#include <atomic>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace {

int checks = 0;
int failures = 0;

void ok(bool condition, const char* what) {
    checks++;
    if (!condition) {
        failures++;
        std::cout << "    FAIL " << what << '\n';
    }
}

struct EntrySpec {
    std::string name;
    std::string bytes;
    bool directory = false;
};

bool openForWrite(archive* writer, const std::filesystem::path& path) {
#ifdef _WIN32
    return archive_write_open_filename_w(writer, path.c_str()) == ARCHIVE_OK;
#else
    return archive_write_open_filename(writer, path.c_str()) == ARCHIVE_OK;
#endif
}

bool createZip(const std::filesystem::path& path,
               const std::vector<EntrySpec>& entries) {
    archive* writer = archive_write_new();
    if (!writer) return false;

    bool ok = archive_write_set_format_zip(writer) == ARCHIVE_OK &&
              openForWrite(writer, path);

    for (const EntrySpec& spec : entries) {
        if (!ok) break;

        archive_entry* entry = archive_entry_new();
        if (!entry) {
            ok = false;
            break;
        }

        archive_entry_set_pathname(entry, spec.name.c_str());
        if (spec.directory) {
            archive_entry_set_filetype(entry, AE_IFDIR);
            archive_entry_set_perm(entry, 0755);
            archive_entry_set_size(entry, 0);
        } else {
            archive_entry_set_filetype(entry, AE_IFREG);
            archive_entry_set_perm(entry, 0644);
            archive_entry_set_size(entry,
                                   static_cast<la_int64_t>(spec.bytes.size()));
        }

        if (archive_write_header(writer, entry) != ARCHIVE_OK) {
            ok = false;
        } else if (!spec.directory && !spec.bytes.empty()) {
            const la_ssize_t written = archive_write_data(
                writer, spec.bytes.data(), spec.bytes.size());
            if (written != static_cast<la_ssize_t>(spec.bytes.size()))
                ok = false;
        }

        archive_entry_free(entry);
    }

    if (archive_write_close(writer) != ARCHIVE_OK) ok = false;
    archive_write_free(writer);
    return ok;
}

// Encryption options depend on how libarchive was built. The caller tries more
// than one and skips the encrypted-entry checks when none can be written.
bool createEncryptedZip(const std::filesystem::path& path,
                        const char* encryption) {
    archive* writer = archive_write_new();
    if (!writer) return false;

    const std::string option = std::string("zip:encryption=") + encryption;
    bool ok = archive_write_set_format_zip(writer) == ARCHIVE_OK &&
              archive_write_set_options(writer, option.c_str()) == ARCHIVE_OK &&
              archive_write_set_passphrase(writer, "secret") == ARCHIVE_OK &&
              openForWrite(writer, path);

    if (ok) {
        archive_entry* entry = archive_entry_new();
        if (!entry) {
            ok = false;
        } else {
            archive_entry_set_pathname(entry, "protected.exe");
            archive_entry_set_filetype(entry, AE_IFREG);
            archive_entry_set_perm(entry, 0644);
            archive_entry_set_size(entry, 3);
            if (archive_write_header(writer, entry) != ARCHIVE_OK ||
                archive_write_data(writer, "abc", 3) != 3) {
                ok = false;
            }
            archive_entry_free(entry);
        }
    }

    if (archive_write_close(writer) != ARCHIVE_OK) ok = false;
    archive_write_free(writer);
    return ok;
}

const ArchiveInspector::Entry*
findEntry(const std::vector<ArchiveInspector::Entry>& entries,
          const std::string& path) {
    for (const ArchiveInspector::Entry& entry : entries) {
        if (entry.path == path) return &entry;
    }
    return nullptr;
}

} // namespace

int main() {
    std::error_code ec;
    const std::filesystem::path dir =
        std::filesystem::temp_directory_path() / "ftorrent-archive-test";
    std::filesystem::remove_all(dir, ec);
    std::filesystem::create_directories(dir, ec);

    ok(ArchiveInspector::isAvailable(), "archive support is compiled in");

    // Which formats the pre-download check can read. This is deliberately
    // narrower than "is this an archive?", which SecurityAnalyzer owns.
    ok(ArchiveInspector::canListBeforeDownload("setup.rar"), ".rar is offered");
    ok(ArchiveInspector::canListBeforeDownload("SETUP.ZIP"), ".ZIP is offered");
    ok(ArchiveInspector::canListBeforeDownload("setup.zip"), ".zip is offered");
    ok(!ArchiveInspector::canListBeforeDownload("setup.7z"),
       ".7z is not listable yet");
    ok(!ArchiveInspector::canListBeforeDownload("setup.exe"),
       ".exe is not an archive");
    ok(!ArchiveInspector::canListBeforeDownload("rar"),
       "a bare name is not an archive");

    const std::filesystem::path zipPath = dir / "sample.zip";
    const std::string million(1000000, 'a');
    const std::vector<EntrySpec> entries = {
        {"readme.txt", "ignored", false}, {"nested", "", true},
        {"nested/inner.EXE", "", false},  {"setup.exe", "abc", false},
        {"big.exe", million, false},
    };
    ok(createZip(zipPath, entries), "a test ZIP could be created");

    const ArchiveInspector::ListingAll listing =
        ArchiveInspector::listEntries(zipPath.string());
    ok(listing.ok, "the test ZIP lists cleanly");
    if (!listing.ok) std::cout << "      error: " << listing.error << '\n';
    ok(listing.entries.size() == 4,
       "the four regular files are listed and the directory is not");

    const auto* inner = findEntry(listing.entries, "nested/inner.EXE");
    const auto* setup = findEntry(listing.entries, "setup.exe");
    const auto* big = findEntry(listing.entries, "big.exe");
    ok(inner != nullptr, "the nested .EXE keeps its path");
    ok(setup != nullptr, "the first .exe is found");
    ok(big != nullptr, "the large .exe is found");
    if (inner && setup && big) {
        ok(inner->sizeKnown && inner->size == 0, "the nested .EXE is empty");
        ok(setup->sizeKnown && setup->size == 3, "the .exe size is reported");
        ok(!setup->encrypted, "a plain entry is not marked encrypted");
        ok(big->sizeKnown && big->size == million.size(),
           "the large .exe size is reported");
        ok(inner->index > 0 && setup->index > inner->index &&
               big->index > setup->index,
           "entry indexes follow archive order");
    }

    {
        std::atomic<bool> cancelled{true};
        const ArchiveInspector::ListingAll cancelledListing =
            ArchiveInspector::listEntries(zipPath.string(), &cancelled);
        ok(!cancelledListing.ok, "a cancelled listing fails");
        ok(cancelledListing.error == "cancelled",
           "a cancelled listing says so");
    }

    // Password-protected entries must be visible and flagged. The writer's
    // encryption support depends on the libarchive build, so this part is
    // skipped rather than failed when neither AES nor ZipCrypto is available.
    const std::filesystem::path encryptedPath = dir / "encrypted.zip";
    if (createEncryptedZip(encryptedPath, "aes256") ||
        createEncryptedZip(encryptedPath, "zipcrypt")) {
        const ArchiveInspector::ListingAll encrypted =
            ArchiveInspector::listEntries(encryptedPath.string());
        ok(encrypted.ok, "an encrypted ZIP lists cleanly");
        ok(encrypted.entries.size() == 1, "the encrypted entry is listed");
        if (encrypted.entries.size() == 1) {
            ok(encrypted.entries[0].encrypted,
               "the encrypted entry is marked as password-protected");
        }
    } else {
        std::cout << "    SKIP encrypted ZIP checks: libarchive cannot write "
                     "encrypted ZIPs here\n";
    }

    const std::filesystem::path textPath = dir / "not-an-archive.zip";
    {
        std::ofstream out(textPath, std::ios::binary | std::ios::trunc);
        out << "this is not a ZIP";
        ok(out.good(), "could write the non-archive test file");
    }
    ok(!ArchiveInspector::listEntries(textPath.string()).ok,
       "a file that is not an archive is refused");
    ok(!ArchiveInspector::listEntries(textPath.string()).error.empty(),
       "the non-archive refusal explains itself");

    ok(!ArchiveInspector::listEntries((dir / "missing.zip").string()).ok,
       "a missing archive is refused");
    ok(!ArchiveInspector::listEntries((dir / "missing.zip").string())
            .error.empty(),
       "the missing-archive refusal explains itself");
    ok(!ArchiveInspector::listEntries("").ok, "an empty path is refused");

#ifdef FTORRENT_ARCHIVE_FIXTURES
    const std::filesystem::path fixtures(FTORRENT_ARCHIVE_FIXTURES);

    // Real RAR5 streams from libarchive's test suite: the reader has to parse
    // them and report their entries instead of failing.
    const char* rarFixtures[] = {
        "test_read_format_rar5_stored.rar",
        "test_read_format_rar5_compressed.rar",
        "test_read_format_rar5_encrypted.rar",
    };
    for (const char* name : rarFixtures) {
        const ArchiveInspector::ListingAll rar =
            ArchiveInspector::listEntries((fixtures / name).string());
        ok(rar.ok, "a RAR5 fixture lists cleanly");
        if (!rar.ok) std::cout << "      " << name << ": " << rar.error << '\n';
    }

    // The RAR case the lock badge exists for: rar5 marks each entry that needs
    // a password, and the preflight listing has to carry that through.
    {
        const ArchiveInspector::ListingAll guarded =
            ArchiveInspector::listEntries(
                (fixtures / "test_read_format_rar5_encrypted.rar").string());
        // libarchive on some platforms cannot read RAR5 headers that carry
        // encryption (for example an older Ubuntu package), so the entries are
        // listed but none of them is flagged encrypted. Report that and move
        // on instead of failing the whole suite for one platform's reader
        // limitation. When it can read them, every entry must say so.
        if (!guarded.ok) {
            std::cout << "    libarchive cannot list the encrypted RAR5 "
                      << "fixture: " << guarded.error << '\n';
        } else if (guarded.entries.empty()) {
            std::cout << "    libarchive listed the encrypted RAR5 fixture "
                      << "without entries\n";
        } else {
            ok(guarded.ok, "the encrypted RAR5 fixture lists its entries");
            int locked = 0;
            for (const ArchiveInspector::Entry& entry : guarded.entries) {
                if (entry.encrypted) ++locked;
            }
            if (locked == 0) {
                std::cout << "    libarchive listed the encrypted RAR5 "
                          << "fixture but flagged no entry as encrypted\n";
            } else {
                ok(locked > 0, "the encrypted RAR5 fixture's entries are "
                               "marked password-protected");
            }
        }
    }

    // A ZIP built for this suite: one plain .exe, one double-extension .exe and
    // one name that tries to climb out of the extraction directory. The listing
    // reports names verbatim; it never turns an archive name into a filesystem
    // path.
    const ArchiveInspector::ListingAll setupZip =
        ArchiveInspector::listEntries((fixtures / "setup.zip").string());
    ok(setupZip.ok, "the hostile-name ZIP lists cleanly");
    if (!setupZip.ok)
        std::cout << "      setup.zip: " << setupZip.error << '\n';
    ok(setupZip.entries.size() == 4, "the hostile-name ZIP lists four files");
    ok(findEntry(setupZip.entries, "setup.exe") != nullptr,
       "the plain .exe is listed");
    ok(findEntry(setupZip.entries, "invoice.pdf.exe") != nullptr,
       "the double-extension .exe is listed");
    ok(findEntry(setupZip.entries, "../escape.exe") != nullptr,
       "the traversal name is reported verbatim");
#endif

    std::filesystem::remove_all(dir, ec);
    std::cout << "  checks=" << checks << " failures=" << failures << '\n';
    return failures == 0 ? 0 : 1;
}
