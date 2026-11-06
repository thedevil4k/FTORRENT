#include "Reputation.h"
#include "HttpClient.h"
#include "Version.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <string>
#include <utility>
#include <vector>

namespace {

const std::string USER_AGENT = Version::httpUserAgent();

const char* const VIRUSTOTAL_FILE_API =
    "https://www.virustotal.com/api/v3/files/";
const char* const MALWAREBAZAAR_API = "https://mb-api.abuse.ch/api/v1/";

// A tag or a signature is remote text. Control characters would let it break a
// label in two, so they are dropped before the string is ever shown.
std::string oneLine(const std::string& text, std::size_t maxLength = 120) {
    std::string out;
    out.reserve(std::min(text.size(), maxLength));
    for (char ch : text) {
        const unsigned char byte = static_cast<unsigned char>(ch);
        if (byte < 0x20 || byte == 0x7f) {
            out.push_back(' ');
            continue;
        }
        out.push_back(ch);
        if (out.size() >= maxLength) break;
    }
    return out;
}

// ---------------------------------------------------------------------------
// A deliberately small JSON reader.
//
// Only what these two answers need: objects, arrays, strings, numbers, booleans
// and null. It is recursive but capped in depth, never trusts a length, and
// returns false on anything malformed, so a provider answering with garbage
// produces an error instead of a fabricated verdict.
// ---------------------------------------------------------------------------

struct Json {
    enum class Type { Null, Bool, Number, String, Array, Object };

    Type type = Type::Null;
    bool boolean = false;
    double number = 0;
    std::string text;
    std::vector<Json> items;
    std::vector<std::pair<std::string, Json>> members;

    const Json* member(const char* key) const {
        for (const auto& entry : members) {
            if (entry.first == key) return &entry.second;
        }
        return nullptr;
    }

    double numberOr(double fallback) const {
        return type == Type::Number ? number : fallback;
    }
};

class JsonParser {
public:
    explicit JsonParser(const std::string& text)
        : m_text(text) {}

    bool parse(Json& out) {
        skipSpace();
        if (!value(out, 0)) return false;
        skipSpace();
        return m_pos == m_text.size();
    }

private:
    static const int MAX_DEPTH = 64;

    void skipSpace() {
        while (m_pos < m_text.size()) {
            const char ch = m_text[m_pos];
            if (ch != ' ' && ch != '\t' && ch != '\n' && ch != '\r') break;
            ++m_pos;
        }
    }

    bool literal(const char* word) {
        const std::size_t length = std::char_traits<char>::length(word);
        if (m_text.compare(m_pos, length, word) != 0) return false;
        m_pos += length;
        return true;
    }

    bool value(Json& out, int depth) {
        if (depth > MAX_DEPTH || m_pos >= m_text.size()) return false;
        skipSpace();
        if (m_pos >= m_text.size()) return false;

        const char ch = m_text[m_pos];
        switch (ch) {
            case '{':
                return object(out, depth);
            case '[':
                return array(out, depth);
            case '"':
                out.type = Json::Type::String;
                return string(out.text);
            case 't':
                if (!literal("true")) return false;
                out.type = Json::Type::Bool;
                out.boolean = true;
                return true;
            case 'f':
                if (!literal("false")) return false;
                out.type = Json::Type::Bool;
                out.boolean = false;
                return true;
            case 'n':
                if (!literal("null")) return false;
                out.type = Json::Type::Null;
                return true;
            default:
                return number(out);
        }
    }

