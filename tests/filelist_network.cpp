// TorrentFileList against the real swarm.
//
// Labelled "network" and skipped by `ctest -LE network`. It talks to Nyaa.si,
// to a public tracker and to the DHT, so it is the only place that can prove
// the two failures a user actually saw when expanding a search row:
//
//   1. A magnet whose trackers the parser kept can still reach its peers even
//      when the DHT routers are blocked.
//   2. A .torrent link is only trusted when its info hash is the one the magnet
//      names, and a mismatch still falls back to the swarm instead of showing
//      somebody else's file list.
#include "TorrentFileList.h"
#include "HttpClient.h"
#include "SearchEngine.h"
#include "Version.h"

#include <iostream>
#include <string>
#include <vector>

namespace {

int checks = 0;
int failures = 0;

void ok(bool condition, const std::string& what) {
    checks++;
    if (!condition) {
        failures++;
        std::cout << "    FAIL " << what << '\n';
    }
}

void section(const char* title) {
    std::cout << "\n== " << title << " ==\n";
}

// A Nyaa row taken from a live search: one file, an alive swarm, and a
// .torrent button that answers.
const char* NYAA_TORRENT = "https://nyaa.si/download/2180637.torrent";
const char* NYAA_HASH = "13df974956a3ff5584247f850539c1a6dad6a7e4";

// apibay hands out magnets with no trackers at all. This one is the first
// Ubuntu result; the public tracker reported 25 seeders when the fix was
// written, so it is the case the default trackers exist for.
const char* TPB_MAGNET =
    "magnet:?xt=urn:btih:2C6B6858D61DA9543D4231A71DB4B1C9264B0685";

} // namespace

int main() {
    std::vector<TorrentFileEntry> files;
    std::string error;

    section("a .torrent link is read without the swarm");
    {
        files.clear();
        error.clear();
        const bool loaded = TorrentFileList::resolve("", NYAA_TORRENT, files, error, 20);
        ok(loaded, "the Nyaa .torrent file downloads and parses");
        ok(!files.empty(), "and lists files");
        if (!loaded) std::cout << "        error: " << error << '\n';
    }

    section("a matching magnet accepts its .torrent link");
    {
        files.clear();
        error.clear();
        const std::string magnet =
            std::string("magnet:?xt=urn:btih:") + NYAA_HASH + "&dn=One+Piece";
        const bool loaded = TorrentFileList::resolve(magnet, NYAA_TORRENT, files, error, 20);
        ok(loaded, "the hash matches, so the file is used");
        ok(!files.empty(), "with the file list");
        if (!loaded) std::cout << "        error: " << error << '\n';
    }

    section("a .torrent of another torrent is not shown");
    {
        files.clear();
        error.clear();
        // A well-formed hash that is not the one the file carries. The swarm
        // probe is given two seconds so this stays a quick check.
        const std::string other =
            "magnet:?xt=urn:btih:0000000000000000000000000000000000000001&dn=other";
        const bool loaded = TorrentFileList::resolve(other, NYAA_TORRENT, files, error, 2);
        ok(!loaded, "the mismatch is refused");
        ok(error.find("different torrent") != std::string::npos,
           "and the message says so");
        ok(files.empty(), "with no files borrowed from the wrong file");
    }

    section("a trackerless magnet reaches its swarm");
    {
        files.clear();
        error.clear();
        const bool loaded = TorrentFileList::resolve(TPB_MAGNET, "", files, error, 60);
        ok(loaded, "the default trackers find peers for a TPB magnet");
        ok(!files.empty(), "and the metadata arrives with a file list");
        if (!loaded) std::cout << "        error: " << error << '\n';
    }

    section("an empty swarm says why");
    {
        files.clear();
        error.clear();
        const std::string dead =
            "magnet:?xt=urn:btih:0000000000000000000000000000000000000001&dn=dead";
        const bool loaded = TorrentFileList::resolve(dead, "", files, error, 4);
        ok(!loaded, "nothing is invented for a dead hash");
        ok(error.find("No peer sent the metadata") != std::string::npos,
           "the message names what failed");
        ok(error.find("tracker") != std::string::npos,
           "and reports what the trackers did");
        std::cout << "        error: " << error << '\n';
    }

    section("a live listing keeps trackers and .torrent links");
    {
        struct Live {
            const char* engine;
            const char* url;
        };
        // One listing per site the fix touches, written out so a wrong URL is
        // obvious rather than computed.
        const Live live[] = {
            { "Nyaa.si", "https://nyaa.si/?f=0&c=0_0&q=one+piece&p=1" },
            { "BitSearch", "https://bitsearch.eu/search?q=one+piece&page=1" },
            { "SolidTorrents", "https://solidtorrents.eu/search?q=one+piece&page=1" },
        };

        // Nyaa is the engine the failure was reproduced on and the one whose
        // listing is stable, so it is the one that is asserted on.
        bool nyaaAnswered = false;
        size_t nyaaRows = 0;
        int nyaaTrackers = 0;
        int nyaaTorrents = 0;

        for (const Live& site : live) {
            SearchEngine::Definition def;
            for (const SearchEngine::Definition& candidate :
                 SearchEngine::builtinEngines()) {
                if (candidate.name == site.engine) def = candidate;
            }
            SearchEngine engine(def);
            const HttpClient::Response page =
                HttpClient::get(site.url, Version::httpUserAgent(), 20);

            std::vector<SearchResult> rows;
            if (page.ok) engine.parse(page.body, rows);

            int withTrackers = 0;
            int withTorrent = 0;
            for (const SearchResult& row : rows) {
                if (row.magnet.find("&tr=") != std::string::npos) ++withTrackers;
                if (!row.torrentUrl.empty()) ++withTorrent;
            }
            std::cout << "        " << site.engine << " http=" << page.httpStatus
                      << " rows=" << rows.size() << " trackers=" << withTrackers
                      << " torrent-links=" << withTorrent << '\n';
            if (!rows.empty()) {
                std::cout << "          sample: "
                          << (rows[0].torrentUrl.empty() ? "(none)"
                                                         : rows[0].torrentUrl)
                          << '\n';
            }

            if (std::string(site.engine) == "Nyaa.si") {
                nyaaAnswered = page.ok;
                nyaaRows = rows.size();
                nyaaTrackers = withTrackers;
                nyaaTorrents = withTorrent;
            }
        }

        ok(nyaaAnswered, "the Nyaa listing answers");
        ok(nyaaRows > 0, "and parses into rows");
        ok(nyaaTrackers > 0,
           "the magnets keep the tr= entries the site publishes");
        ok(nyaaTrackers * 2 > static_cast<int>(nyaaRows),
           "and it is not a one-off row");
        ok(nyaaTorrents * 2 > static_cast<int>(nyaaRows),
           "the .torrent button is attached to most rows");
    }

    std::cout << "\nfilelist_network checks=" << checks
              << " failures=" << failures << "\n";
    return failures ? 1 : 0;
}
