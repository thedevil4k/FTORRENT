#include "FileListService.h"

#include <FL/Fl.H>

#include <thread>

namespace {

// How many lists are remembered. A long session searching many queries would
// otherwise keep every list it ever resolved; dropping the oldest is fine, a
// lost list only costs one click.
const int MAX_CACHED = 400;

// How many are fetched at once. Each fetch opens its own peer session, so this
// also keeps sockets and DHT state bounded.
const int MAX_IN_FLIGHT = 4;

} // namespace

FileListService::FileListService()
    : m_shared(std::make_shared<Shared>())
{
    m_shared->maxCached = MAX_CACHED;
    m_shared->maxInFlight = MAX_IN_FLIGHT;
}

FileListService::~FileListService() {
    // Say goodbye before the members go. A worker that finishes after this sees
    // the flag false and drops its answer, rather than calling back into a
    // service -- and through it a listener -- that is being destroyed.
    m_shared->alive.store(false);
}

FileListService::Entry FileListService::find(const std::string& key) const {
    std::lock_guard<std::mutex> lk(m_shared->mutex);
    auto it = m_shared->entries.find(key);
    return it == m_shared->entries.end() ? Entry{} : it->second;
}

int FileListService::pending() const {
    return m_shared->inFlight.load();
}

void FileListService::clear() {
    std::lock_guard<std::mutex> lk(m_shared->mutex);
    m_shared->entries.clear();
    m_shared->order.clear();
}

bool FileListService::request(const std::string& key,
                              const std::string& magnet,
                              const std::string& torrentUrl,
                              const Completion& done) {
    {
        std::lock_guard<std::mutex> lk(m_shared->mutex);
        auto it = m_shared->entries.find(key);
        if (it != m_shared->entries.end() &&
            (it->second.state == FileListState::Loaded ||
             it->second.state == FileListState::Loading)) {
            return false;                     // known, or already on its way
        }
        // Claim it, so a double click cannot start two probes.
        Entry& entry = m_shared->entries[key];
        entry = Entry{};
        entry.state = FileListState::Loading;
        if (it == m_shared->entries.end()) m_shared->order.push_back(key);
    }

    // Take a concurrency slot. Overflow is not queued: the claim is handed
    // back so the row can be retried by the next click instead of the user
    // being left with a row that says "reading" and never finishes.
    if (m_shared->inFlight.fetch_add(1) >= m_shared->maxInFlight) {
        m_shared->inFlight.fetch_sub(1);
        std::lock_guard<std::mutex> lk(m_shared->mutex);
        auto it = m_shared->entries.find(key);
        if (it != m_shared->entries.end() &&
            it->second.state == FileListState::Loading) {
            it->second = Entry{};
        }
        return false;
    }

    // The worker captures the shared block, never the service. That is what
    // makes it safe for the fetch to outlive its owner.
    std::shared_ptr<Shared> shared = m_shared;
    std::thread([shared, key, magnet, torrentUrl, done]() {
        Delivery* d = new Delivery;
        d->shared = shared;
        d->key = key;
        d->done = done;
        d->entry.state = FileListState::Failed;
        try {
            // resolve() never throws, but a bad_alloc in a worker must not take
            // the process down either.
            if (TorrentFileList::resolve(magnet, torrentUrl, d->entry.files,
                                         d->entry.error)) {
                d->entry.state = FileListState::Loaded;
            }
        } catch (const std::exception& e) {
            d->entry.state = FileListState::Failed;
            d->entry.error = e.what();
        } catch (...) {
            d->entry.state = FileListState::Failed;
            d->entry.error = "Unknown error";
        }
        Fl::awake(&FileListService::deliver, d);
    }).detach();
    return true;
}

// Runs on the UI thread, from Fl::awake().
void FileListService::deliver(void* data) {
    std::unique_ptr<Delivery> d(static_cast<Delivery*>(data));
    const std::shared_ptr<Shared>& shared = d->shared;
    if (!shared->alive.load()) return;        // the owner is gone; drop it

    // The slot is released here rather than in the worker, because the counter
    // has to be touched by the thread that owns it. It matters that this runs
    // even when the key is no longer cached.
    shared->inFlight.fetch_sub(1);

    {
        std::lock_guard<std::mutex> lk(shared->mutex);
        // A retry after a failure is allowed to replace the previous outcome.
        // The key can also be absent: the cap may have evicted it while this
        // fetch was running. The answer is still worth having, so it is put
        // back rather than thrown away.
        auto it = shared->entries.find(d->key);
        if (it == shared->entries.end()) {
            shared->entries.emplace(d->key, d->entry);
            shared->order.push_back(d->key);
        } else {
            it->second = d->entry;
        }
        while (static_cast<int>(shared->order.size()) > shared->maxCached) {
            shared->entries.erase(shared->order.front());
            shared->order.pop_front();
        }
    }

    // Called with no lock held: the listener repaints, and may come straight
    // back here for the next thing it wants.
    if (d->done) d->done(d->entry);
}