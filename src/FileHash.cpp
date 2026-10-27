#include "FileHash.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>

namespace {

std::filesystem::path pathFromUtf8(const std::string& text)
{
#if defined(__cpp_char8_t)
    return std::filesystem::path(
        std::u8string(reinterpret_cast<const char8_t*>(text.data()), text.size()));
#else
    return std::filesystem::u8path(text);
#endif
}

} // namespace

// Small, self-contained SHA-256 (FIPS 180-4). Keeping it here avoids adding
// OpenSSL as a direct build dependency: libtorrent already pulls it in on most
// platforms, but its headers are not guaranteed to be installed for packagers.
FileHash::Sha256::Sha256()
{
    m_state[0] = 0x6a09e667u;
    m_state[1] = 0xbb67ae85u;
    m_state[2] = 0x3c6ef372u;
    m_state[3] = 0xa54ff53au;
    m_state[4] = 0x510e527fu;
    m_state[5] = 0x9b05688cu;
    m_state[6] = 0x1f83d9abu;
    m_state[7] = 0x5be0cd19u;
}

void FileHash::Sha256::update(const unsigned char* data, size_t length)
{
    m_bitLength += static_cast<std::uint64_t>(length) * 8u;

    while (length > 0) {
        const size_t take = std::min(length, sizeof(m_buffer) - m_bufferLength);
        std::memcpy(m_buffer + m_bufferLength, data, take);
        m_bufferLength += take;
        data += take;
        length -= take;

        if (m_bufferLength == sizeof(m_buffer)) {
            transform(m_buffer);
            m_bufferLength = 0;
        }
    }
}

std::string FileHash::Sha256::finalHex()
{
    const std::uint64_t bits = m_bitLength;

    // Padding: a single 1 bit, zeroes, then the message length in bits as
    // a big-endian 64-bit integer.
    m_buffer[m_bufferLength++] = 0x80;
    if (m_bufferLength > 56) {
        while (m_bufferLength < 64) m_buffer[m_bufferLength++] = 0;
        transform(m_buffer);
        m_bufferLength = 0;
    }
    while (m_bufferLength < 56) m_buffer[m_bufferLength++] = 0;
    for (int shift = 56; shift >= 0; shift -= 8) {
        m_buffer[m_bufferLength++] = static_cast<unsigned char>((bits >> shift) & 0xffu);
    }
    transform(m_buffer);

    static const char hex[] = "0123456789abcdef";
    std::string out;
    out.reserve(64);
    for (std::uint32_t word : m_state) {
        for (int shift = 28; shift >= 0; shift -= 4) {
            out.push_back(hex[(word >> shift) & 0xfu]);
        }
    }
    return out;
}

std::uint32_t FileHash::Sha256::rotateRight(std::uint32_t value, int amount)
{
    return (value >> amount) | (value << (32 - amount));
}

void FileHash::Sha256::transform(const unsigned char* block)
{
    static const std::uint32_t constants[64] = {
        0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u,
        0x3956c25bu, 0x59f111f1u, 0x923f82a4u, 0xab1c5ed5u,
        0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u,
        0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u, 0xc19bf174u,
        0xe49b69c1u, 0xefbe4786u, 0x0fc19dc6u, 0x240ca1ccu,
        0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau,
        0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u,
        0xc6e00bf3u, 0xd5a79147u, 0x06ca6351u, 0x14292967u,
        0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu, 0x53380d13u,
        0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u,
        0xa2bfe8a1u, 0xa81a664bu, 0xc24b8b70u, 0xc76c51a3u,
        0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u,
        0x19a4c116u, 0x1e376c08u, 0x2748774cu, 0x34b0bcb5u,
        0x391c0cb3u, 0x4ed8aa4au, 0x5b9cca4fu, 0x682e6ff3u,
        0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u,
        0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u
    };

    std::uint32_t words[64];
    for (int i = 0; i < 16; ++i) {
        words[i] = (static_cast<std::uint32_t>(block[i * 4]) << 24)
                 | (static_cast<std::uint32_t>(block[i * 4 + 1]) << 16)
                 | (static_cast<std::uint32_t>(block[i * 4 + 2]) << 8)
                 | static_cast<std::uint32_t>(block[i * 4 + 3]);
    }
    for (int i = 16; i < 64; ++i) {
        const std::uint32_t s0 = rotateRight(words[i - 15], 7)
                               ^ rotateRight(words[i - 15], 18)
                               ^ (words[i - 15] >> 3);
        const std::uint32_t s1 = rotateRight(words[i - 2], 17)
                               ^ rotateRight(words[i - 2], 19)
                               ^ (words[i - 2] >> 10);
        words[i] = words[i - 16] + s0 + words[i - 7] + s1;
    }

    std::uint32_t a = m_state[0];
    std::uint32_t b = m_state[1];
    std::uint32_t c = m_state[2];
    std::uint32_t d = m_state[3];
    std::uint32_t e = m_state[4];
    std::uint32_t f = m_state[5];
    std::uint32_t g = m_state[6];
    std::uint32_t h = m_state[7];

    for (int i = 0; i < 64; ++i) {
        const std::uint32_t s1 = rotateRight(e, 6)
                               ^ rotateRight(e, 11)
                               ^ rotateRight(e, 25);
        const std::uint32_t ch = (e & f) ^ (~e & g);
        const std::uint32_t temp1 = h + s1 + ch + constants[i] + words[i];
        const std::uint32_t s0 = rotateRight(a, 2)
                               ^ rotateRight(a, 13)
                               ^ rotateRight(a, 22);
        const std::uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
        const std::uint32_t temp2 = s0 + maj;

        h = g;
        g = f;
        f = e;
        e = d + temp1;
        d = c;
        c = b;
        b = a;
        a = temp1 + temp2;
    }

    m_state[0] += a;
    m_state[1] += b;
    m_state[2] += c;
    m_state[3] += d;
    m_state[4] += e;
    m_state[5] += f;
    m_state[6] += g;
    m_state[7] += h;
}

bool FileHash::sha256File(const std::string& path,
                          std::string& digestHex,
                          std::string& error,
                          const std::atomic<bool>* cancelled)
{
    digestHex.clear();
    error.clear();

    if (path.empty()) {
        error = "empty path";
        return false;
    }

    std::error_code ec;
    const std::filesystem::path fsPath = pathFromUtf8(path);
    if (!std::filesystem::is_regular_file(fsPath, ec)) {
        error = ec ? ec.message() : "not a regular file";
        return false;
    }

    std::ifstream input(fsPath, std::ios::binary);
    if (!input) {
        error = "could not open file";
        return false;
    }

    Sha256 hasher;
    std::array<char, 64 * 1024> buffer{};
    while (input) {
        if (cancelled && cancelled->load()) {
            error = "cancelled";
            return false;
        }

        input.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
        const std::streamsize got = input.gcount();
        if (got > 0) {
            hasher.update(reinterpret_cast<const unsigned char*>(buffer.data()),
                          static_cast<size_t>(got));
        }
    }

    if (input.bad() || (input.fail() && !input.eof())) {
        error = "read error";
        return false;
    }

    digestHex = hasher.finalHex();
    return true;
}
