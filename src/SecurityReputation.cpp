#include "SecurityReputation.h"
#include "Reputation.h"
#include "SecurityAnalyzer.h"

#include <algorithm>

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