    bool object(Json& out, int depth) {
        out.type = Json::Type::Object;
        ++m_pos; // '{'
        skipSpace();
        if (m_pos < m_text.size() && m_text[m_pos] == '}') {
            ++m_pos;
            return true;
        }
        while (m_pos < m_text.size()) {
            skipSpace();
            if (m_pos >= m_text.size() || m_text[m_pos] != '"') return false;
            std::string key;
            if (!string(key)) return false;
            skipSpace();
            if (m_pos >= m_text.size() || m_text[m_pos] != ':') return false;
            ++m_pos;
            Json child;
            if (!value(child, depth + 1)) return false;
            out.members.emplace_back(std::move(key), std::move(child));
            skipSpace();
            if (m_pos >= m_text.size()) return false;
            if (m_text[m_pos] == ',') {
                ++m_pos;
                continue;
            }
            if (m_text[m_pos] == '}') {
                ++m_pos;
                return true;
            }
            return false;
        }
        return false;
    }

    bool array(Json& out, int depth) {
        out.type = Json::Type::Array;
        ++m_pos; // '['
        skipSpace();
        if (m_pos < m_text.size() && m_text[m_pos] == ']') {
            ++m_pos;
            return true;
        }
        while (m_pos < m_text.size()) {
            Json child;
            if (!value(child, depth + 1)) return false;
            out.items.push_back(std::move(child));
            skipSpace();
            if (m_pos >= m_text.size()) return false;
            if (m_text[m_pos] == ',') {
                ++m_pos;
                continue;
            }
            if (m_text[m_pos] == ']') {
                ++m_pos;
                return true;
            }
            return false;
        }
        return false;
    }

