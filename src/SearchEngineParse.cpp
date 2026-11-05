#include "SearchEngine.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <regex>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace {

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

// Scheme and host of an engine address, so a listing that publishes
// "/download/123.torrent" can be turned into a link the app can open. Empty
// when the template is not an absolute URL.
std::string originOf(const std::string& urlTemplate) {
    const size_t scheme = urlTemplate.find("://");
    if (scheme == std::string::npos) return std::string();
    const size_t hostStart = scheme + 3;
    size_t end = urlTemplate.find_first_of("/?#", hostStart);
    if (end == std::string::npos) end = urlTemplate.size();
    return urlTemplate.substr(0, end);
}

// Resolves a link found in a listing. Only absolute links and root-relative
// ones are accepted: a bare relative path is as likely to be a redirect as a
// file, and guessing would point the fetch at the wrong place.
std::string absoluteUrl(const std::string& origin, const std::string& link) {
    if (link.empty()) return std::string();
    if (link.rfind("https://", 0) == 0 || link.rfind("http://", 0) == 0) return link;
    if (!origin.empty() && link[0] == '/') return origin + link;
    return std::string();
}

// Applies one selector to a chunk of text and returns group 1. A plugin comes
// from a file or a URL, so its patterns cannot be trusted: an invalid
// expression must produce "no match", never an exception that would unwind out
// of a worker thread.
bool matchSelector(const std::string& chunk, const std::string& selector,
                   std::string& out) {
    if (selector.empty()) return false;
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

// Turns "1.5 GB", "700MB", "1,2 GiB" or "1024" into a byte count.
int64_t parseSizeText(const std::string& text) {
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
int parseCountText(const std::string& text) {
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

// Generic fallback for sites whose markup we do not know: collect every
// magnet link on the page and derive the rest from its surroundings. The
// magnet itself carries the info-hash, the display name (dn=) and, most of the
// time, the trackers (tr=), so it is kept exactly as published: rebuilding it
// from the hash alone dropped the trackers and made a live swarm look dead.
bool harvestMagnets(const std::string& body, std::vector<SearchResult>& out,
                           const std::string& torrentSelector,
                           const std::string& origin) {
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
        // The whole link, not a rebuilt one: the tr= entries are what tell the
        // probe where the peers are.
        r.magnet = link;
        
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

        // The .torrent button belongs to this row when it sits between the
        // previous magnet and this one: every engine prints it beside the
        // magnet it belongs to. Looking only at that stretch keeps a row from
        // borrowing its neighbour's link.
        if (!torrentSelector.empty() && pos > prevEnd) {
            std::string href;
            if (matchSelector(ctx.substr(0, pos - prevEnd), torrentSelector, href)) {
                r.torrentUrl = absoluteUrl(origin, href);
            }
        }
        
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

} // namespace

// Very small helper: minimal JSON string/number extraction by dotted path.
// Deliberately not a full JSON parser: the engines only need flat fields.
bool SearchEngine::jsonField(const std::string& json, const std::string& path, std::string& out) {
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

int64_t SearchEngine::parseSize(const std::string& text) {
    return parseSizeText(text);
}

int SearchEngine::parseCount(const std::string& text) {
    return parseCountText(text);
}

// Sites escape "&" as "&amp;" in their markup, and btdig wraps the matched
// words of a title in <b> tags. Both have to be undone or the magnet handed to
// libtorrent is malformed and the title shows raw HTML.
void SearchEngine::normalizeResult(SearchResult& r) {
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

    // The .torrent link travels through the same markup escaping as the magnet.
    // Left as "&amp;" it would still look right in a browser and 404 when the
    // probe asked for it.
    if (!r.torrentUrl.empty()) {
        r.torrentUrl = decodeHtmlEntities(r.torrentUrl);
        while (!r.torrentUrl.empty() &&
               (r.torrentUrl.back() == '"' || r.torrentUrl.back() == '\'' ||
                std::isspace(static_cast<unsigned char>(r.torrentUrl.back())))) {
            r.torrentUrl.pop_back();
        }
    }
    
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
    return matchSelector(chunk, selector, out);
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
        bool found = harvestMagnets(body, out, m_def.torrentSelector,
                                    originOf(m_def.urlTemplate));
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
    // The .torrent links a listing prints are relative to the site it came
    // from, and both of a site's mirror addresses serve the same paths.
    const std::string origin = originOf(m_def.urlTemplate);
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
            if (applySelector(chunk, m_def.torrentSelector, v)) {
                r.torrentUrl = absoluteUrl(origin, v);
            }
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
