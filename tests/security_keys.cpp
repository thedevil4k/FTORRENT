// SecurityKeys checks.
//
// The point of this module is that a provider key never ends up somewhere it
// can be read by accident: not in settings.ini, not world-readable, and on
// Windows not decryptable by another machine or user. These checks pin down
// the round trip, the removal path, the per-file separation and -- the part
// that actually matters -- that the stored bytes do not contain the key.
#include "SecurityKeys.h"
#include "SystemUtils.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#ifndef _WIN32
#include <sys/stat.h>
#endif

namespace {

int checks = 0;
int failures = 0;

void ok(bool condition, const std::string& what) {
    checks++;
    if (!condition) {
        failures++;
        std::cout << "    FAIL " << what << '\n';
    }
}

void section(const char* title) {
    std::cout << "\n== " << title << " ==\n";
}

std::filesystem::path scratchDir() {
    return std::filesystem::temp_directory_path() /
           "ftorrent-test-security-keys";
}

// Points the app's config at a directory this process owns. Same variables
// SystemUtils::getConfigDir() reads, per platform.
void useScratchConfig() {
    namespace fs = std::filesystem;
    const fs::path dir = scratchDir();
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir, ec);
#ifdef _WIN32
    _putenv_s("APPDATA", dir.string().c_str());
    // SHGetFolderPath() does not follow a changed APPDATA; the explicit
    // override is what keeps this test out of the real profile.
    _putenv_s("FTORRENT_CONFIG_DIR", dir.string().c_str());
#else
    setenv("XDG_CONFIG_HOME", dir.string().c_str(), 1);
    setenv("FTORRENT_CONFIG_DIR", dir.string().c_str(), 1);
#endif
}

std::string readAllBytes(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(in)),
                       std::istreambuf_iterator<char>());
}

} // namespace

