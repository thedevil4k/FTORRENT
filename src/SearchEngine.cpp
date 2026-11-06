#include "SearchEngine.h"
#include "HttpClient.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <functional>
#include <mutex>
#include <thread>

// URL-encodes a query string (%s slot of the engine URL)
std::string SearchEngine::urlEncode(const std::string& in) {
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

// Fetches through the direct address, and retries through the proxy/mirror
// when the direct one is blocked or returns nothing usable. That retry is what
// keeps a site working after it rotates its domain, without any code change.
static bool
fetchWithFallback(const std::string& url, const std::string& proxyUrl,
                  HttpClient::Response& resp, std::string& error,
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
        error = resp.ok ? std::string("The site returned no usable results")
                        : resp.error;
    }

    // Cancelling must not silently turn into a second request to the mirror.
    if (shouldStop && shouldStop()) {
        error = "Cancelled";
        return false;
    }

    if (!proxyUrl.empty()) {
        HttpClient::Response viaProxy =
            HttpClient::get(proxyUrl, ua, 20, shouldStop);
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
                             const std::string& category, int page,
                             const StopCheck& shouldStop,
                             std::vector<SearchResult>& out, std::string& error,
                             PageInfo* info) const {
    std::string url = buildUrl(query, category, page);
    std::string proxyUrl = buildProxyUrl(query, category, page);

    if (!HttpClient::isSecure(url) && proxyUrl.empty()) {
        error = "The site does not use https";
        return false;
    }

    HttpClient::Response resp;
    std::string usedVia;
    if (!fetchWithFallback(
            url, proxyUrl, resp, error, usedVia,
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
            if (res.ok && jsonField(res.body, "magnet", magnet) &&
                !magnet.empty()) {
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
    rows.erase(
        std::remove_if(rows.begin(), rows.end(),
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
        snprintf(buf, sizeof(buf), "%llu B",
                 (unsigned long long)info.bodyBytes);
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
                          std::vector<SearchResult>& out, std::string& error,
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
        if (!fetchPage(query, category, page, shouldStop, found, error,
                       &info)) {
            if (page == 1) return false;
            break;
        }
        if (found.empty()) {
            if (page == 1) {
                error = "No results found on " + m_def.name + " (" +
                        describeFetch(info) + ", parsed 0)";
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
                    (pages > 1 ? " (page " + std::to_string(page) + "/" +
                                     std::to_string(pages) + ")..."
                               : "..."));
        }
        std::vector<SearchResult> found;
        PageInfo info;
        if (!fetchPage(query, category, page, shouldStop, found, error,
                       &info)) {
            if (page == 1) return false;
            break;
        }
        if (found.empty()) {
            if (page == 1) {
                error = "No results found on " + m_def.name + " (" +
                        describeFetch(info) + ", parsed 0)";
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
            std::vector<SearchResult> chunk(found.begin() + i,
                                            found.begin() + end);
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
