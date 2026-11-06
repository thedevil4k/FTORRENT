#include "ArchiveCheckTask.h"

#include "ArchivePreflight.h"
#include "SecurityReputation.h"

#include <FL/Fl.H>

#include <atomic>
#include <thread>
#include <utility>

namespace {

// What Fl::awake() hands back to the UI thread. The task itself may be gone by
// then; the callback and the answer travel together so nothing else is needed.
struct Delivery {
    ArchiveCheckTask::Deliver deliver;
    ArchiveCheckTask::Result result;
};

// How long a check may take. It protects the user from a check that never
// ends: the swarm has to deliver the metadata and the pieces inside this
// window, and a check that cannot is reported as "not now".
const int CHECK_DEADLINE_SECONDS = 120;

} // namespace

ArchiveCheckTask::ArchiveCheckTask(std::string archivePath, Deliver deliver)
    : m_archivePath(std::move(archivePath))
    , m_deliver(std::move(deliver)) {}

ArchiveCheckTask::~ArchiveCheckTask() = default;

void ArchiveCheckTask::cancel() {
    m_cancel.store(true);
}

std::shared_ptr<ArchiveCheckTask>
ArchiveCheckTask::start(std::string archivePath, Deliver deliver,
                        Worker worker) {
    // Not make_shared: the constructor is private and the thread needs the
    // object to stay alive until it posts its answer.
    std::shared_ptr<ArchiveCheckTask> task(
        new ArchiveCheckTask(std::move(archivePath), std::move(deliver)));
    std::thread([task, worker]() { task->run(worker); }).detach();
    return task;
}

std::shared_ptr<ArchiveCheckTask>
ArchiveCheckTask::startFromTorrentBody(std::string torrentBody, int fileIndex,
                                       std::string archivePath,
                                       Deliver deliver) {
    Worker worker = [body = std::move(torrentBody), fileIndex](
                        const std::atomic<bool>& cancelled,
                        std::vector<SecurityFile>& entries, std::string& error,
                        std::string& note, bool& encryptedHeaders) {
        return ArchivePreflight::inspectTorrent(
            body, fileIndex, entries, error, &cancelled, CHECK_DEADLINE_SECONDS,
            &note, &encryptedHeaders);
    };
    return start(std::move(archivePath), std::move(deliver), std::move(worker));
}

std::shared_ptr<ArchiveCheckTask>
ArchiveCheckTask::startFromMagnet(std::string magnet,
                                  std::string torrentPathOrUrl, int fileIndex,
                                  std::string archivePath, Deliver deliver) {
    Worker worker = [magnet = std::move(magnet),
                     torrentPathOrUrl = std::move(torrentPathOrUrl), fileIndex](
                        const std::atomic<bool>& cancelled,
                        std::vector<SecurityFile>& entries, std::string& error,
                        std::string& note, bool& encryptedHeaders) {
        return ArchivePreflight::inspect(
            magnet, torrentPathOrUrl, fileIndex, entries, error, &cancelled,
            CHECK_DEADLINE_SECONDS, &note, &encryptedHeaders);
    };
    return start(std::move(archivePath), std::move(deliver), std::move(worker));
}

std::shared_ptr<ArchiveCheckTask>
ArchiveCheckTask::startFromParams(lt::add_torrent_params params, int fileIndex,
                                  std::string archivePath, Deliver deliver) {
    // The task keeps the cancellation flag alive for the whole run, so the
    // worker sees cancel() the moment it is called.
    Worker worker = [params = std::move(params), fileIndex](
                        const std::atomic<bool>& cancelled,
                        std::vector<SecurityFile>& entries, std::string& error,
                        std::string& note, bool& encryptedHeaders) {
        return ArchivePreflight::inspect(params, fileIndex, entries, error,
                                         &cancelled, CHECK_DEADLINE_SECONDS,
                                         &note, &encryptedHeaders);
    };
    return start(std::move(archivePath), std::move(deliver), std::move(worker));
}

void ArchiveCheckTask::run(const Worker& worker) {
    Delivery* delivery = new Delivery;
    delivery->deliver = m_deliver;

    std::vector<SecurityFile> entries;
    std::string error;
    std::string note;
    bool encryptedHeaders = false;
    const bool ok = worker(m_cancel, entries, error, note, encryptedHeaders);

    // A cancelled check has no answer to give: the surface that asked is
    // usually gone, and if it is not, an error line would be a lie.
    if (m_cancel.load()) {
        delete delivery;
        return;
    }

    Result& result = delivery->result;
    result.ok = ok;
    result.error = error;
    if (ok) {
        result.message = archiveCheckMessage(m_archivePath, entries, note);
        result.entries = std::move(entries);
    } else {
        result.message =
            "Archive: the index could not be read (" + error + ").";
    }
    // The lock badge reads this count and nothing else. Encrypted headers are
    // not a guess: libarchive refused to open the archive for lack of a
    // password, and an unreadable index is exactly what the lock means.
    if (encryptedHeaders) {
        result.encrypted = 1;
    } else {
        for (const SecurityFile& entry : result.entries) {
            if (entry.encrypted) ++result.encrypted;
        }
    }

    Fl::awake(&ArchiveCheckTask::deliverOnUiThread, delivery);
}

void ArchiveCheckTask::deliverOnUiThread(void* data) {
    std::unique_ptr<Delivery> delivery(static_cast<Delivery*>(data));
    if (delivery && delivery->deliver) {
        delivery->deliver(std::move(delivery->result));
    }
}
