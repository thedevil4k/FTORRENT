#include "SecurityKeys.h"
#include "SystemUtils.h"

#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#ifdef _WIN32
    #include <windows.h>
    #include <dpapi.h>
#else
    #include <fcntl.h>
    #include <sys/stat.h>
    #include <unistd.h>
#endif

namespace {

// Marks the blob this class writes, so a stray file is rejected before it is
// ever handed to the OS decryption call.
const char* const MAGIC = "FTKEY1\n";

void wipe(std::string& text) {
    if (!text.empty()) std::memset(&text[0], 0, text.size());
    text.clear();
}

std::filesystem::path keyPath(SecurityKeys::Provider provider) {
    return std::filesystem::path(SystemUtils::getConfigDir()) / "security" /
           (std::string(SecurityKeys::providerId(provider)) + ".key");
}

// Creates the directory that holds the key files. On Linux it is 0700, so the
// file names inside it are not even visible to other users.
bool ensureDirectory(std::string& error) {
    std::error_code ec;
    const std::filesystem::path dir =
        std::filesystem::path(SystemUtils::getConfigDir()) / "security";
    std::filesystem::create_directories(dir, ec);
    if (ec) {
        error = "Could not create " + dir.string() + ": " + ec.message();
        return false;
    }
#ifndef _WIN32
    if (::chmod(dir.string().c_str(), 0700) != 0 && errno != ENOENT) {
        error = "Could not restrict " + dir.string() + " to the current user";
        return false;
    }
#endif
    return true;
}

} // namespace

const char* SecurityKeys::providerId(Provider provider) {
    switch (provider) {
        case Provider::VirusTotal:
        default:
            return "virustotal";
    }
}

std::string SecurityKeys::storagePath(Provider provider) {
    return keyPath(provider).string();
}

std::string SecurityKeys::storageDescription() {
#ifdef _WIN32
    return "Keys are encrypted with Windows DPAPI and can only be read by your "
           "Windows user account on this machine.";
#else
    return "Keys are kept in a file only your user can read (permissions 0600) "
           "under " + (std::filesystem::path(SystemUtils::getConfigDir()) /
                       "security").string() + ".";
#endif
}

#ifdef _WIN32

namespace {

// Plain layout, checked again after decryption: MAGIC, provider id, newline,
// key. Anything else means the file is not one of ours.
bool parsePlain(const std::string& plain, SecurityKeys::Provider provider,
                std::string& key) {
    const std::size_t magicLen = std::strlen(MAGIC);
    if (plain.size() < magicLen ||
        plain.compare(0, magicLen, MAGIC) != 0) {
        return false;
    }
    const std::string id = SecurityKeys::providerId(provider);
    const std::size_t idStart = magicLen;
    if (plain.size() < idStart + id.size() + 1) return false;
    if (plain.compare(idStart, id.size(), id) != 0) return false;
    if (plain[idStart + id.size()] != '\n') return false;
    key = plain.substr(idStart + id.size() + 1);
    return !key.empty();
}

DATA_BLOB entropyBlob() {
    static const char* entropy = "FTorrent security key";
    DATA_BLOB blob;
    blob.pbData = reinterpret_cast<BYTE*>(const_cast<char*>(entropy));
    blob.cbData = static_cast<DWORD>(std::strlen(entropy));
    return blob;
}

} // namespace

bool SecurityKeys::save(Provider provider, const std::string& key,
                        std::string& error) {
    if (key.empty()) return remove(provider);

    if (!ensureDirectory(error)) return false;

    std::string plain = std::string(MAGIC) + providerId(provider) + "\n" + key;
    DATA_BLOB in;
    in.pbData = reinterpret_cast<BYTE*>(const_cast<char*>(plain.data()));
    in.cbData = static_cast<DWORD>(plain.size());
    DATA_BLOB out{};
    DATA_BLOB entropy = entropyBlob();

    const BOOL ok = CryptProtectData(&in, L"FTorrent provider key", &entropy,
                                    nullptr, nullptr,
                                    CRYPTPROTECT_UI_FORBIDDEN, &out);
    wipe(plain);
    if (!ok) {
        error = "Windows could not protect the key (error " +
                std::to_string(GetLastError()) + ")";
        return false;
    }

    const std::filesystem::path path = keyPath(provider);
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file) {
        LocalFree(out.pbData);
        error = "Could not write " + path.string();
        return false;
    }
    file.write(reinterpret_cast<const char*>(out.pbData),
               static_cast<std::streamsize>(out.cbData));
    file.close();
    LocalFree(out.pbData);
    if (!file) {
        error = "Could not finish writing " + path.string();
        return false;
    }
    return true;
}

