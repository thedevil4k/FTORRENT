#ifndef SECURITYREPUTATION_H
#define SECURITYREPUTATION_H

#include "SecurityTypes.h"

#include <string>
#include <vector>

/**
 * @brief Which hash a shield click should ask about, and how to word the answer.
 *
 * Kept apart from the providers so the decision ("is there anything worth
 * asking about, and what do we say when there is not") can be read and tested
 * without any network or widget state.
 */
struct ReputationTarget {
    std::string sha256;   // Lowercase hex, empty when nothing can be asked
    std::string path;     // File the hash belongs to

    bool found() const { return !sha256.empty(); }
};

// Picks the file a shield click asks about. A file that can run is preferred,
// because that is the file whose reputation changes a decision; an archive or
// a text file is only used when nothing runnable carries a hash.
ReputationTarget findReputationTarget(const std::vector<SecurityFile>& files);

// One line for a panel, a row or a tooltip. Returns the exact required wording
// when no whole-file hash exists, and never claims a file is safe.
std::string reputationLine(const ReputationResult& result);

// True only when a provider reported detections for the hash.
bool reputationHasDetections(const ReputationResult& result);

#endif // SECURITYREPUTATION_H
