#include "VirusTotal.h"

#include <cctype>

std::string VirusTotal::fileReportUrl(const std::string& sha256Hex)
{
    if (sha256Hex.size() != 64) {
        return {};
    }

    std::string normalized;
    normalized.reserve(64);
    for (unsigned char c : sha256Hex) {
        if (!std::isxdigit(c)) {
            return {};
        }
        normalized.push_back(static_cast<char>(std::tolower(c)));
    }

    return "https://www.virustotal.com/gui/file/" + normalized;
}