bool SecurityKeys::load(Provider provider, std::string& key) {
    key.clear();
    const std::filesystem::path path = keyPath(provider);
    std::ifstream file(path, std::ios::binary);
    if (!file) return false;

    std::vector<char> blob((std::istreambuf_iterator<char>(file)),
                           std::istreambuf_iterator<char>());
    if (blob.empty()) return false;

    DATA_BLOB in;
    in.pbData = reinterpret_cast<BYTE*>(blob.data());
    in.cbData = static_cast<DWORD>(blob.size());
    DATA_BLOB out{};
    DATA_BLOB entropy = entropyBlob();
    if (!CryptUnprotectData(&in, nullptr, &entropy, nullptr, nullptr,
                            CRYPTPROTECT_UI_FORBIDDEN, &out)) {
        return false;   // A different user, or a corrupted file.
    }

    std::string plain(reinterpret_cast<const char*>(out.pbData), out.cbData);
    LocalFree(out.pbData);
    const bool ok = parsePlain(plain, provider, key);
    wipe(plain);
    return ok;
}

#else

bool SecurityKeys::save(Provider provider, const std::string& key,
                        std::string& error) {
    if (key.empty()) return remove(provider);

    if (!ensureDirectory(error)) return false;

    const std::filesystem::path path = keyPath(provider);
    const std::string plain = std::string(MAGIC) + providerId(provider) + "\n" +
                              key;

    // 0600 at creation time, not after the fact: a chmod that follows the
    // write leaves a window in which the key is world-readable.
    const int fd = ::open(path.string().c_str(),
                          O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0600);
    if (fd < 0) {
        error = "Could not create " + path.string();
        return false;
    }
    // Previous content may have been wider than this key; the open() above
    // truncated it, so the file is the new length already.
    std::size_t written = 0;
    while (written < plain.size()) {
        const ssize_t n = ::write(fd, plain.data() + written,
                                  plain.size() - written);
        if (n <= 0) break;
        written += static_cast<std::size_t>(n);
    }
    ::fsync(fd);
    ::close(fd);

    if (written != plain.size()) {
        ::unlink(path.string().c_str());
        error = "Could not write " + path.string();
        return false;
    }
    // The umask cannot widen 0600, but an inherited or replaced file could
    // carry wider bits; make the end state explicit either way.
    ::chmod(path.string().c_str(), 0600);
    return true;
}

bool SecurityKeys::load(Provider provider, std::string& key) {
    key.clear();
    std::ifstream file(keyPath(provider), std::ios::binary);
    if (!file) return false;

    std::string plain((std::istreambuf_iterator<char>(file)),
                      std::istreambuf_iterator<char>());

    const std::size_t magicLen = std::strlen(MAGIC);
    const std::string id = providerId(provider);
    bool ok = false;
    if (plain.size() >= magicLen + id.size() + 1 &&
        plain.compare(0, magicLen, MAGIC) == 0 &&
        plain.compare(magicLen, id.size(), id) == 0 &&
        plain[magicLen + id.size()] == '\n') {
        key = plain.substr(magicLen + id.size() + 1);
        ok = !key.empty();
    }
    wipe(plain);
    return ok;
}

#endif

bool SecurityKeys::remove(Provider provider) {
    std::error_code ec;
    std::filesystem::remove(keyPath(provider), ec);
    return !ec;
}

bool SecurityKeys::exists(Provider provider) {
    std::error_code ec;
    return std::filesystem::exists(keyPath(provider), ec);
}
