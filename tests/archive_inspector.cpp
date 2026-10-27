// ArchiveInspector checks.
//
// The archive support exists to answer one question before the user runs
// anything: what .exe files are inside this RAR/ZIP, and what is the SHA-256 of
// one of them? These checks build a small ZIP with libarchive, then verify that
// listing ignores non-executables, that hashing matches the published SHA-256
// vectors, and that hostile or malformed input is refused rather than guessed
// at. Nothing is extracted: the archive is created and read in a temp directory
// that the test removes afterwards.
#include "ArchiveInspector.h"
#include "FileHash.h"

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
            if (written != static_cast<la_ssize_t>(spec.bytes.size())) ok = false;
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

const char* kAbcDigest =
    "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad";
const char* kEmptyDigest =
    "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855";
const char* kMillionAsDigest =
    "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0";

} // namespace

int main() {
    std::error_code ec;
    const std::filesystem::path dir =
        std::filesystem::temp_directory_path() / "ftorrent-archive-test";
    std::filesystem::remove_all(dir, ec);
    std::filesystem::create_directories(dir, ec);

    ok(ArchiveInspector::isAvailable(), "archive support is compiled in");
    ok(ArchiveInspector::isArchivePath("setup.rar"), ".rar is offered");
    ok(ArchiveInspector::isArchivePath("SETUP.ZIP"), ".ZIP is offered");
    ok(ArchiveInspector::isArchivePath("setup.zip"), ".zip is offered");
    ok(!ArchiveInspector::isArchivePath("setup.7z"), ".7z is not offered");
    ok(!ArchiveInspector::isArchivePath("setup.exe"), ".exe is not an archive");
    ok(!ArchiveInspector::isArchivePath("rar"), "a bare name is not an archive");

    const std::filesystem::path zipPath = dir / "sample.zip";
    const std::string million(1000000, 'a');
    const std::vector<EntrySpec> entries = {
        {"readme.txt", "ignored", false},
        {"nested", "", true},
        {"nested/inner.EXE", "", false},
        {"setup.exe", "abc", false},
        {"big.exe", million, false},
    };
    ok(createZip(zipPath, entries), "a test ZIP could be created");

    const ArchiveInspector::Listing listing =
        ArchiveInspector::listExecutables(zipPath.string());
    ok(listing.ok, "the test ZIP lists cleanly");
    if (!listing.ok) std::cout << "      error: " << listing.error << '\n';
    ok(listing.executables.size() == 3,
       "only the three .exe entries are listed");

    if (listing.executables.size() == 3) {
        const auto& inner = listing.executables[0];
        const auto& setup = listing.executables[1];
        const auto& big = listing.executables[2];

        ok(inner.path == "nested/inner.EXE", "the nested .EXE keeps its path");
        ok(inner.sizeKnown && inner.size == 0, "the nested .EXE is empty");
        ok(setup.path == "setup.exe", "the first .exe is found");
        ok(setup.sizeKnown && setup.size == 3, "the .exe size is reported");
        ok(!setup.encrypted, "a plain entry is not marked encrypted");
        ok(big.sizeKnown && big.size == million.size(),
           "the large .exe size is reported");
        ok(inner.index > 0 && setup.index > inner.index && big.index > setup.index,
           "entry indexes follow archive order");

        std::string digest;
        std::string error;
        ok(ArchiveInspector::hashExecutable(zipPath.string(), setup.index,
                                            nullptr, digest, error),
           "setup.exe can be hashed");
        ok(digest == kAbcDigest, "setup.exe has the SHA-256 of its bytes");
        if (digest != kAbcDigest) std::cout << "      got: " << digest << '\n';

        ok(ArchiveInspector::hashExecutable(zipPath.string(), inner.index,
                                            nullptr, digest, error),
           "an empty .exe can be hashed");
        ok(digest == kEmptyDigest, "an empty .exe hashes to the empty digest");

        ok(ArchiveInspector::hashExecutable(zipPath.string(), big.index,
                                            nullptr, digest, error),
           "a large .exe can be hashed in chunks");
        ok(digest == kMillionAsDigest, "the large .exe matches its vector");

        // An entry that is not an .exe is refused even when its index is valid.
        ok(!ArchiveInspector::hashExecutable(zipPath.string(), 1, nullptr,
                                             digest, error),
           "a .txt entry is refused");
        ok(error == "the selected entry is not an .exe file",
           "the .txt refusal explains itself");
        ok(digest.empty(), "a refused entry leaves no digest");

        ok(!ArchiveInspector::hashExecutable(zipPath.string(), 2, nullptr,
                                             digest, error),
           "a directory entry is refused");
        ok(error == "the selected entry is not a regular file",
           "the directory refusal explains itself");

        ok(!ArchiveInspector::hashExecutable(zipPath.string(), 99, nullptr,
                                             digest, error),
           "a missing entry index is refused");
        ok(error == "the archive entry was not found",
           "a missing entry index explains itself");

        ok(!ArchiveInspector::hashExecutable(zipPath.string(), 0, nullptr,
                                             digest, error),
           "entry index zero is invalid");

        std::atomic<bool> cancelled{true};
        digest = "stale";
        error.clear();
        ok(!ArchiveInspector::hashExecutable(zipPath.string(), setup.index,
                                             &cancelled, digest, error),
           "a cancelled hash returns no digest");
        ok(error == "cancelled", "a cancelled hash says so");
        ok(digest.empty(), "cancellation clears the previous digest");

        const ArchiveInspector::Listing cancelledListing =
            ArchiveInspector::listExecutables(zipPath.string(), &cancelled);
        ok(!cancelledListing.ok, "a cancelled listing fails");
        ok(cancelledListing.error == "cancelled", "a cancelled listing says so");
    }

    // Password-protected entries must be visible but never hashed. The writer's
    // encryption support depends on the libarchive build, so this part is
    // skipped rather than failed when neither AES nor ZipCrypto is available.
    const std::filesystem::path encryptedPath = dir / "encrypted.zip";
    if (createEncryptedZip(encryptedPath, "aes256") ||
        createEncryptedZip(encryptedPath, "zipcrypt")) {
        const ArchiveInspector::Listing encrypted =
            ArchiveInspector::listExecutables(encryptedPath.string());
        ok(encrypted.ok, "an encrypted ZIP lists cleanly");
        ok(encrypted.executables.size() == 1,
           "the encrypted .exe is listed");
        if (encrypted.executables.size() == 1) {
            ok(encrypted.executables[0].encrypted,
               "the encrypted entry is marked as password-protected");

            std::string digest = "stale";
            std::string error;
            ok(!ArchiveInspector::hashExecutable(
                   encryptedPath.string(), encrypted.executables[0].index,
                   nullptr, digest, error),
               "a password-protected entry is not hashed");
            ok(error == "the selected entry is password-protected",
               "the encrypted refusal explains itself");
            ok(digest.empty(), "the encrypted refusal leaves no digest");
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
    const ArchiveInspector::Listing bad =
        ArchiveInspector::listExecutables(textPath.string());
    ok(!bad.ok, "a file that is not an archive is refused");
    ok(!bad.error.empty(), "the non-archive refusal explains itself");

    const ArchiveInspector::Listing missing =
        ArchiveInspector::listExecutables((dir / "missing.zip").string());
    ok(!missing.ok, "a missing archive is refused");
    ok(!missing.error.empty(), "the missing-archive refusal explains itself");

    ok(!ArchiveInspector::listExecutables("").ok, "an empty path is refused");

    std::filesystem::remove_all(dir, ec);
    std::cout << "  checks=" << checks << " failures=" << failures << '\n';
    return failures == 0 ? 0 : 1;
}
