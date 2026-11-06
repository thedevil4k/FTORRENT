#include "ReputationService.h"
#include "SecurityKeys.h"

#include <FL/Fl.H>

#include <algorithm>
#include <thread>
#include <utility>

namespace {

// Joins the provider summaries. Providers that could not answer are listed
// last, so a real finding is never buried under a configuration complaint.
std::string joinSummaries(const std::vector<ProviderVerdict>& verdicts) {
    std::string answered;
    std::string failed;
    for (const ProviderVerdict& verdict : verdicts) {
        if (verdict.summary.empty()) continue;
        std::string& target = verdict.answered ? answered : failed;
        if (!target.empty()) target += " ";
        target += verdict.summary;
    }
    if (answered.empty()) return failed;
    if (failed.empty()) return answered;
    return answered + " " + failed;
}

std::vector<std::string>
answeredNames(const std::vector<ProviderVerdict>& verdicts) {
    std::vector<std::string> names;
    for (const ProviderVerdict& verdict : verdicts) {
        if (verdict.answered) names.push_back(verdict.provider);
    }
    return names;
}

std::string joinNames(const std::vector<std::string>& names) {
    std::string out;
    for (std::size_t i = 0; i < names.size(); ++i) {
        if (i == names.size() - 1 && i > 0) out += " and ";
        else if (i > 0) out += ", ";
        out += names[i];
    }
    return out;
}

ReputationResult aggregate(const std::string& sha256,
                           const std::vector<ProviderVerdict>& verdicts) {
    ReputationResult result;
    result.sha256 = sha256;
    result.verdicts = verdicts;

    bool anyKnown = false;
    bool anyAnswered = false;
    bool anyWaitedOnKey = false;
    bool anyMalicious = false;

    for (const ProviderVerdict& verdict : verdicts) {
        if (verdict.answered) anyAnswered = true;
        if (verdict.known) anyKnown = true;
        if (verdict.malicious > 0 || verdict.suspicious > 0)
            anyMalicious = true;
        if (verdict.needsKey) anyWaitedOnKey = true;
    }

    if (anyKnown) {
        result.state = ReputationState::Known;
        result.message = joinSummaries(verdicts);
        if (!anyMalicious) {
            // A record with no detections is still not a guarantee: the sample
            // may simply be newer than the engines' signatures.
            result.message +=
                " A record with no detections is not a guarantee.";
        }
        return result;
    }

    if (!anyAnswered) {
        result.message = joinSummaries(verdicts);
        result.state =
            anyWaitedOnKey ? ReputationState::NoKey : ReputationState::Error;
        if (result.message.empty()) {
            result.message = "No reputation provider could be asked.";
        }
        return result;
    }

    result.state = ReputationState::Unknown;
    const std::string names = joinNames(answeredNames(verdicts));
    result.message = "No record for this hash in " +
                     (names.empty() ? std::string("the providers") : names) +
                     ".";
    const std::string failed = joinSummaries(verdicts);
    if (!failed.empty() && failed != result.message) {
        result.message += " " + failed;
    }
    return result;
}

std::string loadKeyFor(const ReputationProvider& provider) {
    if (!provider.requiresKey()) return std::string();
    std::string key;
    if (provider.name() == "VirusTotal") {
        SecurityKeys::load(SecurityKeys::Provider::VirusTotal, key);
    } else if (provider.name() == "MalwareBazaar") {
        SecurityKeys::load(SecurityKeys::Provider::MalwareBazaar, key);
    }
    return key;
}

} // namespace

ReputationService& sharedReputationService() {
    static ReputationService service;
    return service;
}

ReputationResult ReputationService::noHashResult() {
    ReputationResult result;
    result.state = ReputationState::NoCompleteHash;
    result.message = reputationNoHashMessage();
    return result;
}

ReputationService::ReputationService()
    : ReputationService({
          std::make_shared<VirusTotalProvider>(),
          std::make_shared<MalwareBazaarProvider>(),
      }) {}

ReputationService::ReputationService(
    std::vector<std::shared_ptr<ReputationProvider>> providers)
    : m_shared(std::make_shared<Shared>()) {
    m_shared->providers = std::move(providers);
}

ReputationService::~ReputationService() {
    // Stops workers where they can notice (between providers and inside the
    // HTTP transfer) and drops whatever lands afterwards.
    m_shared->cancel.store(true);
    m_shared->alive.store(false);
}

