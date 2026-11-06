#ifndef TORRENT_FILE_HASH_TASK_H
#define TORRENT_FILE_HASH_TASK_H

#include <libtorrent/add_torrent_params.hpp>
#include <libtorrent/torrent_handle.hpp>

#include <atomic>
#include <functional>
#include <memory>
#include <string>

namespace lt = libtorrent;

/**
 * @brief Downloads only the pieces that cover one torrent file and computes
 * its SHA-256, without downloading the whole torrent.
 *
 * Used by the pre-download security flow: the user wants to know the hash of
 * an executable inside a torrent before deciding whether to download it.
 * This task fetches only the pieces that cover that file, hashes them, and
 * delivers the digest. Nothing is uploaded; the temporary session is torn down
 * as soon as the answer is ready.
 */
class TorrentFileHashTask {
public:
    struct Result {
        bool ok = false;
        std::string error;
        std::string sha256; // lowercase hex, empty on failure
        std::string path;   // the file's path inside the torrent
    };

    using Deliver = std::function<void(Result)>;

    // Starts a detached worker thread. The caller owns the returned handle only
    // long enough to cancel() if the dialog closes before the answer arrives.
    static std::shared_ptr<TorrentFileHashTask>
    start(lt::add_torrent_params params, int fileIndex,
          const std::string& filePath, std::string savePath, Deliver deliver);

    void cancel();
    ~TorrentFileHashTask();

    TorrentFileHashTask(const TorrentFileHashTask&) = delete;
    TorrentFileHashTask& operator=(const TorrentFileHashTask&) = delete;

private:
    TorrentFileHashTask(lt::add_torrent_params params, int fileIndex,
                        const std::string& filePath, std::string savePath,
                        Deliver deliver);
    void run();
    static void deliverOnUiThread(void* data);

    lt::add_torrent_params m_params;
    int m_fileIndex;
    std::string m_filePath;
    std::string m_savePath;   // caller's save path (not used for storage)
    std::string m_tempDir;    // throwaway directory the session writes into
    Deliver m_deliver;
    std::atomic<bool> m_cancel{false};
    std::shared_ptr<lt::session> m_session;
    lt::torrent_handle m_handle;
};

#endif // TORRENT_FILE_HASH_TASK_H
