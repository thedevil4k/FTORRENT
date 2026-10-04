#include "SearchEngine.h"
#include "HttpClient.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <iterator>
#include <mutex>
#include <regex>
#include <set>
#include <sstream>
#include <thread>

namespace {

// URL-encodes a query string (%s slot of the engine URL)
std::string urlEncode(const std::string& in) {
    static const char* hex = "0123456789ABCDEF";
    std::string out;
    out.reserve(in.size() * 3);
    for (unsigned char c : in) {
        if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
            out += static_cast<char>(c);
        } else {
            out += '%';
            out += hex[c >> 4];
            out += hex[c & 0x0f];
        }
    }
    return out;
}

// Decodes %XX sequences, as found in the dn= field of a magnet link.
std::string urlDecode(const std::string& in) {
    std::string out;
    out.reserve(in.size());
    for (size_t i = 0; i < in.size(); ++i) {
        if (in[i] == '%' && i + 2 < in.size() &&
            std::isxdigit((unsigned char)in[i+1]) && std::isxdigit((unsigned char)in[i+2])) {
            auto hex = [](char c) -> int {
                if (c >= '0' && c <= '9') return c - '0';
                if (c >= 'a' && c <= 'f') return c - 'a' + 10;
                return c - 'A' + 10;
            };
            out += static_cast<char>(hex(in[i+1]) * 16 + hex(in[i+2]));
            i += 2;
        } else if (in[i] == '+') {
            out += ' ';
        } else {
            out += in[i];
        }
    }
    return out;
}

// True for a bare BitTorrent info hash: 40 hex characters (SHA-1) or 32 for the
// older SHA-256 truncated form. APIs return these where a magnet link is expected.
bool isInfoHash(const std::string& value) {
    if (value.size() != 40 && value.size() != 32) return false;
    for (char c : value) {
        if (!std::isxdigit((unsigned char)c)) return false;
    }
    return true;
}

// Turns the HTML entities that show up inside href attributes back into the
// characters they stand for. Several sites escape the "=" and ":" of a magnet
// link ("magnet:?xt&#x3D;urn:btih:..."), which is invisible in a browser but
// makes the link unmatchable.
std::string decodeHtmlEntities(const std::string& in) {
    std::string out;
    out.reserve(in.size());
    
    for (size_t i = 0; i < in.size(); ++i) {
        if (in[i] != '&') {
            out += in[i];
            continue;
        }
        
        // &#x3D; / &#61; / &#58; : numeric, hexadecimal or decimal
        if (in.compare(i, 3, "&#x") == 0 || in.compare(i, 3, "&#X") == 0) {
            size_t end = in.find(';', i + 3);
            if (end != std::string::npos && end - (i + 3) <= 6) {
                std::string digits = in.substr(i + 3, end - i - 3);
                bool hex = true;
                for (char c : digits) {
                    if (!std::isxdigit((unsigned char)c)) { hex = false; break; }
                }
                if (hex && !digits.empty()) {
                    out += static_cast<char>(std::strtol(digits.c_str(), nullptr, 16));
                    i = end;
                    continue;
                }
            }
        }
        if (in.compare(i, 2, "&#") == 0) {
            size_t end = in.find(';', i + 2);
            if (end != std::string::npos && end - (i + 2) <= 8) {
                std::string digits = in.substr(i + 2, end - i - 2);
                bool digitsOnly = !digits.empty();
                for (char c : digits) {
                    if (!std::isdigit((unsigned char)c)) { digitsOnly = false; break; }
                }
                if (digitsOnly) {
                    long value = std::strtol(digits.c_str(), nullptr, 10);
                    // Only the punctuation that appears in a magnet link is
                    // decoded; anything else is left untouched so that a title
                    // containing "&#8230;" does not turn into an ellipsis.
                    if (value == 61 || value == 58 || value == 38) {
                        out += static_cast<char>(value);
                        i = end;
                        continue;
                    }
                }
            }
        }
        
        static const struct { const char* entity; char ch; } named[] = {
            { "&amp;", '&' }, { "&quot;", '"' }, { "&apos;", '\'' },
            { "&lt;", '<' }, { "&gt;", '>' }, { "&colon;", ':' }, { "&equals;", '=' },
        };
        bool replaced = false;
        for (const auto& n : named) {
            size_t len = std::strlen(n.entity);
            if (in.compare(i, len, n.entity) == 0) {
                out += n.ch;
                i += len - 1;
                replaced = true;
                break;
            }
        }
        if (!replaced) out += in[i];
    }
    return out;
}

std::string trim(const std::string& s) {
    size_t b = s.find_first_not_of(" \t\r\n");
    if (b == std::string::npos) return "";
    size_t e = s.find_last_not_of(" \t\r\n");
    return s.substr(b, e - b + 1);
}

