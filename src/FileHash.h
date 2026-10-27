#ifndef FILEHASH_H
#define FILEHASH_H

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <string>

/**
 * @brief Hashing helpers for files the user asks to check.
 *
 * The digest is computed locally. No bytes leave the machine here; the caller
 * decides what to do with the finished SHA-256 (FTorrent opens VirusTotal with
 * it, which is a lookup by hash, not an upload).
 */
class FileHash {
public:
    /**
     * @brief Incremental SHA-256 (FIPS 180-4).
     *
     * The archive reader uses this to hash decompressed bytes as they arrive.
     * Nothing has to be written to disk, so an archive entry cannot escape
     * into the filesystem through a crafted path.
     */
    class Sha256 {
    public:
        Sha256();

        void update(const unsigned char* data, size_t length);
        std::string finalHex();

    private:
        static std::uint32_t rotateRight(std::uint32_t value, int amount);
        void transform(const unsigned char* block);

        std::uint32_t m_state[8];
        unsigned char m_buffer[64] = {};
        size_t m_bufferLength = 0;
        std::uint64_t m_bitLength = 0;
    };

    /**
     * @brief Compute the SHA-256 of an existing regular file.
     *
     * @param path      File to read.
     * @param digestHex Receives 64 lowercase hexadecimal characters on success.
     * @param error     Receives a human-readable reason on failure.
     * @param cancelled Optional flag checked between 64 KiB blocks. Setting it
     *                  stops the read and reports a cancellation as an error.
     * @return true when the whole file was read and hashed.
     */
    static bool sha256File(const std::string& path,
                           std::string& digestHex,
                           std::string& error,
                           const std::atomic<bool>* cancelled = nullptr);
};

#endif // FILEHASH_H
