#ifndef ARCHIVEPREFLIGHT_H
#define ARCHIVEPREFLIGHT_H

#include "SecurityTypes.h"

#include <libtorrent/add_torrent_params.hpp>

#include <atomic>
#include <cstdint>
#include <string>
#include <vector>

/**
 * @brief Reads an archive's index before the download, from two pieces.
 *
 * ZIP keeps its central directory at the end of the file and RAR5 keeps headers
 * at both ends, so the first and the last piece are enough to list what an
 * archive contains - which is exactly what the user wants to know ("does this
 * rar hide an .exe?") without downloading the archive.
 *
 * Only those pieces are fetched, and only when the user asks. They are written
 * to a temporary directory under the config folder, used to build a sparse file
 * of the real size with just those two windows filled, and deleted before the
 * call returns. Nothing is ever written to the download folder.
 */
class ArchivePreflight {
public:
    // What was fetched for one file: its first and last piece.
    struct Snippet {
        std::int64_t fileSize = 0;
        std::string head;   // Bytes from the start of the file
        std::string tail;   // Bytes from the end of the file
    };

    // Where the pieces of one file live in the piece space.
    struct PieceRange {
        int firstPiece = 0;
        int firstOffset = 0;   // Offset of the file's first byte in that piece
        int firstLength = 0;
        int lastPiece = 0;
        int lastOffset = 0;    // Offset of the file's bytes in the last piece
        int lastLength = 0;
    };

    // Pure arithmetic, exposed for the tests.
    static PieceRange computeRange(std::int64_t fileOffset, std::int64_t fileSize,
                                   int pieceLength, std::int64_t torrentSize);

    // Writes a file of the full size with only the head and tail filled. The
    // gap is left unwritten, so it stays sparse on disk.
    static bool writeSparseFile(const std::string& path, const Snippet& snippet,
                                std::string& error);

    // Fetches only the piece(s) holding the first and last byte of one file.
    // 'torrentPathOrUrl' may be an https:// .torrent link, a local .torrent
    // path, or empty when a magnet is given (in which case the metadata is
    // fetched first). A downloaded .torrent is checked against the magnet's
    // info hash before its bytes are trusted.
    static bool fetchSnippet(const std::string& magnet,
                             const std::string& torrentPathOrUrl, int fileIndex,
                             Snippet& out, std::string& error,
                             const std::atomic<bool>* cancelled,
                             int timeoutSeconds = 90);

    // Same, for a torrent already known to the app (Details). Passing its
    // metadata means no second metadata exchange with the swarm.
    static bool fetchSnippet(const lt::add_torrent_params& params,
                             int fileIndex, Snippet& out, std::string& error,
                             const std::atomic<bool>* cancelled,
                             int timeoutSeconds = 90);

    // Fetch, assemble, list the entries, delete everything it created.
    // 'entries' receives every regular file name the index exposed.
    static bool inspect(const std::string& magnet,
                        const std::string& torrentPath, int fileIndex,
                        std::vector<SecurityFile>& entries, std::string& error,
                        const std::atomic<bool>* cancelled,
                        int timeoutSeconds = 90);

    static bool inspect(const lt::add_torrent_params& params, int fileIndex,
                        std::vector<SecurityFile>& entries,
                        std::string& error, const std::atomic<bool>* cancelled,
                        int timeoutSeconds = 90);

    // Same, from the bytes of a .torrent the app already holds: the file list
    // the search row resolved keeps them, so a check pays no metadata exchange
    // for a torrent it has already seen. Returns false and fills 'error' when
    // the bytes are not a torrent.
    static bool inspectTorrent(const std::string& torrentBody, int fileIndex,
                               std::vector<SecurityFile>& entries,
                               std::string& error,
                               const std::atomic<bool>* cancelled,
                               int timeoutSeconds = 90);

    // Root of the temporary directories. Cleaned up by inspect().
    static std::string tempRoot();
};

#endif // ARCHIVEPREFLIGHT_H
