#ifndef ARCHIVEPROBESESSION_H
#define ARCHIVEPROBESESSION_H

#include <libtorrent/add_torrent_params.hpp>
#include <libtorrent/file_storage.hpp>
#include <libtorrent/session.hpp>
#include <libtorrent/torrent_handle.hpp>
#include <libtorrent/torrent_info.hpp>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "ArchivePreflight.h"   // PieceRange and computeRange

/**
 * @brief One throwaway libtorrent session that reads byte ranges of one file.
 *
 * It never wants the file: every file starts at priority zero and a piece is
 * fetched only because the check asks for it by index. That is what lets the
 * archive preflight read a ZIP's index, or walk a RAR 1.5-4.x header chain, out
 * of a 20 GB torrent without downloading the archive.
 *
 * Two libtorrent behaviours are handled here, both found with a local seeder:
 *
 *  - With every file deselected the torrent is marked "finished" as soon as the
 *    last wanted piece arrives, and then drops its peers. The next request has
 *    to revive it (clear_peers + connect_peer to the peers it was seen with)
 *    or it never connects again.
 *  - Right after its initial check libtorrent has no piece layer and throws
 *    "a piece layer is invalid" at a piece request, so the request is retried.
 *
 * The session writes only to a temporary directory under the config folder and
 * removes it in the destructor.
 */
class ArchiveProbeSession {
public:
    // Removes the torrent from the session and waits until libtorrent has
    // really let it go. The session may be destroyed after this returns, and
    // only then may the files it wrote be deleted.
    static void shutdown(lt::session& session, lt::torrent_handle& handle);

    // Reads the metadata if 'params' does not carry it, then marks every file
    // deselected. 'error' explains any failure and ok() is then false.
    ArchiveProbeSession(const lt::add_torrent_params& params, int fileIndex,
                        const std::atomic<bool>* cancel, int timeoutSeconds,
                        std::string& error);
    ~ArchiveProbeSession();

    ArchiveProbeSession(const ArchiveProbeSession&) = delete;
    ArchiveProbeSession& operator=(const ArchiveProbeSession&) = delete;

    bool ok() const { return m_ok; }
    std::int64_t fileSize() const { return m_fileSize; }
    int pieceLength() const { return m_pieceLength; }
    int fetchedPieces() const { return m_fetchedPieces; }

    // At most this many distinct pieces may be fetched. Zero means no limit.
    void setPieceBudget(int pieces) { m_budget = pieces; }

    // The piece holding the first byte of the file, and the one holding the
    // last, trimmed to the file's bytes.
    bool readFirst(std::string& out, std::string& error);
    bool readLast(std::string& out, std::string& error);

    // Reads bytes relative to the start of the archive file, for libarchive.
    bool readArchiveRange(std::int64_t offset, std::size_t length,
                          std::string& out, std::string& error);

private:
    static constexpr std::size_t CACHE_LIMIT_BYTES = 128u * 1024u * 1024u;
    static constexpr std::size_t MAX_SEEN_PEERS = 40;

    void storePiece(int index, std::string bytes);
    void collectAlerts();
    void settleFilePriorities();
    bool requestPiece(int piece);
    void rememberPeer(const lt::tcp::endpoint& endpoint);
    void reviveSwarm(bool force = false);
    bool waitForPiece(int piece, std::string& error, int maxWaitSeconds = 0);

    const std::atomic<bool>* m_cancel = nullptr;
    std::unique_ptr<lt::session> m_ses;
    lt::torrent_handle m_handle;
    std::shared_ptr<const lt::torrent_info> m_info;
    const lt::file_storage* m_storage = nullptr;
    std::filesystem::path m_workDir;
    ArchivePreflight::PieceRange m_range;
    std::int64_t m_fileOffset = 0;
    std::int64_t m_fileSize = 0;
    int m_pieceLength = 0;
    int m_pieceCount = 0;
    int m_budget = 0;
    int m_fetchedPieces = 0;
    bool m_budgetHit = false;
    bool m_settled = false;
    bool m_ok = false;
    std::map<int, std::string> m_cache;
    std::size_t m_cacheBytes = 0;
    std::map<int, bool> m_asked;
    std::vector<lt::tcp::endpoint> m_seenPeers;
    std::chrono::steady_clock::time_point m_lastRevive{};
    std::chrono::steady_clock::time_point m_deadline;
    std::chrono::steady_clock::time_point m_lastAsk;
};

#endif // ARCHIVEPROBESESSION_H