// Very small helper: minimal JSON string/number extraction by dotted path.
// Deliberately not a full JSON parser: the engines only need flat fields.
bool jsonField(const std::string& json, const std::string& path, std::string& out) {
    std::string key = path;
    size_t start = 0;
    std::string value;
    bool found = false;
    
    while (!key.empty()) {
        size_t dot = key.find('.');
        std::string part = (dot == std::string::npos) ? key : key.substr(0, dot);
        key = (dot == std::string::npos) ? "" : key.substr(dot + 1);
        
        std::string needle = "\"" + part + "\"";
        size_t p = json.find(needle, start);
        if (p == std::string::npos) return false;
        p = json.find(':', p + needle.size());
        if (p == std::string::npos) return false;
        ++p;
        while (p < json.size() && std::isspace((unsigned char)json[p])) ++p;
        
        std::string val;
        if (p < json.size() && json[p] == '"') {
            size_t e = json.find('"', p + 1);
            if (e == std::string::npos) return false;
            val = json.substr(p + 1, e - p - 1);
            start = e + 1;
        } else if (p < json.size() && (json[p] == '{' || json[p] == '[')) {
            // Nested object/array: scan with depth counting
            char open = json[p];
            char close = (open == '{') ? '}' : ']';
            int depth = 0;
            size_t q = p;
            for (; q < json.size(); ++q) {
                if (json[q] == open) ++depth;
                else if (json[q] == close) { --depth; if (depth == 0) break; }
            }
            if (q >= json.size()) return false;
            val = json.substr(p, q - p + 1);
            start = q + 1;
        } else {
            size_t e = json.find_first_of(",}]", p);
            if (e == std::string::npos) return false;
            val = json.substr(p, e - p);
            start = e;
        }
        value = val;
        found = true;
    }
    
    if (found) out = value;
    return found;
}

} // namespace

std::string SearchEngine::buildUrl(const std::string& query,
                                    const std::string& category,
                                    int page) const {
    std::string url = m_def.urlTemplate;
    size_t pos = url.find("%s");
    if (pos != std::string::npos) {
        url.replace(pos, 2, urlEncode(query));
    }
    pos = url.find("%c");
    if (pos != std::string::npos) {
        url.replace(pos, 2, categoryCode(category));
    }
    pos = url.find("%p");
    if (pos != std::string::npos) {
        url.replace(pos, 2, std::to_string(page > 0 ? page : 1));
    }
    return url;
}

std::string SearchEngine::buildProxyUrl(const std::string& query,
                                        const std::string& category,
                                        int page) const {
    if (m_def.proxyTemplate.empty()) return "";
    std::string url = m_def.proxyTemplate;
    size_t pos = url.find("%s");
    if (pos != std::string::npos) {
        url.replace(pos, 2, urlEncode(query));
    }
    pos = url.find("%c");
    if (pos != std::string::npos) {
        url.replace(pos, 2, categoryCode(category));
    }
    pos = url.find("%p");
    if (pos != std::string::npos) {
        url.replace(pos, 2, std::to_string(page > 0 ? page : 1));
    }
    return url;
}

// The code the site expects for a dropdown category ("Movies" -> "201").
// Falls back to the "all" entry, then to "0", so a stale selection can never
// produce a broken URL.
std::string SearchEngine::categoryCode(const std::string& category) const {
    auto it = m_def.categories.find(category);
    if (it != m_def.categories.end()) return it->second;
    it = m_def.categories.find("all");
    if (it != m_def.categories.end()) return it->second;
    it = m_def.categories.find("All");
    if (it != m_def.categories.end()) return it->second;
    return "0";
}

// Turns "1.5 GB", "700MB", "1,2 GiB" or "1024" into a byte count.
static int64_t parseSizeText(const std::string& text) {
    // e.g. "1.5 GB", "700MB", "1,2 GiB", "1024". Some pages separate the
    // number from the unit with &nbsp; instead of a plain space.
    std::regex re(R"(([0-9]+(?:[.,][0-9]+)?)(?:\s|&nbsp;)*([KMGTP]?)(i?B)?)", std::regex::icase);
    std::smatch m;
    if (!std::regex_search(text, m, re)) return 0;
    
    std::string num = m[1].str();
    for (auto& c : num) if (c == ',') c = '.';
    double value = std::strtod(num.c_str(), nullptr);
    
    std::string unit = m[2].str();
    for (auto& c : unit) c = static_cast<char>(std::toupper((unsigned char)c));
    
    int64_t mult = 1;
    if (unit == "K") mult = 1024LL;
    else if (unit == "M") mult = 1024LL * 1024;
    else if (unit == "G") mult = 1024LL * 1024 * 1024;
    else if (unit == "T") mult = 1024LL * 1024 * 1024 * 1024;
    else if (unit == "P") mult = 1024LL * 1024 * 1024 * 1024 * 1024;
    
    return static_cast<int64_t>(value * mult);
}

// Reads a peer count, keeping every digit of a grouped number ("1,234" -> 1234).
static int parseCountText(const std::string& text) {
    // Sites write big numbers with separators ("1,234"), and taking the leading
    // digits of that would give 1 instead of 1234. So grab the whole numeric
    // run and keep only the digits. The trailing group is optional so that a
    // single digit ("0 seeders") still counts.
    static const std::regex re(R"([0-9](?:[0-9.,' ]*[0-9])?)", std::regex::ECMAScript);
    std::smatch m;
    if (!std::regex_search(text, m, re)) return -1;
    
    std::string digits;
    for (char c : m[0].str()) {
        if (std::isdigit((unsigned char)c)) digits += c;
    }
    if (digits.empty()) return -1;
    return static_cast<int>(std::strtol(digits.c_str(), nullptr, 10));
}

int64_t SearchEngine::parseSize(const std::string& text) {
    return parseSizeText(text);
}

int SearchEngine::parseCount(const std::string& text) {
    return parseCountText(text);
}

