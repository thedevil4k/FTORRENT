#include "SearchEngine.h"

#include <cstdlib>
#include <sstream>
#include <string>
#include <vector>

namespace {

std::string trim(const std::string& s) {
    size_t b = s.find_first_not_of(" \t\r\n");
    if (b == std::string::npos) return "";
    size_t e = s.find_last_not_of(" \t\r\n");
    return s.substr(b, e - b + 1);
}

} // namespace

SearchEngine::Definition
SearchEngine::parseDefinition(const std::string& iniText) {
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
        else if (key == "torrent") def.torrentSelector = val;
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
        } else if (key == "paginas") def.maxPages = std::atoi(val.c_str());
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
    if (!def.rowSelector.empty()) ss << "fila=" << def.rowSelector << "\n";
    if (!def.magnetSelector.empty())
        ss << "magnet=" << def.magnetSelector << "\n";
    if (!def.torrentSelector.empty())
        ss << "torrent=" << def.torrentSelector << "\n";
    if (!def.titleSelector.empty())
        ss << "titulo=" << def.titleSelector << "\n";
    if (!def.sizeSelector.empty()) ss << "tamano=" << def.sizeSelector << "\n";
    if (!def.seedersSelector.empty())
        ss << "semillas=" << def.seedersSelector << "\n";
    if (!def.leechersSelector.empty())
        ss << "descargas=" << def.leechersSelector << "\n";
    if (!def.linePattern.empty()) ss << "linea=" << def.linePattern << "\n";
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
    struct SolidDef {
        const char* name;
        const char* url;
        const char* proxy;
    };
    static const SolidDef solidSites[] = {
        {"SolidTorrents", "https://solidtorrents.eu", "https://bitsearch.eu"},
        {"BitSearch", "https://bitsearch.eu", "https://solidtorrents.eu"},
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
        // The card also carries a .torrent button; the hash-only form is kept
        // so the probe does not have to send the title back to the site.
        d.torrentSelector = "href=\"(/download/torrent/[0-9A-Fa-f]{40})";
        d.titleSelector =
            "<a href=\"/torrent/[^\"]*\"[^>]*>\\s*([^<]+?)\\s*</a>";
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
        // Each row prints a /download/<id>.torrent button beside its magnet.
        d.torrentSelector = "href=\"(/download/[0-9]+\\.torrent)\"";
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
        d.leechersSelector = "<td align=\"right\">[0-9]+</td>\\s*<td "
                             "align=\"right\">([0-9]+)</td>";
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
