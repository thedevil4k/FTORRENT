#ifndef SEARCHENGINE_H
#define SEARCHENGINE_H

#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <vector>

// Kept in sync with the version in CMakeLists.txt and MainWindow.h, but
// duplicated here so the search engine does not have to include the GUI.
#define FTP_VERSION "0.5.0"

/**
 * @brief Declarative torrent search engine, plus the built-in engine list.
 *
 * An engine is described by a small INI-like file so that adding a new site
 * does not require touching the C++ code. Format (see src/search_engines/):
 *
 *   nombre=BitSearch
 *   url=https://bitsearch.eu/search?q=%s
 *   tipo=html            (html | json)
 *   fila=item            (html: the part of the page repeated per result)
 *   magnet=item:<a href="(/magnet:\?xt=urn:btih:[^"]+)">
 *   titulo=item:<h2[^>]*>([^<]+)</h2>
 *   tamano=item:([0-9.]+)\s*([KMGT]i?B)
 *   semillas=item:([0-9,]+)\s*seeders
 *
 * Rules are <campo>:<selector>, and each selector yields group 1. For JSON
 * engines the selector is a dotted path such as "torrentName" or
 * "stats.seeds". %s inside url is replaced by the URL-encoded query.
 */
struct SearchResult {
    std::string name;
    std::string magnet;
    // Some engines answer with a .torrent URL instead of a magnet; it is
    // downloaded on demand when the user picks the result.
    std::string torrentUrl;
    // Identifies the entry when the listing itself carries no magnet and the
    // link has to be asked for separately (Pirate Face returns the catalogue
    // and hands out one magnet per request). Empty for every other engine.
    std::string detailId;
    int64_t size = 0;          // Bytes, 0 when unknown
    int seeders = -1;          // -1 when unknown
    int leechers = -1;         // -1 when unknown
    std::string source;        // Engine name that produced it
};

class SearchEngine {
public:
    // One configured search site.
    //
    // A site is described the way torrends.to describes the ones it wraps:
    // a base address and a search pattern with %s where the query goes. When
    // the direct address is blocked (Cloudflare, geo-block, dead mirror) the
    // optional proxy address is tried as a fallback, which is what makes the
    // built-in catalogue survive sites changing their domain.
    struct Definition {
        std::string name;
        std::string urlTemplate;           // direct address + pattern
        std::string proxyTemplate;         // same, against a mirror/proxy
        std::string type = "html";         // "html", "json" or "lines"
        // Optional per-site extraction rules. When rowSelector is empty the
        // parser falls back to harvesting every magnet link it can find, which
        // is what keeps unknown or redesigned sites working.
        std::string rowSelector;
        std::string magnetSelector;
        std::string titleSelector;
        std::string sizeSelector;
        std::string seedersSelector;
        std::string leechersSelector;
        // For type "lines": one regex applied to each response line, with
        // fixed groups 1=name, 2=size ("56 GB"), 3=seeders, 4=magnet link.
        // Used by endpoints answering plain text instead of HTML or JSON.
        std::string linePattern;
        // Listings that do not include the magnet. When both are set, rows
        // without a magnet are kept and each one is resolved through
        // detailUrlTemplate, where %i stands for the value detailIdSelector
        // points at, URL-encoded. Empty for every other engine, which makes
        // the extra step a no-op.
        std::string detailIdSelector;
        std::string detailUrlTemplate;
        // Site categories as shown in the dropdown ("Movies") to the code the
        // site expects ("201"). Empty when the site has no categories.
        std::map<std::string, std::string> categories;
        // How many result pages to walk (page 1..N). The urlTemplate carries
        // %p for the page number; without %p only page 1 is ever fetched.
        int maxPages = 1;
        bool enabled = true;
    };

    explicit SearchEngine(const Definition& def) : m_def(def) {}

    const Definition& definition() const { return m_def; }

    // Replaces %s (query), %c (category code) and %p (page) in the URL
    std::string buildUrl(const std::string& query,
                         const std::string& category = "all",
                         int page = 1) const;

    // Same, against the proxy/mirror; empty when there is none
    std::string buildProxyUrl(const std::string& query,
                              const std::string& category = "all",
                              int page = 1) const;

    using BatchCallback = std::function<void(const std::vector<SearchResult>&)>;
    // One-line stage reports ("Contacting X (page 1/3)..."). Called from the
    // worker thread, so the receiver must hop to the UI thread itself.
    using StageCallback = std::function<void(const std::string&)>;
    // Returns true when the user asked to cancel. Checked while the request is
    // in flight, not only between steps, so cancelling takes effect at once.
    using StopCheck = std::function<bool()>;

    // Fetches and parses. Returns false and fills 'error' on failure.
    bool search(const std::string& query,
                std::vector<SearchResult>& out,
                std::string& error,
                const StopCheck& shouldStop = nullptr,
                const std::string& category = "all") const;

    // Same, but calls onResult for every batch as it arrives so the UI can
    // fill the table progressively instead of waiting for the whole page.
    // Stops early and returns false if shouldStop() turns true (cancel button).
    bool searchStreaming(const std::string& query,
                         const BatchCallback& onBatch,
                         const StopCheck& shouldStop,
                         std::string& error,
                         const std::string& category = "all",
                         const StageCallback& onStage = nullptr) const;

    // Parses a body that was already fetched (used by search() and by tests)
    bool parse(const std::string& body, std::vector<SearchResult>& out) const;

    // Engines that work without an account, shipped with the app
    static std::vector<Definition> builtinEngines();

    // Reads/writes engine definitions as INI text
    static Definition parseDefinition(const std::string& iniText);
    static std::string serializeDefinition(const Definition& def);

    // What the network actually did on one fetched page, so an empty search
    // can report it instead of silence.
    struct PageInfo {
        long httpStatus = 0;
        size_t bodyBytes = 0;
        std::string via;   // "direct" or "proxy"
    };
    bool fetchPage(const std::string& query,
                   const std::string& category,
                   int page,
                   const StopCheck& shouldStop,
                   std::vector<SearchResult>& out,
                   std::string& error,
                   PageInfo* info = nullptr) const;

private:
    Definition m_def;

    // Applies one "<selector>" rule to a chunk of text and returns group 1
    bool applySelector(const std::string& chunk, const std::string& selector,
                       std::string& out) const;

    // Fills in the magnet of every row that only carries a detailId, and drops
    // the rows that turn out to have none at all.
    void resolveDetails(std::vector<SearchResult>& rows,
                        const StopCheck& shouldStop) const;

    // Parses "1.5 GB" / "700MB" / "1024" into bytes
    static int64_t parseSize(const std::string& text);
    static int parseCount(const std::string& text);

    // Site code for a dropdown category, with safe fallbacks
    std::string categoryCode(const std::string& category) const;
};

#endif // SEARCHENGINE_H