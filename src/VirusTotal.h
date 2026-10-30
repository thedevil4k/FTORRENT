#ifndef VIRUSTOTAL_H
#define VIRUSTOTAL_H

#include <string>

/**
 * @brief VirusTotal links built from a local hash.
 *
 * FTorrent does not query the VirusTotal API and does not upload files. This
 * builds the normal report page for a SHA-256 the caller already computed; the
 * current build has no UI that opens it, so the engine is kept for a later
 * pre-download phase.
 */
class VirusTotal {
public:
    /**
     * @brief Build https://www.virustotal.com/gui/file/<sha256>.
     *
     * @param sha256Hex 64 hexadecimal characters; case is normalized.
     * @return The URL, or an empty string when the digest is malformed.
     */
    static std::string fileReportUrl(const std::string& sha256Hex);
};

#endif // VIRUSTOTAL_H
