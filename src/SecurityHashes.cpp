#include "SecurityHashes.h"

#include <string>

namespace {

std::string hexOf(const lt::sha256_hash& hash) {
    static const char digits[] = "0123456789abcdef";
    std::string out;
    out.reserve(64);
    for (unsigned char byte : hash) {
        out.push_back(digits[byte >> 4]);
        out.push_back(digits[byte & 0x0f]);
    }
    return out;
}

bool allZero(const lt::sha256_hash& hash) {
    for (unsigned char byte : hash) {
        if (byte != 0) return false;
    }
    return true;
}

} // namespace

void SecurityHashes::fillWholeFileHash(const lt::torrent_info& info,
                                       const lt::file_storage& fs, int fileIndex,
                                       SecurityFile& file) {
    file.sha256.clear();
    file.sha256Complete = false;

    if (!info.info_hashes().has_v2()) return;

    const lt::file_index_t index(fileIndex);
    if (fileIndex < 0 || fileIndex >= fs.num_files()) return;
    if (fs.pad_file_at(index)) return;

    const std::int64_t size = fs.file_size(index);
    // Two or more 16 KiB leaves make the root a Merkle root, not the digest of
    // the file. Only the single-block case is safe to call a SHA-256.
    if (size <= 0 || size > kBlockSize) return;

    const lt::sha256_hash root = fs.root(index);
    // A torrent that carried no pieces root leaves zeros here.
    if (allZero(root)) return;

    file.sha256 = hexOf(root);
    file.sha256Complete = true;
}
