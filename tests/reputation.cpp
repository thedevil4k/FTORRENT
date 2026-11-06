// Reputation checks.
//
// Two things are being held down here. First, the parsing: a provider answer
// is remote data, and a malformed one must produce an error rather than a
// fabricated verdict. Second, the wording and the state machine: a lookup that
// could not run must never read as "clean", and the no-hash case must show the
// exact sentence the product requires.
//
// The service checks use a fake provider, so nothing here touches the network.
#include "Reputation.h"
#include "ReputationService.h"
#include "SecurityReputation.h"
#include "SecurityTypes.h"

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace {

int checks = 0;
int failures = 0;

void ok(bool condition, const std::string& what) {
    checks++;
    if (!condition) {
        failures++;
        std::cout << "    FAIL " << what << '\n';
    }
}

void section(const char* title) {
    std::cout << "\n== " << title << " ==\n";
}

bool contains(const std::string& text, const std::string& part) {
    return text.find(part) != std::string::npos;
}

const char* const HASH =
    "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855";

std::filesystem::path scratchDir() {
    return std::filesystem::temp_directory_path() / "ftorrent-test-reputation";
}

// Keeps the no-key check from reading a real key the developer may have saved.
void useScratchConfig() {
    namespace fs = std::filesystem;
    const fs::path dir = scratchDir();
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir, ec);
#ifdef _WIN32
    _putenv_s("APPDATA", dir.string().c_str());
#else
    setenv("XDG_CONFIG_HOME", dir.string().c_str(), 1);
#endif
}

class FakeProvider : public ReputationProvider {
public:
    FakeProvider(std::string name, bool requiresKey, ProviderVerdict verdict,
                 int delayMs = 0)
        : m_name(std::move(name))
        , m_requiresKey(requiresKey)
        , m_verdict(std::move(verdict))
        , m_delayMs(delayMs) {}

    std::string name() const override { return m_name; }
    bool requiresKey() const override { return m_requiresKey; }

    ProviderVerdict query(const std::string&, const std::string&,
                          const std::atomic<bool>&) const override {
        if (m_delayMs > 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(m_delayMs));
        }
        return m_verdict;
    }

private:
    std::string m_name;
    bool m_requiresKey;
    ProviderVerdict m_verdict;
    int m_delayMs;
};

ProviderVerdict verdictOf(const std::string& provider, bool answered,
                          bool known, int malicious, int total,
                          const std::string& summary) {
    ProviderVerdict v;
    v.provider = provider;
    v.answered = answered;
    v.known = known;
    v.malicious = malicious;
    v.total = total;
    v.summary = summary;
    return v;
}

// Runs the UI loop until the service has nothing in flight, so the delivery
// through Fl::awake() actually happens. Under headless xvfb there is no real
// event loop to deliver Fl::awake() callbacks, so this only waits for the
// worker thread to finish: once pending() is 0, the result is already in the
// cache and service.find() can read it.
bool waitFor(ReputationService& service, int timeoutMs = 10000) {
    const auto deadline =
        std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
    while (service.pending() > 0) {
        if (std::chrono::steady_clock::now() > deadline) return false;
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    return true;
}

// Runs a lookup and reads the result straight from the cache, without relying
// on Fl::awake() delivery which headless CI cannot guarantee.
ReputationResult waitForResult(ReputationService& service,
                               const std::string& hash) {
    ok(waitFor(service), "the lookup finishes");
    return service.find(hash);
}

} // namespace

