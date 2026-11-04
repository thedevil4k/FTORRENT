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

// The small print under a reputation answer: the hash itself, so the user can
// check it on their own, or an honest "none" when the torrent metadata carries
// no whole-file SHA-256. It is never tied to a provider having answered: the
// right to know which hash was used does not depend on an API key.
std::string hashSmallPrint(const std::string& sha256Hex,
                           const std::string& path = std::string());

// True only when a provider reported detections for the hash.
bool reputationHasDetections(const ReputationResult& result);

// One line describing what an archive's index holds, for the level-2 answer.
// Never claims the archive is safe, only what its names show.
std::string archiveSummary(const std::string& archivePath,
                           const std::vector<SecurityFile>& entries);

// The line a check shows: the summary, prefixed by the partial-list note when
// the walk stopped early. Kept here so the three surfaces that show the answer
// cannot word it differently.
std::string archiveCheckMessage(const std::string& archivePath,
                                const std::vector<SecurityFile>& entries,
                                const std::string& note);

// Picks the file a reputation lookup may hash on this machine when the torrent
// metadata carries no whole-file hash: it must be complete, it must be able to
// run, and its path must stay inside the torrent. The returned target has an
// empty sha256 and the file's relative path: the caller hashes it. An empty
// path means there is nothing to hash. Never hashes anything by itself.
ReputationTarget findLocalHashTarget(const std::vector<SecurityFile>& files,
                                     const std::vector<bool>& complete);

#endif // SECURITYREPUTATION_H
