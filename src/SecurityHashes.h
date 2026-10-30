#ifndef SECURITYHASHES_H
#define SECURITYHASHES_H

#include "SecurityTypes.h"

#include <libtorrent/file_storage.hpp>
#include <libtorrent/torrent_info.hpp>

/**
 * @brief Whole-file hashes taken from the torrent metadata itself.
 *
 * A BitTorrent v2 metainfo file stores, per file, the root of a Merkle tree
 * built from 16 KiB blocks (BEP 52). That root equals the SHA-256 of the file
 * only when the file is a single 16 KiB block: with two or more leaves it is a
 * Merkle root, which is a different value from the digest of the file.
 *
 * Sending a Merkle root to VirusTotal would produce a confident "no record" for
 * a file that is in fact known, so anything larger than one block is left
 * without a hash on purpose. The result is the honest answer: no whole-file
 * hash exists before the download.
 */
namespace SecurityHashes {

// BEP 52 block size. Exposed for the tests and for the documentation.
const std::int64_t kBlockSize = 16 * 1024;

// Fills 'file.sha256' and 'file.sha256Complete' from 'info'/'fs'.
// Leaves both empty/false for v1-only torrents, pad files, and files that do
// not fit in a single block.
void fillWholeFileHash(const lt::torrent_info& info,
                       const lt::file_storage& fs, int fileIndex,
                       SecurityFile& file);

} // namespace SecurityHashes

#endif // SECURITYHASHES_H