int main() {
    useScratchConfig();

    section("hash shape");
    ok(isSha256Hex(HASH), "a 64-character hex digest is a SHA-256");
    ok(!isSha256Hex(std::string(HASH).substr(0, 63)),
       "63 characters is not a SHA-256");
    ok(!isSha256Hex(
           "zzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzz"),
       "non-hex characters are not a SHA-256");
    ok(isSha256Hex(
           "E3B0C44298FC1C149AFBF4C8996FB92427AE41E4649B934CA495991B7852B855"),
       "uppercase hex is accepted");
    ok(normalizeSha256("E3B0") == "e3b0", "normalising lowercases");
    ok(VirusTotalProvider::reportPageUrl(HASH) ==
           std::string("https://www.virustotal.com/gui/file/") + HASH,
       "the report page is built from the hash");
    ok(VirusTotalProvider::reportPageUrl("nope").empty(),
       "a malformed hash has no report page");

    section("VirusTotal parsing");
    {
        const std::string body =
            "{\"data\":{\"type\":\"file\",\"attributes\":{"
            "\"last_analysis_stats\":{\"harmless\":60,\"malicious\":3,"
            "\"suspicious\":1,\"undetected\":6,\"timeout\":0}}}}";
        const ProviderVerdict v = VirusTotalProvider::parseReport(body);
        ok(v.answered, "a full report counts as answered");
        ok(v.known, "a full report has a record");
        ok(v.malicious == 3, "the malicious count is read");
        ok(v.suspicious == 1, "the suspicious count is read");
        ok(v.total == 70, "the engine total is the sum of the buckets");
        ok(contains(v.summary, "3 of 70"), "the summary states the ratio");
        ok(!contains(v.summary, "safe"), "the summary never says safe");
    }
    {
        const ProviderVerdict v = VirusTotalProvider::parseReport("not json");
        ok(!v.answered, "garbage is not an answer");
        ok(!v.known, "garbage does not produce a record");
        ok(contains(v.summary, "could not be read"),
           "garbage is reported as unreadable");
    }
    {
        const ProviderVerdict v =
            VirusTotalProvider::parseReport("{\"data\":{\"attributes\":{}}}");
        ok(!v.answered, "a report with no analysis is not an answer");
        ok(!v.known, "a report with no analysis is not a record");
    }
    {
        const ProviderVerdict v = VirusTotalProvider::parseReport(
            "{\"data\":{\"attributes\":{\"last_analysis_stats\":"
            "{\"harmless\":\"many\"}}}}");
        ok(v.answered, "wrong types in the stats do not break the parse");
        ok(v.malicious == 0, "a non-numeric count is not read as a detection");
    }

    section("MalwareBazaar parsing");
    {
        const ProviderVerdict v = MalwareBazaarProvider::parseReport(
            "{\"query_status\":\"hash_not_found\"}");
        ok(v.answered, "hash_not_found is a real answer");
        ok(!v.known, "hash_not_found is not a record");
        ok(contains(v.summary, "No record"), "hash_not_found says so plainly");
    }
    {
        const ProviderVerdict v = MalwareBazaarProvider::parseReport(
            "{\"query_status\":\"ok\",\"data\":[{\"sha256_hash\":\"" +
            std::string(HASH) +
            "\",\"signature\":\"Emotet\",\"file_type\":\"exe\"}]}");
        ok(v.answered, "a hit is an answer");
        ok(v.known, "a hit is a record");
        ok(contains(v.summary, "Emotet"), "the signature is in the summary");
        ok(contains(v.summary, "malware database"),
           "the summary says where the record comes from");
    }
    {
        // A tag is remote text and must not be able to break a label.
        const ProviderVerdict v = MalwareBazaarProvider::parseReport(
            "{\"query_status\":\"ok\",\"data\":[{\"signature\":"
            "\"Evil\\nFamily\"}]}");
        ok(v.known, "a tag with a control character is still a record");
        ok(!contains(v.summary, "\n"), "the tag's newline is dropped");
    }
    {
        const ProviderVerdict v =
            MalwareBazaarProvider::parseReport("{\"query_status\":\"ok\"}");
        ok(!v.known, "an ok answer with no samples is not a record");
        ok(v.answered, "an ok answer with no samples is still an answer");
    }
    {
        const ProviderVerdict v = MalwareBazaarProvider::parseReport("<<<>>>");
        ok(!v.answered, "garbage is not an answer");
    }

    section("live providers (opt-in)");
    {
        // Off by default: the suite must stay offline for CI and for the
        // "no telemetry" promise. Setting FTORRENT_LIVE_PROVIDERS=1 runs one
        // real MalwareBazaar query; FTORRENT_VT_API_KEY adds VirusTotal.
        // Nothing is uploaded either way: a provider only ever sees a hash.
        const char* live = std::getenv("FTORRENT_LIVE_PROVIDERS");
        if (!live || std::string(live) != "1") {
            std::cout
                << "    skipped: set FTORRENT_LIVE_PROVIDERS=1 to ask the "
                   "real providers\n";
        } else {
            const std::atomic<bool> cancel{false};

            // abuse.ch needs an Auth-Key; the live run reports which way it
            // went rather than pretending the provider is open.
            const char* bazaarKey = std::getenv("FTORRENT_MB_API_KEY");
            MalwareBazaarProvider bazaar;
            const ProviderVerdict bazaarVerdict = bazaar.query(
                HASH, bazaarKey ? bazaarKey : std::string(), cancel);
            std::cout << "    MalwareBazaar: " << bazaarVerdict.summary << '\n';
            if (bazaarKey && *bazaarKey) {
                ok(bazaarVerdict.answered,
                   "MalwareBazaar answered when given an Auth-Key");
            } else {
                ok(!bazaarVerdict.answered &&
                       bazaarVerdict.summary.find("Auth-Key") !=
                           std::string::npos,
                   "without an Auth-Key MalwareBazaar says which key it wants");
            }
            ok(bazaarVerdict.summary.find("safe") == std::string::npos,
               "a live answer never says safe");

            const char* vtKey = std::getenv("FTORRENT_VT_API_KEY");
            if (vtKey && *vtKey) {
                VirusTotalProvider virusTotal;
                const ProviderVerdict vtVerdict =
                    virusTotal.query(HASH, vtKey, cancel);
                std::cout << "    VirusTotal: " << vtVerdict.summary << '\n';
                ok(!vtVerdict.summary.empty(),
                   "VirusTotal gave an answer or a clear reason");
            } else {
                std::cout << "    VirusTotal: skipped (set FTORRENT_VT_API_KEY "
                             "to ask it)\n";
            }
        }
    }

    section("the no-hash sentence");
    {
        const std::string expected =
            "VT no disponible antes de descargar el archivo completo.";
        ok(std::string(reputationNoHashMessage()) == expected,
           "the required wording is exact");
        const ReputationResult r = ReputationService::noHashResult();
        ok(r.state == ReputationState::NoCompleteHash,
           "no hash has its own state");
        ok(r.message == expected, "the no-hash result carries the sentence");
        ok(!contains(r.message, "safe"), "the no-hash result never says safe");
    }

    section("service: a known malicious hash");
    {
        std::vector<std::shared_ptr<ReputationProvider>> providers;
        providers.push_back(std::make_shared<FakeProvider>(
            "VirusTotal", false,
            verdictOf("VirusTotal", true, true, 12, 70,
                      "VirusTotal: 12 of 70 engines flag this file.")));
        providers.push_back(std::make_shared<FakeProvider>(
            "MalwareBazaar", false,
            verdictOf("MalwareBazaar", true, false, 0, 0,
                      "No record in MalwareBazaar for this hash.")));

        ReputationService service(providers);
        ok(service.request(HASH, nullptr) == ReputationService::Start::Started,
           "a valid hash starts a lookup");
        const ReputationResult got = waitForResult(service, HASH);
        ok(got.state == ReputationState::Known, "a detection is Known");
        ok(contains(got.message, "12 of 70"), "the message carries the ratio");
        ok(!contains(got.message, "safe"), "the message never says safe");

        ok(service.request(HASH, nullptr) == ReputationService::Start::Cached,
           "a cached answer is not requested twice");
        ok(service.find(HASH).state == ReputationState::Known,
           "the cached answer is readable");
    }

    section("service: nobody knows the hash");
    {
        std::vector<std::shared_ptr<ReputationProvider>> providers;
        providers.push_back(std::make_shared<FakeProvider>(
            "VirusTotal", false,
            verdictOf("VirusTotal", true, false, 0, 0,
                      "No record in VirusTotal for this hash.")));
        providers.push_back(std::make_shared<FakeProvider>(
            "MalwareBazaar", false,
            verdictOf("MalwareBazaar", true, false, 0, 0,
                      "No record in MalwareBazaar for this hash.")));

        ReputationService service(providers);
        ok(service.request(HASH, nullptr) == ReputationService::Start::Started,
           "a valid hash starts a lookup");
        const ReputationResult got = waitForResult(service, HASH);
        ok(got.state == ReputationState::Unknown, "no record is Unknown");
        ok(contains(got.message, "No record for this hash"),
           "the message says there is no record");
        ok(!contains(got.message, "clean") && !contains(got.message, "safe"),
           "an unknown hash is never called clean or safe");
    }

    section("service: a record with no detections");
    {
        std::vector<std::shared_ptr<ReputationProvider>> providers;
        providers.push_back(std::make_shared<FakeProvider>(
            "VirusTotal", false,
            verdictOf("VirusTotal", true, true, 0, 70,
                      "VirusTotal: 0 of 70 engines flag this file.")));
        ReputationService service(providers);
        ok(service.request(HASH, nullptr) == ReputationService::Start::Started,
           "a valid hash starts a lookup");
        const ReputationResult got = waitForResult(service, HASH);
        ok(got.state == ReputationState::Known,
           "a zero-detection record exists");
        ok(contains(got.message, "not a guarantee"),
           "a clean record is explicitly not a guarantee");
    }

    section("service: a provider that needs a key");
    {
        std::vector<std::shared_ptr<ReputationProvider>> providers;
        // Named VirusTotal and requiresKey(), so the service looks for a key
        // and finds none in the scratch config.
        providers.push_back(std::make_shared<FakeProvider>(
            "VirusTotal", true,
            verdictOf("VirusTotal", true, true, 5, 70, "should not be used")));

        ReputationService service(providers);
        ok(service.request(HASH, nullptr) == ReputationService::Start::Started,
           "a valid hash starts a lookup");
        const ReputationResult got = waitForResult(service, HASH);
        ok(got.state == ReputationState::NoKey,
           "a missing key is its own state, not an error");
        ok(contains(got.message, "API key"), "the message explains the key");
        ok(got.verdicts.size() == 1 && got.verdicts[0].needsKey,
           "the verdict says the key was the blocker");
        ok(got.verdicts.size() == 1 && !got.verdicts[0].known,
           "nothing is looked up without a key");
    }

    section("service: bad input and limits");
    {
        std::vector<std::shared_ptr<ReputationProvider>> providers;
        providers.push_back(std::make_shared<FakeProvider>(
            "VirusTotal", false,
            verdictOf("VirusTotal", true, false, 0, 0, "No record.")));
        ReputationService service(providers);

        ok(service.request("", nullptr) == ReputationService::Start::Rejected,
           "an empty hash starts nothing");
        ok(service.request("not-a-hash", nullptr) ==
               ReputationService::Start::Rejected,
           "a malformed hash starts nothing");
        ok(service.pending() == 0, "nothing was started");

        ok(service.request(HASH, nullptr) == ReputationService::Start::Started,
           "a valid hash starts a lookup");
        ok(service.request(HASH, nullptr) ==
               ReputationService::Start::AlreadyRunning,
           "the same hash in flight is not started twice");
        ok(waitFor(service), "the lookup finishes");

        service.clear();
        ok(service.find(HASH).state == ReputationState::NotRequested,
           "clearing forgets the cached answer");
        ok(service.find("").state == ReputationState::NotRequested,
           "an empty hash was never asked about");
    }

    section("choosing what to ask about");
    {
        SecurityFile picture;
        picture.path = "photo.jpg";
        picture.size = 100;
        picture.sha256 = HASH;
        picture.sha256Complete = true;

        SecurityFile tool;
        tool.path = "setup.exe";
        tool.size = 200;
        tool.sha256 = std::string(64, 'a');
        tool.sha256Complete = true;

        SecurityFile big;
        big.path = "movie.mp4";
        big.size = 900000;
        big.sha256 = std::string(64, 'b');
        big.sha256Complete = false; // a Merkle root, not a file hash

        ReputationTarget target = findReputationTarget({picture, big});
        ok(target.found(), "a complete hash is found");
        ok(target.sha256 == HASH, "the first complete hash is used");

        target = findReputationTarget({picture, tool});
        ok(target.sha256 == tool.sha256,
           "a runnable file is preferred over a picture");
        ok(target.path == "setup.exe",
           "the target names the file it belongs to");

        target = findReputationTarget({big});
        ok(!target.found(), "a hash that is not the whole file is never used");

        target = findReputationTarget({});
        ok(!target.found(), "an empty list has nothing to ask about");
    }

    section("archive summary");
    {
        ok(contains(archiveSummary("pack.zip", {}), "no files"),
           "an empty index says so");

        SecurityFile readme;
        readme.path = "docs/readme.txt";
        const std::string quiet = archiveSummary("pack.zip", {readme});
        ok(contains(quiet, "1 entries"), "the entry count is stated");
        ok(contains(quiet, "no known indicators"),
           "a harmless index is worded carefully");
        ok(!contains(quiet, "safe"), "a harmless index never says safe");

        SecurityFile tool;
        tool.path = "setup.exe";
        const std::string loud = archiveSummary("pack.zip", {readme, tool});
        ok(contains(loud, "1 executable"), "an executable inside is counted");
        ok(contains(loud, "Review before downloading"),
           "an executable inside asks for review");

        // The lock badge and this line answer the same question: the index
        // says the entry needs a password, so what is behind it cannot be
        // checked before the download.
        SecurityFile locked;
        locked.path = "private/photo.jpg";
        locked.encrypted = true;
        const std::string guarded =
            archiveSummary("pack.zip", {readme, locked});
        ok(contains(guarded, "1 entry is password-protected"),
           "a password-protected entry is stated in the summary");
        ok(contains(guarded, "cannot be inspected"),
           "and the summary says its content cannot be checked");
        ok(!contains(guarded, "safe"),
           "an encrypted archive is never called safe");

        SecurityFile locked2 = locked;
        locked2.path = "private/notes.txt";
        const std::string two = archiveSummary("pack.zip", {locked, locked2});
        ok(contains(two, "2 entries are password-protected"),
           "the plural wording follows the count");
    }

    section("the hash in the small print");
    {
        // The user is entitled to know which hash was used, or that none was
        // found. This line never depends on a provider having answered.
        const std::string shown = hashSmallPrint(HASH, "setup.exe");
        ok(contains(shown, HASH),
           "the whole hash is printed, not a truncated one");
        ok(contains(shown, "setup.exe"), "and it names the file it belongs to");
        ok(!contains(shown, "safe"),
           "the small print never claims anything is safe");

        const std::string withPath = hashSmallPrint(HASH);
        ok(contains(withPath, HASH), "a hash with no path still prints");

        const std::string none = hashSmallPrint("", "");
        ok(contains(none, "none in the torrent metadata"),
           "with no whole-file hash the line says so");
        ok(contains(none, "never downloaded just to compute one"),
           "and says the file is not downloaded to obtain it");
        ok(!contains(none, "safe"), "and never says safe either");
    }

    section("what may be hashed locally");
    {
        SecurityFile doneExe;
        doneExe.path = "setup.exe";
        doneExe.size = 1000;
        SecurityFile pendingExe;
        pendingExe.path = "pending.exe";
        pendingExe.size = 1000;
        SecurityFile escaping;
        escaping.path = "../escape.exe";
        escaping.size = 10;
        SecurityFile absolute;
        absolute.path = "C:/Windows/evil.exe";
        absolute.size = 10;
        absolute.absolutePath = true;

        ReputationTarget target =
            findLocalHashTarget({pendingExe, doneExe}, {false, true});
        ok(target.path == "setup.exe", "a completed runnable file is chosen");
        ok(target.sha256.empty(),
           "a local target has no hash yet: the caller must compute it");

        target = findLocalHashTarget({doneExe}, {false});
        ok(target.path.empty(), "an unfinished file is never hashed");

        target = findLocalHashTarget({escaping, absolute}, {true, true});
        ok(target.path.empty(),
           "a path that leaves the torrent folder is never read");

        SecurityFile text;
        text.path = "readme.txt";
        text.size = 100;
        target = findLocalHashTarget({doneExe, text}, {false, true});
        ok(target.path == "readme.txt",
           "a completed non-runnable file is the fallback");

        target = findLocalHashTarget({}, {});
        ok(target.path.empty(), "nothing to hash when there is no file list");
    }

    section("display wording");
    {
        ok(reputationLine(ReputationService::noHashResult()) ==
               std::string(reputationNoHashMessage()),
           "the no-hash line is the required sentence");

        const std::string notAsked = reputationLine(ReputationResult{});
        ok(contains(notAsked, "shield"), "an unasked lookup says how to ask");
        ok(!contains(notAsked, "safe"), "an unasked lookup never says safe");

        ReputationResult querying;
        querying.state = ReputationState::Querying;
        ok(contains(reputationLine(querying), "Checking"),
           "a running lookup says it is checking");

        ReputationResult hit;
        hit.state = ReputationState::Known;
        ProviderVerdict verdict;
        verdict.provider = "VirusTotal";
        verdict.answered = true;
        verdict.known = true;
        verdict.malicious = 4;
        verdict.total = 70;
        hit.verdicts.push_back(verdict);
        ok(reputationHasDetections(hit), "a detection is reported as such");

        ReputationResult miss;
        miss.state = ReputationState::Unknown;
        ok(!reputationHasDetections(miss), "no record is not a detection");

        ReputationResult clean;
        clean.state = ReputationState::Known;
        ProviderVerdict zero;
        zero.answered = true;
        zero.known = true;
        zero.malicious = 0;
        zero.total = 70;
        clean.verdicts.push_back(zero);
        ok(!reputationHasDetections(clean),
           "a zero-detection record is not a detection");
    }

    std::cout << "\nreputation checks=" << checks << " failures=" << failures
              << '\n';

    std::error_code ec;
    std::filesystem::remove_all(scratchDir(), ec);
    return failures ? 1 : 0;
}
