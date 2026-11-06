#ifndef REPUTATIONSERVICE_H
#define REPUTATIONSERVICE_H

#include "Reputation.h"
#include "SecurityTypes.h"

#include <atomic>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

/**
 * @brief Runs reputation lookups for hashes the user asked about.
 *
 * Same shape as FileListService: request() goes in on the UI thread, a worker
 * does the network I/O, and the completion comes back out on the UI thread
 * through Fl::awake(). A result is cached per hash, so clicking the same shield
 * twice costs one request.
 *
 * Nothing here starts on its own. There is no timer, no prefetch and no
 * background sweep: a lookup exists only because the user clicked a shield.
 */
class ReputationService {
public:
    using Completion = std::function<void(const ReputationResult&)>;

    ReputationService();
    // Used by tests to supply providers that never touch the network.
    explicit ReputationService(
        std::vector<std::shared_ptr<ReputationProvider>> providers);
    ~ReputationService();

    ReputationService(const ReputationService&) = delete;
    ReputationService& operator=(const ReputationService&) = delete;

    // What a request() call actually did.
    enum class Start {
        Started,        // This call started the lookup; done will be called.
        AlreadyRunning, // A lookup was in flight; done joins its waiters.
        Cached,         // The answer is already here; use find().
        Rejected        // Not a SHA-256, or too many lookups are running.
    };

    // What is known right now. Starts nothing.
    ReputationResult find(const std::string& sha256) const;

    // Starts a lookup unless the answer is already cached or on its way.
    // 'done' runs on the UI thread for Started and AlreadyRunning; for Cached
    // and Rejected it is never called, and the caller uses find() or shows its
    // own message.
    Start request(const std::string& sha256, const Completion& done);

    // Lookups currently running.
    int pending() const;

    // Forgets every cached answer.
    void clear();

    // The answer for "there is no whole-file hash to ask about", carrying the
    // exact wording the product requires.
    static ReputationResult noHashResult();

private:
    struct Shared {
        std::vector<std::shared_ptr<ReputationProvider>> providers;
        int maxCached = 200;
        int maxInFlight = 2;
        std::mutex mutex;
        std::unordered_map<std::string, ReputationResult> cache;
        // Every surface that asked about a hash while it was in flight. All of
        // them are answered when the single request lands, so a second click
        // is never left showing "Checking...".
        std::unordered_map<std::string, std::vector<Completion>> waiting;
        std::deque<std::string> order;
        std::atomic<int> inFlight{0};
        std::atomic<bool> alive{true};
        std::atomic<bool> cancel{false};
    };

    struct Delivery {
        std::shared_ptr<Shared> shared;
        std::string key;
        ReputationResult result;
    };

    static void deliver(void* data);

    std::shared_ptr<Shared> m_shared;
};

// The instance every widget shares, so a hash looked up in the Add Torrent
// dialog is already known when its Details window is opened. Construction is
// lazy; tests build their own service with fake providers instead.
ReputationService& sharedReputationService();

#endif // REPUTATIONSERVICE_H
