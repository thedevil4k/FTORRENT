#include "SecurityReputation.h"
#include "Reputation.h"
#include "SecurityAnalyzer.h"

#include <algorithm>
#include <cctype>
#include <string>

ReputationTarget findReputationTarget(const std::vector<SecurityFile>& files) {
    ReputationTarget fallback;

    for (const SecurityFile& file : files) {
        if (file.padFile) continue;
        if (!file.sha256Complete || !isSha256Hex(file.sha256)) continue;
        if (SecurityAnalyzer::isRunnable(file)) {
            return ReputationTarget{file.sha256, file.path};
        }
        if (!fallback.found()) fallback = ReputationTarget{file.sha256, file.path};
    }
    return fallback;
}

std::string reputationLine(const ReputationResult& result) {
    switch (result.state) {
        case ReputationState::NoCompleteHash:
            // The exact sentence the product requires; see SecurityTypes.h.
            return reputationNoHashMessage();
        case ReputationState::Querying:
            return "Checking the hash with VirusTotal and MalwareBazaar...";
        case ReputationState::Known:
        case ReputationState::Unknown:
        case ReputationState::NoKey:
        case ReputationState::Error:
            return result.message.empty() ? std::string("No answer yet.")
                                          : result.message;
        case ReputationState::NotRequested:
        default:
            return "Click the shield to check this hash with VirusTotal and "
                   "MalwareBazaar.";
    }
}

bool reputationHasDetections(const ReputationResult& result) {
    if (result.state != ReputationState::Known) return false;
    for (const ProviderVerdict& verdict : result.verdicts) {
        if (verdict.malicious > 0 || verdict.suspicious > 0) return true;
    }
    return false;
}

std::string archiveSummary(const std::string& archivePath,
                           const std::vector<SecurityFile>& entries) {
    std::string message = "Archive " + archivePath + ": ";
    if (entries.empty()) {
        return message + "the index lists no files.";
    }

    const SecurityReport report = SecurityAnalyzer::analyze(entries);
    message += std::to_string(entries.size()) + " entries, ";
    if (report.executables > 0 || report.scripts > 0 ||
        report.shortcuts > 0) {
        message += std::to_string(report.executables) + " executable(s), " +
                   std::to_string(report.scripts) + " script(s). " +
                   "Review before downloading.";
    } else {
        message += "no known indicators in their names.";
    }
    return message;
}

namespace {

// True when any path segment is "..", which would leave the torrent folder.
bool hasParentSegment(const std::string& path) {
    std::size_t start = 0;
    while (start <= path.size()) {
        const std::size_t end = path.find_first_of("/\\", start);
        const std::size_t length =
            (end == std::string::npos ? path.size() : end) - start;
        if (length == 2 && path[start] == '.' && path[start + 1] == '.') {
            return true;
        }
        if (end == std::string::npos) break;
        start = end + 1;
    }
    return false;
}

bool staysInsideTorrent(const SecurityFile& file) {
    if (file.path.empty()) return false;
    if (file.symlink || file.absolutePath) return false;
    const char first = file.path.front();
    if (first == '/' || first == '\\') return false;
    if (file.path.size() > 1 &&
        std::isalpha(static_cast<unsigned char>(first)) &&
        file.path[1] == ':') {
        return false;
    }
    return !hasParentSegment(file.path);
}

} // namespace

ReputationTarget findLocalHashTarget(const std::vector<SecurityFile>& files,
                                     const std::vector<bool>& complete) {
    ReputationTarget fallback;
    const std::size_t count = std::min(files.size(), complete.size());

    for (std::size_t i = 0; i < count; ++i) {
        const SecurityFile& file = files[i];
        if (!complete[i]) continue;
        if (file.padFile || file.size <= 0) continue;
        // A recorded path is attacker-controlled; only read from disk when it
        // cannot point outside the torrent's own folder.
        if (!staysInsideTorrent(file)) continue;

        if (SecurityAnalyzer::isRunnable(file)) {
            ReputationTarget target;
            target.path = file.path;
            return target;
        }
        if (fallback.path.empty()) {
            ReputationTarget target;
            target.path = file.path;
            fallback = target;
        }
    }
    return fallback;
}