ReputationResult ReputationService::find(const std::string& sha256) const {
    const std::string key = normalizeSha256(sha256);
    std::lock_guard<std::mutex> lk(m_shared->mutex);
    auto it = m_shared->cache.find(key);
    return it == m_shared->cache.end() ? ReputationResult{} : it->second;
}

int ReputationService::pending() const {
    return m_shared->inFlight.load();
}

void ReputationService::clear() {
    std::lock_guard<std::mutex> lk(m_shared->mutex);
    m_shared->cache.clear();
    m_shared->order.clear();
}

ReputationService::Start ReputationService::request(const std::string& sha256,
                                                    const Completion& done) {
    if (!isSha256Hex(sha256)) return Start::Rejected;
    const std::string key = normalizeSha256(sha256);

    {
        std::lock_guard<std::mutex> lk(m_shared->mutex);
        // Only a real answer blocks a second request. A "no key" or a network
        // error is retried, because the user may have just fixed either one.
        auto it = m_shared->cache.find(key);
        if (it != m_shared->cache.end()) {
            if (it->second.state == ReputationState::Known ||
                it->second.state == ReputationState::Unknown) {
                return Start::Cached;
            }
            if (it->second.state == ReputationState::Querying) {
                if (done) m_shared->waiting[key].push_back(done);
                return Start::AlreadyRunning;
            }
        }
        // Claim the hash so a second click cannot start a duplicate request.
        ReputationResult pending;
        pending.state = ReputationState::Querying;
        pending.sha256 = key;
        m_shared->cache[key] = pending;
        m_shared->order.push_back(key);
        if (done) m_shared->waiting[key].push_back(done);
    }

    if (m_shared->inFlight.fetch_add(1) >= m_shared->maxInFlight) {
        m_shared->inFlight.fetch_sub(1);
        std::lock_guard<std::mutex> lk(m_shared->mutex);
        m_shared->cache.erase(key);
        m_shared->waiting.erase(key);
        return Start::Rejected;
    }

    std::shared_ptr<Shared> shared = m_shared;
    std::thread([shared, key]() {
        Delivery* delivery = new Delivery;
        delivery->shared = shared;
        delivery->key = key;

        std::vector<ProviderVerdict> verdicts;
        try {
            for (const auto& provider : shared->providers) {
                if (!provider) continue;
                if (shared->cancel.load()) break;

                // A provider that needs a key is skipped when there is none,
                // but the verdict says so instead of pretending it was asked.
                ProviderVerdict verdict;
                const std::string apiKey = loadKeyFor(*provider);
                if (provider->requiresKey() && apiKey.empty()) {
                    verdict.provider = provider->name();
                    verdict.needsKey = true;
                    verdict.summary =
                        provider->name() +
                        " needs an API key (Preferences, Security).";
                } else {
                    verdict = provider->query(key, apiKey, shared->cancel);
                }
                verdicts.push_back(std::move(verdict));
            }
        } catch (const std::exception&) {
            ProviderVerdict verdict;
            verdict.summary = "A reputation provider failed unexpectedly.";
            verdicts.push_back(std::move(verdict));
        } catch (...) {
            ProviderVerdict verdict;
            verdict.summary = "A reputation provider failed unexpectedly.";
            verdicts.push_back(std::move(verdict));
        }

        delivery->result = aggregate(key, verdicts);
        Fl::awake(&ReputationService::deliver, delivery);
    }).detach();
    return Start::Started;
}

// Runs on the UI thread, from Fl::awake().
void ReputationService::deliver(void* data) {
    std::unique_ptr<Delivery> delivery(static_cast<Delivery*>(data));
    const std::shared_ptr<Shared>& shared = delivery->shared;
    if (!shared->alive.load()) return; // the owner is gone; drop it

    shared->inFlight.fetch_sub(1);

    std::vector<Completion> waiters;
    {
        std::lock_guard<std::mutex> lk(shared->mutex);
        shared->cache[delivery->key] = delivery->result;
        auto pendingWaiters = shared->waiting.find(delivery->key);
        if (pendingWaiters != shared->waiting.end()) {
            waiters = std::move(pendingWaiters->second);
            shared->waiting.erase(pendingWaiters);
        }
        while (static_cast<int>(shared->order.size()) > shared->maxCached) {
            shared->cache.erase(shared->order.front());
            shared->order.pop_front();
        }
    }

    for (const Completion& waiter : waiters) {
        if (waiter) waiter(delivery->result);
    }
}