// Sites escape "&" as "&amp;" in their markup, and btdig wraps the matched
// words of a title in <b> tags. Both have to be undone or the magnet handed to
// libtorrent is malformed and the title shows raw HTML.
static void normalizeResult(SearchResult& r) {
    // Magnet links arrive HTML-escaped from several sites (&#x3D; for "=",
    // &amp; for "&"); undo that first or the link handed to libtorrent is
    // unusable even though it looks right in a browser.
    r.magnet = decodeHtmlEntities(r.magnet);
    if (!r.magnet.empty() && r.magnet.rfind("magnet:", 0) != 0) {
        r.magnet = "magnet:" + r.magnet;
    }
    for (const char* ent : {"&amp;", "&quot;", "&#39;", "&lt;", "&gt;"}) {
        size_t pos;
        while ((pos = r.magnet.find(ent)) != std::string::npos) {
            r.magnet.replace(pos, std::string(ent).size(), 
                             ent[1] == 'a' ? "&" : "");
        }
    }
    if (r.magnet.rfind("magnet:", 0) != 0) r.magnet.clear();
    
    // Quitar el HTML del titulo y descomprimir las entidades basicas
    std::string clean;
    bool inTag = false;
    for (char c : r.name) {
        if (c == '<') { inTag = true; continue; }
        if (c == '>') { inTag = false; continue; }
        if (!inTag) clean += c;
    }
    size_t pos;
    while ((pos = clean.find("&amp;")) != std::string::npos) clean.replace(pos, 5, "&");
    while ((pos = clean.find("&quot;")) != std::string::npos) clean.replace(pos, 6, "\"");
    while ((pos = clean.find("&#39;")) != std::string::npos) clean.replace(pos, 5, "'");
    while ((pos = clean.find("&lt;")) != std::string::npos) clean.replace(pos, 4, "<");
    while ((pos = clean.find("&gt;")) != std::string::npos) clean.replace(pos, 4, ">");
    
    // Colapsar espacios y quitar los que quedaron en los bordes
    std::string tidy;
    bool prevSpace = false;
    for (char c : clean) {
        bool isSpace = (c == ' ' || c == '\t' || c == '\n' || c == '\r');
        if (isSpace) {
            if (!prevSpace && !tidy.empty()) tidy += ' ';
        } else {
            tidy += c;
        }
        prevSpace = isSpace;
    }
    while (!tidy.empty() && tidy.back() == ' ') tidy.pop_back();
    r.name = tidy;
}

bool SearchEngine::applySelector(const std::string& chunk, const std::string& selector,
                                 std::string& out) const {
    if (selector.empty()) return false;
    
    // A plugin comes from a file or a URL, so its patterns cannot be trusted:
    // an invalid expression must produce "no match", never an exception that
    // would unwind out of the worker thread.
    try {
        std::regex re(selector, std::regex::ECMAScript);
        std::smatch m;
        if (!std::regex_search(chunk, m, re)) return false;
        if (m.size() < 2) return false;
        out = m[1].str();
        return true;
    } catch (const std::regex_error&) {
        return false;
    }
}