int main() {
    useScratchConfig();

    // The store must resolve under the scratch directory. Without this, a
    // Windows test wrote to the real profile (SHGetFolderPath ignored the
    // redirected APPDATA) and removed the key that was already there.
    ok(std::filesystem::path(
           SecurityKeys::storagePath(SecurityKeys::Provider::VirusTotal))
               .parent_path()
               .parent_path()
               .lexically_normal() == scratchDir().lexically_normal(),
       "the key store resolves inside the scratch directory");

    const std::string key =
        "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef";
    const SecurityKeys::Provider vt = SecurityKeys::Provider::VirusTotal;

    section("normalizing a pasted key");
    {
        ok(SecurityKeys::normalize("  abc123  ") == "abc123",
           "spaces around a key are dropped");
        ok(SecurityKeys::normalize("abc\t12\r\n3") == "abc123",
           "tabs and a line break inside a key are dropped");
        ok(SecurityKeys::normalize(" \t\r\n ") == "",
           "whitespace alone is not a key");
        ok(SecurityKeys::normalize(key) == key,
           "a clean key is left exactly as it is");
    }

    section("empty store");
    ok(!SecurityKeys::exists(vt), "no key before one is saved");
    {
        std::string loaded = "untouched";
        ok(!SecurityKeys::load(vt, loaded),
           "loading a missing key reports failure");
        ok(loaded.empty(), "a failed load leaves nothing behind in the output");
    }

    section("round trip");
    {
        std::string error;
        ok(SecurityKeys::save(vt, key, error),
           "saving a key succeeds: " + error);
        ok(SecurityKeys::exists(vt), "the key exists after saving");

        std::string loaded;
        ok(SecurityKeys::load(vt, loaded),
           "the key can be read back by the same user");
        ok(loaded == key, "the key read back is the key that was saved");

        ok(!SecurityKeys::storagePath(vt).empty(),
           "the store reports where the key lives");
        ok(!SecurityKeys::storageDescription().empty(),
           "the store describes how it protects the key");
        ok(SecurityKeys::storageDescription().find(key) == std::string::npos,
           "the description never leaks the key itself");
    }

    section("the stored bytes");
    {
        const std::string raw = readAllBytes(SecurityKeys::storagePath(vt));
        ok(!raw.empty(), "the key file is not empty");
#ifdef _WIN32
        // DPAPI encrypts the blob: the clear key must not appear on disk.
        ok(raw.find(key) == std::string::npos,
           "the file on disk does not contain the key in the clear");
#endif
#ifdef _WIN32
        ok(raw.compare(0, 6, "FTKEY1") != 0,
           "on Windows the file is a DPAPI blob, not the plain layout");
#else
        ok(raw.compare(0, 6, "FTKEY1") == 0,
           "on Linux the file carries the class's own marker");
#endif
    }

#ifndef _WIN32
    section("permissions");
    {
        struct stat st;
        const std::string path = SecurityKeys::storagePath(vt);
        ok(::stat(path.c_str(), &st) == 0, "the key file exists on disk");
        ok((st.st_mode & 0777) == 0600,
           "the key file is readable only by its owner (0600), not " +
               std::to_string(st.st_mode & 0777));

        const std::filesystem::path dir =
            std::filesystem::path(path).parent_path();
        struct stat ds;
        ok(::stat(dir.string().c_str(), &ds) == 0,
           "the key directory exists on disk");
        ok((ds.st_mode & 0777) == 0700,
           "the key directory is private to its owner (0700)");
    }
#endif

    section("replacement and removal");
    {
        const std::string other = "ffffffffffffffffffffffffffffffff";
        std::string error;
        ok(SecurityKeys::save(vt, other, error),
           "a second save replaces the first key");

        std::string loaded;
        ok(SecurityKeys::load(vt, loaded), "the replacement can be read back");
        ok(loaded == other, "the replacement is the key that was saved");

        ok(SecurityKeys::save(vt, std::string(), error),
           "saving an empty key removes it");
        ok(!SecurityKeys::exists(vt), "the key no longer exists after removal");
        ok(!SecurityKeys::load(vt, loaded),
           "a removed key cannot be read back");

        ok(SecurityKeys::remove(vt), "removing a missing key is not an error");
    }

    section("two providers, two files");
    {
        const SecurityKeys::Provider mb = SecurityKeys::Provider::MalwareBazaar;
        const std::string bazaarKey =
            "0123456789abcdef0123456789abcdef"; // 32 hex

        std::string error;
        ok(SecurityKeys::save(vt, key, error), "the VirusTotal key is saved");
        ok(SecurityKeys::save(mb, bazaarKey, error),
           "the MalwareBazaar key is saved: " + error);
        ok(SecurityKeys::storagePath(vt) != SecurityKeys::storagePath(mb),
           "each provider has its own file");

        std::string vtLoaded, mbLoaded;
        ok(SecurityKeys::load(vt, vtLoaded) && vtLoaded == key,
           "the VirusTotal key survives next to the other one");
        ok(SecurityKeys::load(mb, mbLoaded) && mbLoaded == bazaarKey,
           "the MalwareBazaar key is read back on its own");

        SecurityKeys::save(mb, std::string(), error);
        ok(!SecurityKeys::exists(mb), "removing one key removes its file");
        ok(SecurityKeys::exists(vt), "the other provider's key is untouched");
    }

    section("corrupt and foreign files");
    {
        const std::filesystem::path path = SecurityKeys::storagePath(vt);
        std::filesystem::create_directories(path.parent_path());
        {
            std::ofstream out(path, std::ios::binary | std::ios::trunc);
            out << "not a key file at all";
        }
        std::string loaded = "untouched";
        ok(!SecurityKeys::load(vt, loaded),
           "a file that is not ours is refused");
        ok(loaded.empty(), "a refused file yields no key");
        std::error_code ec;
        std::filesystem::remove(path, ec);
    }

    std::cout << "\nsecurity_keys checks=" << checks << " failures=" << failures
              << '\n';

    std::error_code ec;
    std::filesystem::remove_all(scratchDir(), ec);
    return failures ? 1 : 0;
}