    static void appendUtf8(std::string& out, unsigned int code) {
        if (code <= 0x7F) {
            out.push_back(static_cast<char>(code));
        } else if (code <= 0x7FF) {
            out.push_back(static_cast<char>(0xC0 | (code >> 6)));
            out.push_back(static_cast<char>(0x80 | (code & 0x3F)));
        } else if (code <= 0xFFFF) {
            out.push_back(static_cast<char>(0xE0 | (code >> 12)));
            out.push_back(static_cast<char>(0x80 | ((code >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (code & 0x3F)));
        } else {
            out.push_back(static_cast<char>(0xF0 | (code >> 18)));
            out.push_back(static_cast<char>(0x80 | ((code >> 12) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | ((code >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (code & 0x3F)));
        }
    }

    bool hex4(unsigned int& out) {
        if (m_pos + 4 > m_text.size()) return false;
        out = 0;
        for (int i = 0; i < 4; ++i) {
            const char ch = m_text[m_pos + static_cast<std::size_t>(i)];
            unsigned int digit = 0;
            if (ch >= '0' && ch <= '9')
                digit = static_cast<unsigned int>(ch - '0');
            else if (ch >= 'a' && ch <= 'f')
                digit = static_cast<unsigned int>(ch - 'a' + 10);
            else if (ch >= 'A' && ch <= 'F')
                digit = static_cast<unsigned int>(ch - 'A' + 10);
            else return false;
            out = (out << 4) | digit;
        }
        m_pos += 4;
        return true;
    }

    bool string(std::string& out) {
        if (m_pos >= m_text.size() || m_text[m_pos] != '"') return false;
        ++m_pos;
        out.clear();
        while (m_pos < m_text.size()) {
            const char ch = m_text[m_pos++];
            if (ch == '"') return true;
            if (ch != '\\') {
                out.push_back(ch);
                continue;
            }
            if (m_pos >= m_text.size()) return false;
            const char escape = m_text[m_pos++];
            switch (escape) {
                case '"':
                    out.push_back('"');
                    break;
                case '\\':
                    out.push_back('\\');
                    break;
                case '/':
                    out.push_back('/');
                    break;
                case 'b':
                    out.push_back('\b');
                    break;
                case 'f':
                    out.push_back('\f');
                    break;
                case 'n':
                    out.push_back('\n');
                    break;
                case 'r':
                    out.push_back('\r');
                    break;
                case 't':
                    out.push_back('\t');
                    break;
                case 'u': {
                    unsigned int code = 0;
                    if (!hex4(code)) return false;
                    // A surrogate pair is two escapes; combine them so a tag
                    // with an emoji does not turn into two broken halves.
                    if (code >= 0xD800 && code <= 0xDBFF &&
                        m_pos + 1 < m_text.size() && m_text[m_pos] == '\\' &&
                        m_text[m_pos + 1] == 'u') {
                        m_pos += 2;
                        unsigned int low = 0;
                        if (!hex4(low)) return false;
                        if (low >= 0xDC00 && low <= 0xDFFF) {
                            code = 0x10000 + ((code - 0xD800) << 10) +
                                   (low - 0xDC00);
                        }
                    }
                    appendUtf8(out, code);
                    break;
                }
                default:
                    return false;
            }
        }
        return false;
    }

    bool number(Json& out) {
        const std::size_t start = m_pos;
        if (m_pos < m_text.size() && m_text[m_pos] == '-') ++m_pos;
        bool digits = false;
        while (m_pos < m_text.size() &&
               std::isdigit(static_cast<unsigned char>(m_text[m_pos]))) {
            ++m_pos;
            digits = true;
        }
        if (m_pos < m_text.size() && m_text[m_pos] == '.') {
            ++m_pos;
            while (m_pos < m_text.size() &&
                   std::isdigit(static_cast<unsigned char>(m_text[m_pos]))) {
                ++m_pos;
                digits = true;
            }
        }
        if (!digits) return false;
        out.type = Json::Type::Number;
        out.number =
            std::strtod(m_text.substr(start, m_pos - start).c_str(), nullptr);
        return true;
    }

    const std::string& m_text;
    std::size_t m_pos = 0;
};

int countOf(const Json* object, const char* key) {
    if (!object) return 0;
    const Json* field = object->member(key);
    if (!field) return 0;
    const double value = field->numberOr(0);
    if (value < 0) return 0;
    return static_cast<int>(value);
}

} // namespace

bool isSha256Hex(const std::string& text) {
    if (text.size() != 64) return false;
    for (unsigned char ch : text) {
        if (!std::isxdigit(ch)) return false;
    }
    return true;
}

std::string normalizeSha256(const std::string& text) {
    std::string out = text;
    for (char& ch : out) {
        ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    }
    return out;
}

std::string VirusTotalProvider::reportPageUrl(const std::string& sha256) {
    if (!isSha256Hex(sha256)) return {};
    return "https://www.virustotal.com/gui/file/" + normalizeSha256(sha256);
}

ProviderVerdict VirusTotalProvider::parseReport(const std::string& body) {
    ProviderVerdict verdict;
    verdict.provider = "VirusTotal";

    Json root;
    JsonParser parser(body);
    if (!parser.parse(root) || root.type != Json::Type::Object) {
        verdict.summary = "VirusTotal sent an answer that could not be read.";
        return verdict;
    }

    const Json* data = root.member("data");
    const Json* attributes = data ? data->member("attributes") : nullptr;
    const Json* stats =
        attributes ? attributes->member("last_analysis_stats") : nullptr;
    if (!stats || stats->type != Json::Type::Object) {
        verdict.summary = "VirusTotal sent an answer with no analysis.";
        return verdict;
    }

    verdict.answered = true;
    verdict.known = true;
    verdict.malicious = countOf(stats, "malicious");
    verdict.suspicious = countOf(stats, "suspicious");
    verdict.total = verdict.malicious + verdict.suspicious +
                    countOf(stats, "harmless") + countOf(stats, "undetected") +
                    countOf(stats, "timeout");

    verdict.summary = "VirusTotal: " + std::to_string(verdict.malicious) +
                      " of " + std::to_string(verdict.total) +
                      " engines flag this file";
    if (verdict.suspicious > 0) {
        verdict.summary +=
            ", " + std::to_string(verdict.suspicious) + " suspicious";
    }
    verdict.summary += ".";
    return verdict;
}

ProviderVerdict
VirusTotalProvider::query(const std::string& sha256, const std::string& apiKey,
                          const std::atomic<bool>& cancel) const {
    ProviderVerdict verdict;
    verdict.provider = "VirusTotal";

    if (!isSha256Hex(sha256)) {
        verdict.summary =
            "VirusTotal was not asked: the hash is not a SHA-256.";
        return verdict;
    }
    if (apiKey.empty()) {
        verdict.summary =
            "VirusTotal needs an API key (Preferences, Security).";
        return verdict;
    }

    const HttpClient::Response res = HttpClient::get(
        std::string(VIRUSTOTAL_FILE_API) + normalizeSha256(sha256), USER_AGENT,
        15, [&cancel]() { return cancel.load(); }, nullptr, 10,
        {{"x-apikey", apiKey}}, false);

    if (res.httpStatus == 404) {
        verdict.answered = true;
        verdict.summary = "No record in VirusTotal for this hash.";
        return verdict;
    }
    if (res.httpStatus == 401 || res.httpStatus == 403) {
        verdict.summary = "VirusTotal rejected the API key.";
        return verdict;
    }
    if (res.httpStatus == 429) {
        verdict.summary =
            "VirusTotal's rate limit was reached. Try again later.";
        return verdict;
    }
    if (!res.ok) {
        verdict.summary = "VirusTotal could not be reached: " + res.error;
        return verdict;
    }
    return parseReport(res.body);
}

ProviderVerdict MalwareBazaarProvider::parseReport(const std::string& body) {
    ProviderVerdict verdict;
    verdict.provider = "MalwareBazaar";

    Json root;
    JsonParser parser(body);
    if (!parser.parse(root) || root.type != Json::Type::Object) {
        verdict.summary =
            "MalwareBazaar sent an answer that could not be read.";
        return verdict;
    }

    const Json* status = root.member("query_status");
    const std::string state = status ? status->text : std::string();

    if (state == "hash_not_found" || state == "no_results" ||
        state == "file_not_found") {
        verdict.answered = true;
        verdict.summary = "No record in MalwareBazaar for this hash.";
        return verdict;
    }
    if (state != "ok") {
        verdict.summary = state.empty()
                              ? "MalwareBazaar sent an answer with no status."
                              : "MalwareBazaar answered: " + oneLine(state, 40);
        return verdict;
    }

    const Json* data = root.member("data");
    if (!data || data->type != Json::Type::Array || data->items.empty()) {
        verdict.answered = true;
        verdict.summary = "No record in MalwareBazaar for this hash.";
        return verdict;
    }

    verdict.answered = true;
    verdict.known = true;

    const Json* sample = &data->items.front();
    const Json* signature = sample->member("signature");
    std::string tag = signature ? oneLine(signature->text) : std::string();
    if (tag.empty()) tag = "no signature";

    verdict.summary =
        "MalwareBazaar: this file is in the malware database (" + tag + ").";
    return verdict;
}

ProviderVerdict
MalwareBazaarProvider::query(const std::string& sha256,
                             const std::string& apiKey,
                             const std::atomic<bool>& cancel) const {
    ProviderVerdict verdict;
    verdict.provider = "MalwareBazaar";

    if (!isSha256Hex(sha256)) {
        verdict.summary =
            "MalwareBazaar was not asked: the hash is not a SHA-256.";
        return verdict;
    }
    if (apiKey.empty()) {
        verdict.summary =
            "MalwareBazaar needs an Auth-Key (Preferences, Security).";
        return verdict;
    }

    const HttpClient::Response res = HttpClient::postForm(
        MALWAREBAZAAR_API, USER_AGENT,
        "query=get_info&hash=" + normalizeSha256(sha256), 15,
        [&cancel]() { return cancel.load(); }, 10,
        {{"Content-Type", "application/x-www-form-urlencoded"},
         {"Auth-Key", apiKey}},
        false);

    if (res.httpStatus == 401 || res.httpStatus == 403) {
        verdict.summary = "MalwareBazaar rejected the Auth-Key.";
        return verdict;
    }
    if (!res.ok) {
        verdict.summary = "MalwareBazaar could not be reached: " + res.error;
        return verdict;
    }
    return parseReport(res.body);
}
