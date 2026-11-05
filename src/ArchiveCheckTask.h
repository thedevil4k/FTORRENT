#ifndef ARCHIVECHECKTASK_H
#define ARCHIVECHECKTASK_H

#include "SecurityTypes.h"

#include <libtorrent/add_torrent_params.hpp>

#include <atomic>
#include <functional>
#include <memory>
#include <string>
#include <vector>

/**
 * @brief One archive check, off the UI thread, delivered back to it.
 *
 * Search results, Add Torrent and Details all ask the same question and must
 * answer it the same way. This class owns the thread, the cancellation flag and
 * the hop back to the UI thread, so a surface only says which archive to check
 * and what to do with the answer. Before it existed, the three of them each
 * kept a copy of that plumbing and they drifted: the encrypted-header badge
 * reached only one of them.
 *
 * The delivered callback runs on the UI thread. It is invoked with the result
 * exactly once, unless cancel() was called first. A caller that may be gone
 * before the answer arrives checks its own `alive` flag inside the callback.
 */
class ArchiveCheckTask {
public:
    struct Result {
        bool ok = false;
        std::string error;
        // Ready to display: the summary, the partial note, or the error.
        std::string message;
        std::vector<SecurityFile> entries;
        // Entries flagged password-protected, plus one when the archive's
        // headers themselves are encrypted. The row's lock badge reads this.
        int encrypted = 0;
    };

    using Deliver = std::function<void(Result)>;

    // The .torrent bytes a search row already resolved: no metadata exchange.
    static std::shared_ptr<ArchiveCheckTask> startFromTorrentBody(
        std::string torrentBody, int fileIndex, std::string archivePath,
        Deliver deliver);

    // A magnet, with an optional .torrent path or URL to try first.
    static std::shared_ptr<ArchiveCheckTask> startFromMagnet(
        std::string magnet, std::string torrentPathOrUrl, int fileIndex,
        std::string archivePath, Deliver deliver);

    // A torrent the application already has in hand (Details).
    static std::shared_ptr<ArchiveCheckTask> startFromParams(
        lt::add_torrent_params params, int fileIndex, std::string archivePath,
        Deliver deliver);

    // Asks the worker to stop. The callback is not invoked afterwards.
    void cancel();

    ~ArchiveCheckTask();

    ArchiveCheckTask(const ArchiveCheckTask&) = delete;
    ArchiveCheckTask& operator=(const ArchiveCheckTask&) = delete;

private:
    // Returns false and fills error/note/encryptedHeaders like
    // ArchivePreflight::inspect does. The flag is the task's own, so cancel()
    // stops the worker wherever it is waiting.
    using Worker = std::function<bool(const std::atomic<bool>& cancelled,
                                      std::vector<SecurityFile>& entries,
                                      std::string& error, std::string& note,
                                      bool& encryptedHeaders)>;

    ArchiveCheckTask(std::string archivePath, Deliver deliver);

    static std::shared_ptr<ArchiveCheckTask> start(std::string archivePath,
                                                   Deliver deliver,
                                                   Worker worker);
    void run(const Worker& worker);
    static void deliverOnUiThread(void* data);

    std::string m_archivePath;
    Deliver m_deliver;
    // The worker's own cancellation flag. The task outlives the worker: the
    // thread holds a reference to it until the answer is posted.
    std::atomic<bool> m_cancel{false};
};

#endif // ARCHIVECHECKTASK_H