// Generic fallback for sites whose markup we do not know: collect every
// magnet link on the page and derive the rest from its surroundings. The
// magnet itself carries the info-hash and, most of the time, the display name
// (dn=), so a redesigned page still yields usable results instead of nothing.
static bool harvestMagnets(const std::string& body, std::vector<SearchResult>& out) {
    // The link is looked for with a permissive pattern and validated afterwards.
    // Sites disagree on how much of a magnet they escape: some write
    // "magnet:?xt=urn:btih:...", others "magnet:?xt&#x3D;urn:btih:...", so a
    // single strict pattern misses half of them.
    static const std::regex magnetRe(R"(magnet:[^"'<>\s]*)", std::regex::ECMAScript);
    static const std::regex strictRe(
        R"(^magnet:\?xt=urn:btih:([0-9a-fA-F]{40}|[0-9a-fA-F]{32}))",
        std::regex::ECMAScript);
    
    std::set<std::string> seen;
    auto begin = std::sregex_iterator(body.begin(), body.end(), magnetRe);
    auto end = std::sregex_iterator();
    
    // End of the previous match, carried along because a regex iterator is only
    // a forward iterator.
    size_t prevMatchEnd = 0;
    
    for (auto it = begin; it != end; ++it) {
        if (out.size() >= 200) break;
        
        // Undo the HTML escaping before the link can be understood.
        std::string link = decodeHtmlEntities(it->str());
        
        std::smatch sm;
        if (!std::regex_search(link, sm, strictRe)) continue;
        
        std::string hash = sm[1].str();
        std::transform(hash.begin(), hash.end(), hash.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (!seen.insert(hash).second) continue;   // same file listed twice
        
        SearchResult r;
        r.source = "";
        r.magnet = "magnet:?xt=urn:btih:" + hash;
        
        // The name is normally in the magnet itself as dn=, already URL-encoded.
        size_t dn = link.find("&dn=");
        if (dn != std::string::npos) {
            size_t endName = link.find('&', dn + 4);
            if (endName == std::string::npos) endName = link.size();
            r.name = urlDecode(link.substr(dn + 4, endName - (dn + 4)));
        }
        
        // Context: only the text around this magnet, bounded by the previous
        // and next match. A fixed window would pick up the size and peer counts
        // belonging to the neighbouring result.
        size_t pos = static_cast<size_t>(it->position(0));
        auto next = std::next(it);
        size_t prevEnd = prevMatchEnd;
        size_t nextPos = (next == end) ? body.size() : static_cast<size_t>(next->position(0));
        if (pos > prevEnd + 900) prevEnd = pos > 900 ? pos - 900 : 0;
        if (nextPos > pos + it->length(0) + 900) nextPos = pos + it->length(0) + 900;
        std::string ctx = body.substr(prevEnd, nextPos - prevEnd);
        
        if (r.name.empty()) {
            // Fall back to the visible text of the nearest link or heading
            static const std::regex textRe(R"(<a[^>]*>([^<]{4,160})</a>)", std::regex::ECMAScript);
            std::smatch tm;
            if (std::regex_search(ctx, tm, textRe)) {
                r.name = urlDecode(decodeHtmlEntities(tm[1].str()));
            }
        }
        if (r.name.empty()) r.name = hash;
        
        static const std::regex sizeRe(R"(([0-9][0-9.,]*\s?[KMGTP]i?B))", std::regex::icase);
        std::smatch sized;
        if (std::regex_search(ctx, sized, sizeRe)) r.size = parseSizeText(sized[1].str());
        
        static const std::regex seedRe(R"(([0-9][0-9.,]*)\s*(?:seeders?|seeds|seeding|completos|sembradores))",
                                       std::regex::icase);
        if (std::regex_search(ctx, sized, seedRe)) r.seeders = parseCountText(sized[1].str());
        
        static const std::regex leecherRe(R"(([0-9][0-9.,]*)\s*(?:leechers?|peers|descargas|incompletos))",
                                          std::regex::icase);
        if (std::regex_search(ctx, sized, leecherRe)) r.leechers = parseCountText(sized[1].str());
        
        prevMatchEnd = pos + it->length(0);
        out.push_back(r);
    }
    return !out.empty();
}

bool SearchEngine::parse(const std::string& body, std::vector<SearchResult>& out) const {
    if (body.empty()) return false;
    
    // Plain-text endpoints: one result per line. No engine in the catalogue
    // uses this today, but the parser stays because it needs no HTML at all
    // and is the cheapest way to bring a new text endpoint back.
    if (m_def.type == "lines") {
        if (m_def.linePattern.empty()) return false;
        try {
            std::regex lineRe(m_def.linePattern,
                              std::regex::ECMAScript | std::regex::icase);
            std::istringstream stream(body);
            std::string text;
            while (std::getline(stream, text)) {
                if (!text.empty() && text.back() == '\r') text.pop_back();
                if (text.empty()) continue;
                std::smatch m;
                if (!std::regex_match(text, m, lineRe)) continue;
                if (m.size() < 5) continue;
                
                SearchResult r;
                r.source = m_def.name;
                r.name = m[1].str();
                r.magnet = m[4].str();
                r.size = parseSizeText(m[2].str());
                r.seeders = parseCountText(m[3].str());
                
                normalizeResult(r);
                if (r.magnet.empty() || r.name.empty()) continue;
                out.push_back(r);
                if (out.size() >= 200) break;
            }
        } catch (const std::regex_error&) {
            return false;
        }
        return !out.empty();
    }
    
    // No site-specific rules: harvest whatever magnets the page exposes.
    if (m_def.rowSelector.empty() && m_def.magnetSelector.empty()) {
        bool found = harvestMagnets(body, out);
        // harvestMagnets() is a free function and cannot know which site it is
        // running on, so it leaves the source empty. Every engine that relies
        // on it (Nyaa.si, BTDig) therefore showed a blank Source column while
        // the ones with explicit selectors showed their name. Filled in here,
        // where the engine is known, so a row can always be traced back to the
        // engine that produced it.
        if (found) {
            for (auto& r : out) r.source = m_def.name;
        }
        return found;
    }
    
    if (m_def.type == "json") {
        // Split into repeated objects on the row selector's key, or on "name"
        std::string marker = m_def.rowSelector.empty() ? "name" : m_def.rowSelector;
        size_t start = 0;
        int count = 0;
        while (true) {
            size_t p = body.find(marker, start);
            if (p == std::string::npos) break;
            // Walk back to the '{' that opens this object
            size_t objStart = p;
            while (objStart > 0 && body[objStart] != '{') --objStart;
            size_t objEnd = body.find('}', p);
            if (objEnd == std::string::npos) break;
            std::string obj = body.substr(objStart, objEnd - objStart + 1);
            
            SearchResult r;
            r.source = m_def.name;
            std::string v;
            std::string rawLink;
            if (jsonField(obj, m_def.titleSelector, v)) r.name = v;
            if (jsonField(obj, m_def.magnetSelector, v)) rawLink = v;
            if (!m_def.detailIdSelector.empty() &&
                jsonField(obj, m_def.detailIdSelector, v)) {
                r.detailId = v;
            }
            if (jsonField(obj, m_def.sizeSelector, v)) r.size = parseSize(v);
            if (jsonField(obj, m_def.seedersSelector, v)) r.seeders = parseCount(v);
            if (jsonField(obj, m_def.leechersSelector, v)) r.leechers = parseCount(v);
            
            r.magnet = rawLink;
            // Some APIs answer with a bare info hash instead of a full magnet
            // link, so the link is put together from it.
            if (isInfoHash(rawLink)) {
                r.magnet = "magnet:?xt=urn:btih:" + rawLink;
                if (!r.name.empty()) {
                    r.magnet += "&dn=" + urlEncode(r.name);
                }
            }
            
            normalizeResult(r);
            // A row is worth keeping when it already carries a magnet, or when
            // this engine says the magnet can be asked for separately.
            const bool hasMagnet = !r.magnet.empty() && !r.name.empty();
            const bool needsDetail = !m_def.detailUrlTemplate.empty() &&
                                     !r.detailId.empty() && !r.name.empty();
            if (hasMagnet || needsDetail) {
                out.push_back(r);
                if (++count > 200) break;
            }
            start = objEnd + 1;
        }
        if (!out.empty()) return true;
    }
    
    // HTML: iterate over the rows found by the row selector
    if (m_def.rowSelector.empty()) return false;
    try {
        std::regex rowRe(m_def.rowSelector, std::regex::ECMAScript);
        auto begin = std::sregex_iterator(body.begin(), body.end(), rowRe);
        auto end = std::sregex_iterator();
        for (auto it = begin; it != end; ++it) {
            std::string chunk = it->str();
            SearchResult r;
            r.source = m_def.name;
            std::string v;
            if (applySelector(chunk, m_def.titleSelector, v)) r.name = v;
            if (applySelector(chunk, m_def.magnetSelector, v)) r.magnet = v;
            if (applySelector(chunk, m_def.sizeSelector, v)) r.size = parseSize(v);
            if (applySelector(chunk, m_def.seedersSelector, v)) r.seeders = parseCount(v);
            if (applySelector(chunk, m_def.leechersSelector, v)) r.leechers = parseCount(v);
            
            normalizeResult(r);
            if (!r.magnet.empty() && !r.name.empty()) {
                out.push_back(r);
                if (out.size() > 200) break;
            }
        }
    } catch (const std::regex_error&) {
        return false;   // A malformed plugin file should not crash the app
    }
    return !out.empty();
}

// Fetches through the direct address, and retries through the proxy/mirror
// when the direct one is blocked or returns nothing usable. That retry is what
// keeps a site working after it rotates its domain, without any code change.
static bool fetchWithFallback(const std::string& url,
                              const std::string& proxyUrl,
                              HttpClient::Response& resp,
                              std::string& error,
                              std::string& usedVia,
                              const std::function<bool(const std::string&)>& usable,
                              const SearchEngine::StopCheck& shouldStop) {
    std::string ua = "FTORRENT " FTP_VERSION;
    
    // A site often answers with a challenge page or an empty listing instead of
    // refusing outright, so a body is only accepted once the parser finds
    // something in it. Otherwise the mirror below gets its turn.
    if (!url.empty()) {
        resp = HttpClient::get(url, ua, 15, shouldStop);
        if (resp.ok && usable(resp.body)) {
            usedVia = "direct";
            return true;
        }
        error = resp.ok ? std::string("The site returned no usable results") : resp.error;
    }
    
    // Cancelling must not silently turn into a second request to the mirror.
    if (shouldStop && shouldStop()) {
        error = "Cancelled";
        return false;
    }
    
    if (!proxyUrl.empty()) {
        HttpClient::Response viaProxy = HttpClient::get(proxyUrl, ua, 20, shouldStop);
        if (viaProxy.ok && usable(viaProxy.body)) {
            resp = viaProxy;
            usedVia = "proxy";
            error.clear();
            return true;
        }
        if (viaProxy.ok) {
            error = "The site and its mirror returned no usable results";
        } else if (error.empty()) {
            error = viaProxy.error;
        }
    }
    
    if (error.empty()) error = "Could not reach the site";
    return false;
}

bool SearchEngine::fetchPage(const std::string& query,
                              const std::string& category,
                              int page,
                              const StopCheck& shouldStop,
                              std::vector<SearchResult>& out,
                              std::string& error,
                              PageInfo* info) const {
    std::string url = buildUrl(query, category, page);
    std::string proxyUrl = buildProxyUrl(query, category, page);
    
    if (!HttpClient::isSecure(url) && proxyUrl.empty()) {
        error = "The site does not use https";
        return false;
    }
    
    HttpClient::Response resp;
    std::string usedVia;
    if (!fetchWithFallback(url, proxyUrl, resp, error, usedVia,
                           [this](const std::string& body) {
                               std::vector<SearchResult> probe;
                               return parse(body, probe);
                           },
                           shouldStop)) {
        if (info) {
            info->httpStatus = resp.httpStatus;
            info->bodyBytes = resp.body.size();
            info->via = usedVia;
        }
        return false;
    }
    
    if (shouldStop && shouldStop()) {
        error = "Cancelled";
        return false;
    }
    if (info) {
        info->httpStatus = resp.httpStatus;
        info->bodyBytes = resp.body.size();
        info->via = usedVia;
    }
    if (!parse(resp.body, out)) return false;
    // Only afterwards, so a listing without links still counts as a hit.
    resolveDetails(out, shouldStop);
    return true;
}

// Some catalogues answer with a list whose entries carry everything but the
// magnet, and hand the link out one entry at a time. When an engine says so,
// the magnet is fetched here instead of during parse(), which stays a pure
// text-to-rows function.
void SearchEngine::resolveDetails(std::vector<SearchResult>& rows,
                                  const StopCheck& shouldStop) const {
    if (m_def.detailUrlTemplate.empty()) return;

    // Each entry costs one request, and a listing can hold hundreds of them,
    // so the walk is capped. HttpClient::get() opens its own curl handle per
    // call, so the requests are independent and a small pool is safe.
    const size_t kMaxRequests = 120;
    const size_t kMaxThreads = 6;
    const std::string ua = std::string("FTORRENT ") + FTP_VERSION;

    std::vector<size_t> pending;
    for (size_t i = 0; i < rows.size() && pending.size() < kMaxRequests; ++i) {
        if (rows[i].magnet.empty() && !rows[i].detailId.empty()) {
            pending.push_back(i);
        }
    }
    if (pending.empty()) return;

    std::atomic<size_t> next{0};
    std::mutex writeLock;
    auto worker = [&]() {
        while (true) {
            if (shouldStop && shouldStop()) return;
            size_t n = next.fetch_add(1);
            if (n >= pending.size()) return;
            SearchResult& r = rows[pending[n]];

            std::string url = m_def.detailUrlTemplate;
            size_t at;
            while ((at = url.find("%i")) != std::string::npos) {
                url.replace(at, 2, urlEncode(r.detailId));
            }
            HttpClient::Response res = HttpClient::get(url, ua, 10, shouldStop);
            std::string magnet;
            if (res.ok && jsonField(res.body, "magnet", magnet) && !magnet.empty()) {
                std::lock_guard<std::mutex> lock(writeLock);
                r.magnet = magnet;
                normalizeResult(r);
            }
        }
    };

    size_t threads = std::min(kMaxThreads, pending.size());
    std::vector<std::thread> pool;
    pool.reserve(threads);
    for (size_t t = 0; t < threads; ++t) pool.emplace_back(worker);
    for (std::thread& t : pool) t.join();

    // An entry with no torrent behind it has nothing to offer here, and a row
    // without a magnet cannot be downloaded from the results table.
    rows.erase(std::remove_if(rows.begin(), rows.end(),
                              [](const SearchResult& r) { return r.magnet.empty(); }),
               rows.end());
}

// Short human form of a fetch ("HTTP 200, 22 KB via direct") for error lines.
static std::string describeFetch(const SearchEngine::PageInfo& info) {
    std::string text;
    if (info.httpStatus > 0) {
        text += "HTTP " + std::to_string(info.httpStatus);
    } else {
        text += "no answer";
    }
    char buf[64];
    if (info.bodyBytes < 1024) {
        snprintf(buf, sizeof(buf), "%llu B", (unsigned long long)info.bodyBytes);
    } else {
        snprintf(buf, sizeof(buf), "%.0f KB", info.bodyBytes / 1024.0);
    }
    text += ", ";
    text += buf;
    if (!info.via.empty()) {
        text += " via ";
        text += info.via;
    }
    return text;
}

bool SearchEngine::search(const std::string& query,
                          std::vector<SearchResult>& out,
                          std::string& error,
                          const StopCheck& shouldStop,
                          const std::string& category) const {
    if (query.empty()) {
        error = "Empty query";
        return false;
    }
    
    std::string url = buildUrl(query, category, 1);
    std::string proxyUrl = buildProxyUrl(query, category, 1);
    
    if (!HttpClient::isSecure(url) && proxyUrl.empty()) {
        error = "The site does not use https";
        return false;
    }
    
    // Walk result pages 1..N. A page that yields nothing ends the walk: on
    // page 1 that is a failure, later it just means there is no more.
    // Templates without %p always build the same address, so only page 1 is
    // fetched for them instead of downloading duplicates.
    bool paged = m_def.urlTemplate.find("%p") != std::string::npos ||
                 m_def.proxyTemplate.find("%p") != std::string::npos;
    int pages = m_def.maxPages > 0 ? m_def.maxPages : 1;
    for (int page = 1; page <= pages; ++page) {
        if (shouldStop && shouldStop()) {
            error = "Cancelled";
            return false;
        }
        std::vector<SearchResult> found;
        PageInfo info;
        if (!fetchPage(query, category, page, shouldStop, found, error, &info)) {
            if (page == 1) return false;
            break;
        }
        if (found.empty()) {
            if (page == 1) {
                error = "No results found on " + m_def.name +
                        " (" + describeFetch(info) + ", parsed 0)";
                return false;
            }
            break;
        }
        out.insert(out.end(), found.begin(), found.end());
        if (out.size() >= 200 || !paged) break;
    }
    return !out.empty();
}

bool SearchEngine::searchStreaming(const std::string& query,
                                   const BatchCallback& onBatch,
                                   const StopCheck& shouldStop,
                                   std::string& error,
                                   const std::string& category,
                                   const StageCallback& onStage) const {
    if (query.empty()) {
        error = "Empty query";
        return false;
    }
    
    std::string url = buildUrl(query, category, 1);
    std::string proxyUrl = buildProxyUrl(query, category, 1);
    
    if (!HttpClient::isSecure(url) && proxyUrl.empty()) {
        error = "The site does not use https";
        return false;
    }
    
    // Same walk as search(), but every page is emitted as it arrives so the
    // table fills progressively instead of waiting for the last page.
    bool paged = m_def.urlTemplate.find("%p") != std::string::npos ||
                 m_def.proxyTemplate.find("%p") != std::string::npos;
    int pages = m_def.maxPages > 0 ? m_def.maxPages : 1;
    const size_t BATCH = 15;
    size_t total = 0;
    for (int page = 1; page <= pages; ++page) {
        if (shouldStop && shouldStop()) {
            error = "Cancelled";
            return false;
        }
        if (onStage) {
            onStage("Contacting " + m_def.name +
                    (pages > 1 ? " (page " + std::to_string(page) +
                                 "/" + std::to_string(pages) + ")..." : "..."));
        }
        std::vector<SearchResult> found;
        PageInfo info;
        if (!fetchPage(query, category, page, shouldStop, found, error, &info)) {
            if (page == 1) return false;
            break;
        }
        if (found.empty()) {
            if (page == 1) {
                error = "No results found on " + m_def.name +
                        " (" + describeFetch(info) + ", parsed 0)";
                return false;
            }
            break;
        }
        for (size_t i = 0; i < found.size() && total < 200; i += BATCH) {
            if (shouldStop && shouldStop()) {
                error = "Cancelled";
                return false;
            }
            size_t end = std::min(i + BATCH, found.size());
            end = std::min(end, i + (200 - total));
            std::vector<SearchResult> chunk(found.begin() + i, found.begin() + end);
            total += chunk.size();
            if (onBatch) onBatch(chunk);
        }
        if (total >= 200 || !paged) break;
    }
    if (total == 0) {
        error = "No results found on " + m_def.name;
        return false;
    }
    return true;
}

SearchEngine::Definition SearchEngine::parseDefinition(const std::string& iniText) {
    Definition def;
    std::istringstream ss(iniText);
    std::string line;
    while (std::getline(ss, line)) {
        line = trim(line);
        if (line.empty() || line[0] == '#') continue;
        size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string key = trim(line.substr(0, eq));
        std::string val = trim(line.substr(eq + 1));
        
        if (key == "nombre") def.name = val;
        else if (key == "url") def.urlTemplate = val;
        else if (key == "proxy") def.proxyTemplate = val;
        else if (key == "tipo") def.type = val;
        else if (key == "fila") def.rowSelector = val;
        else if (key == "magnet") def.magnetSelector = val;
        else if (key == "titulo") def.titleSelector = val;
        else if (key == "tamano") def.sizeSelector = val;
        else if (key == "semillas") def.seedersSelector = val;
        else if (key == "descargas") def.leechersSelector = val;
        else if (key == "linea") def.linePattern = val;
        else if (key == "categorias") {
            // "All=0;Movies=201": entries split on ';', name from code on '='.
            std::istringstream cats(val);
            std::string entry;
            while (std::getline(cats, entry, ';')) {
                size_t split = entry.find('=');
                if (split == std::string::npos) continue;
                std::string name = trim(entry.substr(0, split));
                std::string code = trim(entry.substr(split + 1));
                if (!name.empty() && !code.empty()) def.categories[name] = code;
            }
        }
        else if (key == "paginas") def.maxPages = std::atoi(val.c_str());
        else if (key == "activo") def.enabled = (val != "0" && val != "false");
    }
    return def;
}

std::string SearchEngine::serializeDefinition(const Definition& def) {
    std::ostringstream ss;
    ss << "nombre=" << def.name << "\n";
    ss << "url=" << def.urlTemplate << "\n";
    if (!def.proxyTemplate.empty()) ss << "proxy=" << def.proxyTemplate << "\n";
    ss << "tipo=" << def.type << "\n";
    if (!def.rowSelector.empty())      ss << "fila=" << def.rowSelector << "\n";
    if (!def.magnetSelector.empty())   ss << "magnet=" << def.magnetSelector << "\n";
    if (!def.titleSelector.empty())    ss << "titulo=" << def.titleSelector << "\n";
    if (!def.sizeSelector.empty())     ss << "tamano=" << def.sizeSelector << "\n";
    if (!def.seedersSelector.empty())  ss << "semillas=" << def.seedersSelector << "\n";
    if (!def.leechersSelector.empty()) ss << "descargas=" << def.leechersSelector << "\n";
    if (!def.linePattern.empty())      ss << "linea=" << def.linePattern << "\n";
    if (!def.categories.empty()) {
        ss << "categorias=";
        bool first = true;
        for (const auto& kv : def.categories) {
            if (!first) ss << ";";
            first = false;
            ss << kv.first << "=" << kv.second;
        }
        ss << "\n";
    }
    if (def.maxPages > 1) ss << "paginas=" << def.maxPages << "\n";
    ss << "activo=" << (def.enabled ? "1" : "0") << "\n";
    return ss.str();
}

std::vector<SearchEngine::Definition> SearchEngine::builtinEngines() {
    std::vector<Definition> engines;
    
    // Only sites that answer a plain HTTP request are enabled by default: a
    // search box that silently returns nothing is worse than a shorter list.
    // Each of these was checked to return magnets to a non-browser client.
    
    // SolidTorrents and BitSearch share a backend and its markup: every
    // result card carries the title link, a size cell and seeder/leecher
    // cells, so explicit selectors give exact numbers where the generic
    // harvest only guessed. Paginator is ?q=...&page=N (20 per page).
    // The .to domains now answer 301 to bitsearch.eu, so the addresses below
    // point at the two hosts that still serve the listing directly and use
    // each other as the fallback mirror.
    struct SolidDef { const char* name; const char* url; const char* proxy; };
    static const SolidDef solidSites[] = {
        { "SolidTorrents", "https://solidtorrents.eu", "https://bitsearch.eu" },
        { "BitSearch",     "https://bitsearch.eu",     "https://solidtorrents.eu" },
    };
    
    for (const SolidDef& site : solidSites) {
        Definition d;
        d.name = site.name;
        d.type = "html";
        d.urlTemplate = std::string(site.url) + "/search?q=%s&page=%p";
        d.proxyTemplate = std::string(site.proxy) + "/search?q=%s&page=%p";
        // One card per result; the tempered[h3] keeps a magnet-less card from
        // swallowing the next card's magnet.
        d.rowSelector = "<h3(?:(?!<h3)[\\s\\S])*?magnet:[^\"]+\"";
        d.magnetSelector = "(magnet:[^\"]+)";
        d.titleSelector = "<a href=\"/torrent/[^\"]*\"[^>]*>\\s*([^<]+?)\\s*</a>";
        d.sizeSelector = "<span>([0-9][0-9.,]*\\s*[KMGT]i?B)</span>";
        d.seedersSelector = "\">([0-9][0-9.,]*)</span>\\s*<span>seeders";
        d.leechersSelector = "\">([0-9][0-9.,]*)</span>\\s*<span>leechers";
        d.maxPages = 5;
        d.enabled = true;
        engines.push_back(d);
    }
    
    // Nyaa.si: categories and pages travel as query parameters. No row
    // selectors: the generic harvest reads names straight out of the magnets'
    // dn= fields, which is exactly what the reference plugin does by hand.
    {
        Definition d;
        d.name = "Nyaa.si";
        d.type = "html";
        d.urlTemplate = "https://nyaa.si/?f=0&c=%c&q=%s&p=%p";
        d.categories["All"] = "0_0";
        d.categories["Anime"] = "1_0";
        d.categories["Books"] = "3_0";
        d.categories["Music"] = "2_0";
        d.categories["Pictures"] = "5_0";
        d.categories["Software"] = "6_0";
        d.categories["TV"] = "4_0";
        d.categories["Movies"] = "4_0";
        d.maxPages = 3;
        d.enabled = true;
        engines.push_back(d);
    }
    
    // The Pirate Bay through a proxy serving its classic markup. Rows are
    // fully structured (<tr> with detLink title, magnet href, Size cell and
    // seeder/leecher cells), so explicit selectors beat the generic harvest
    // here: the 1 KB magnet links would otherwise push the size and peer
    // counts out of the harvester's context window.
    {
        Definition d;
        d.name = "The Pirate Bay";
        d.type = "html";
        d.urlTemplate = "https://tpbpxy.site/search/%s/%p/99/%c/";
        d.rowSelector = "<tr[^>]*>[\\s\\S]*?</tr>";
        d.magnetSelector = "href=\"(magnet:[^\"]+)\"";
        d.titleSelector = "title=\"Details for ([^\"]+)\"";
        d.sizeSelector = ",\\s*Size\\s+([0-9][0-9.,]*(?:\\s|&nbsp;)+[KMGT]i?B)";
        d.seedersSelector = "<td align=\"right\">([0-9]+)</td>";
        d.leechersSelector = "<td align=\"right\">[0-9]+</td>\\s*<td align=\"right\">([0-9]+)</td>";
        d.categories["All"] = "0";
        d.categories["Music"] = "101";
        d.categories["Movies"] = "201";
        d.categories["TV shows"] = "205";
        d.categories["HD Movies"] = "207";
        d.categories["HD TV shows"] = "208";
        d.categories["Windows"] = "301";
        d.categories["PC games"] = "401";
        d.categories["E-books"] = "601";
        d.maxPages = 3;
        d.enabled = true;
        engines.push_back(d);
    }
    
    // TorrentGalaxy was dropped from the catalogue: /torrents.php?search= now
    // answers 302 to the home page, and even the real listing stopped carrying
    // magnet links (they only appear on the per-torrent page), so there is
    // nothing left to parse without fetching every entry.
    //
    // BTDig replaces it. It is a DHT index that answers a plain GET with the
    // whole listing, magnets included, so the generic harvest is enough here:
    // no row selectors to rot, names taken from the magnets' own dn= field.
    // Paginator is ?q=...&p=N, but BTDig is noticeably flaky under repeated
    // requests (roughly one search in three stalls), and page 1 already
    // carries a full listing: asking for more only multiplies the chances of
    // waiting on a timeout.
    {
        Definition d;
        d.name = "BTDig";
        d.type = "html";
        d.urlTemplate = "https://btdig.com/search?q=%s&p=%p";
        d.maxPages = 1;
        d.enabled = true;
        engines.push_back(d);
    }
    
    // Apibay: the Pirate Bay's own JSON API, kept because it needs no scraping
    // at all. Flat objects, so the small dotted-path extractor is enough, and
    // it answers a bare info_hash which the parser turns into a full magnet.
    {
        Definition d;
        d.name = "The Pirate Bay (API)";
        d.type = "json";
        d.urlTemplate = "https://apibay.org/q.php?q=%s";
        // Marker used to split the array; quoting it keeps a value that
        // happens to contain the word "name" from splitting an object in two.
        d.rowSelector = "\"name\":\"";
        d.titleSelector = "name";
        d.magnetSelector = "info_hash";
        d.sizeSelector = "size";
        d.seedersSelector = "seeders";
        d.leechersSelector = "leechers";
        d.maxPages = 1;
        d.enabled = true;
        engines.push_back(d);
    }
    
    // Pirate Face (pirateface.co) is the AI-model repository: the thing people
    // come here for is a model, and every mirrored repo also ships as a
    // torrent. The site is a Next.js app and its /s/<query> page answers
    // "No torrents match ... yet." for everything, but the catalogue that
    // page itself calls is plain JSON, and that is all this engine needs:
    //   GET /api/models?q=<query>[&task=<task>]
    //       -> {"models":[{"id","name","sizeBytes","seed","leech",...}],
    //           "nextCursor":"saved:eyJ..."}
    // Each entry carries name, size and peers but no magnet, which the same
    // service hands out one repository at a time:
    //   GET /api/torrents?repo=<author%2Fmodel>
    //       -> {"magnet":"magnet:?xt=urn:btih:...", "seeders":N, ...}
    //       -> {"error":"not listed"} when the repo is not mirrored
    // That second call is what detailIdSelector/detailUrlTemplate describe,
    // and it runs off the UI thread with the cancel check alive. Roughly one
    // model in five is mirrored, so most follow-ups answer "not listed" and
    // their row is dropped instead of being shown without a magnet.
    //
    // Only the first page is walked. Pagination is cursor-only: nextCursor is
    // a base64 blob of the site's internal query state, and page=, offset=,
    // skip= and limit= are all ignored server-side, so %p cannot address a
    // second page without forging the cursor.
    {
        Definition d;
        d.name = "Pirate Face";
        d.type = "json";
        d.urlTemplate = "https://pirateface.co/api/models?q=%s&task=%c";
        d.rowSelector = "\"id\":\"";
        d.titleSelector = "name";
        d.detailIdSelector = "id";
        d.detailUrlTemplate = "https://pirateface.co/api/torrents?repo=%i";
        d.sizeSelector = "sizeBytes";
        d.seedersSelector = "seed";
        d.leechersSelector = "leech";
        d.maxPages = 1;
        d.enabled = true;
        d.categories = {
            {"All", ""},
            {"Text generation", "text-generation"},
            // No "/" in a label: Fl_Choice reads it as a submenu separator.
            {"Image and text", "image-text-to-text"},
            {"Speech recognition", "automatic-speech-recognition"},
            {"Text to image", "text-to-image"},
            {"Sentence similarity", "sentence-similarity"},
            {"Feature extraction", "feature-extraction"},
        };
        engines.push_back(d);
    }
    
    return engines;
}
