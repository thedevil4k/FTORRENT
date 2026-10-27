// SHA-256 and VirusTotal URL checks.
//
// The digest is the only thing that decides whether the browser opens the
// report the user expects, so the vectors are the published FIPS 180-4 ones
// plus a file that crosses the 64-byte block boundary. The URL helper is
// checked for the malformed digests that must never reach a shell.
#include "FileHash.h"
#include "VirusTotal.h"

#include <atomic>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

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

std::filesystem::path scratchPath() {
    static int counter = 0;
    return std::filesystem::temp_directory_path() /
           ("ftorrent-filehash-" + std::to_string(++counter) + ".bin");
}

bool writeFile(const std::filesystem::path& path, const std::string& bytes) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) return false;
    out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    return out.good();
}

void checkDigest(const std::string& bytes, const std::string& expected, const char* what) {
    const std::filesystem::path path = scratchPath();
    if (!writeFile(path, bytes)) {
        ok(false, "could write the test file");
        return;
    }

    std::string digest;
    std::string error;
    const bool hashed = FileHash::sha256File(path.string(), digest, error);
    ok(hashed, what);
    ok(hashed && digest == expected, what);
    if (!hashed) std::cout << "      error: " << error << '\n';
    std::error_code ec;
    std::filesystem::remove(path, ec);
}

} // namespace

int main() {
    checkDigest("",
                "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855",
                "empty file");
    checkDigest("abc",
                "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",
                "abc");
    checkDigest("The quick brown fox jumps over the lazy dog",
                "d7a8fbb307d7809469ca9abcb0082e4f8d5651e46d3cdb762d02d0bf37c9e592",
                "one block");
    checkDigest("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq",
                "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1",
                "length padding crosses a block");
    checkDigest(std::string(1000000, 'a'),
                "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0",
                "one million a's");

    std::string digest;
    std::string error;
    ok(!FileHash::sha256File("", digest, error), "empty path is rejected");
    ok(digest.empty(), "empty path leaves the digest empty");

    std::error_code ec;
    const std::filesystem::path missing = scratchPath();
    std::filesystem::remove(missing, ec);
    ok(!FileHash::sha256File(missing.string(), digest, error),
       "missing file is rejected");
    ok(!error.empty(), "missing file explains itself");

    const std::filesystem::path cancelPath = scratchPath();
    ok(writeFile(cancelPath, "abc"), "could write the cancellation test file");
    std::atomic<bool> cancelled{true};
    ok(!FileHash::sha256File(cancelPath.string(), digest, error, &cancelled),
       "a cancelled read returns no digest");
    ok(error == "cancelled", "a cancelled read says so");
    std::filesystem::remove(cancelPath, ec);

    ok(VirusTotal::fileReportUrl(
           "BA7816BF8F01CFEA414140DE5DAE2223B00361A396177A9CB410FF61F20015AD") ==
           "https://www.virustotal.com/gui/file/ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",
       "valid SHA-256 is normalized and placed in the URL");
    ok(VirusTotal::fileReportUrl("abc").empty(),
       "short digest produces no URL");
    ok(VirusTotal::fileReportUrl(std::string(64, 'z')).empty(),
       "non-hex digest produces no URL");

    std::cout << "  checks=" << checks << " failures=" << failures << '\n';
    return failures == 0 ? 0 : 1;
}
