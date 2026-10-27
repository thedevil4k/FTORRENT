#ifndef VIRUSTOTAL_H
#define VIRUSTOTAL_H

#include <string>

/**
 * @brief VirusTotal links built from a local hash.
 *
 * FTorrent does not query the VirusTotal API and does not upload files. The
 * user clicked a button, so the least surprising and most private behaviour is
 * to open the normal report page for the SHA-256 we already computed.
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
