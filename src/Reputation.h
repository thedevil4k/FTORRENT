#ifndef REPUTATION_H
#define REPUTATION_H

#include "SecurityTypes.h"

#include <atomic>
#include <string>

/**
 * @brief One external reputation service, queried by hash only.
 *
 * Providers never receive a file, a name or a BitTorrent infohash: only the
 * 64 lowercase hex characters of a whole-file SHA-256. Nothing is uploaded and
 * nothing is queried unless the user asked for it.
 */
class ReputationProvider {
public:
    virtual ~ReputationProvider() = default;

    virtual std::string name() const = 0;

    // True when the provider cannot answer without an API key.
    virtual bool requiresKey() const = 0;

    // Looks one hash up. Never throws and never blocks for longer than the
    // HTTP timeout. 'cancel' is polled while the request runs.
    virtual ProviderVerdict query(const std::string& sha256,
                                  const std::string& apiKey,
                                  const std::atomic<bool>& cancel) const = 0;
};

/** @brief True for exactly 64 hexadecimal characters. */
bool isSha256Hex(const std::string& text);

/** @brief Lowercases a hash this app produced. */
std::string normalizeSha256(const std::string& text);

class VirusTotalProvider : public ReputationProvider {
public:
    std::string name() const override { return "VirusTotal"; }
    bool requiresKey() const override { return true; }

    ProviderVerdict query(const std::string& sha256, const std::string& apiKey,
                          const std::atomic<bool>& cancel) const override;

    // Parses a /api/v3/files/<hash> body. Public so the parsing can be tested
    // against canned answers without touching the network.
    static ProviderVerdict parseReport(const std::string& body);

    // https://www.virustotal.com/gui/file/<hash>, empty for a bad hash.
    static std::string reportPageUrl(const std::string& sha256);
};

class MalwareBazaarProvider : public ReputationProvider {
public:
    std::string name() const override { return "MalwareBazaar"; }
    bool requiresKey() const override { return false; }

    ProviderVerdict query(const std::string& sha256, const std::string& apiKey,
                          const std::atomic<bool>& cancel) const override;

    // Parses a get_info answer (query_status ok / hash_not_found / ...).
    static ProviderVerdict parseReport(const std::string& body);
};

#endif // REPUTATION_H
