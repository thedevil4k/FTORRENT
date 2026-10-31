// SearchEngine::parse() checks.
//
// Two promises are pinned here, both of them regressions the user hit when
// expanding a search row:
//
//   1. A magnet is kept as the site published it. Rebuilding the link from the
//      info hash dropped every tr= entry, and the probe then had to find peers
//      through the DHT alone. Nyaa.si and BTDig publish magnets with working
//      trackers, so those rows went from "live swarm" to "no peers" purely in
//      the parser.
//
//   2. The .torrent button next to a magnet belongs to that row and to no
//      other. Rows are parsed from one flat page, so a careless search would
//      hand row N row N+1's file.
#include "SearchEngine.h"

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

// The definition the app ships for 'name', so the test exercises the real
// selectors and not a copy that can drift from them.
SearchEngine::Definition builtin(const std::string& name) {
    for (const SearchEngine::Definition& def : SearchEngine::builtinEngines()) {
        if (def.name == name) return def;
    }
    return SearchEngine::Definition{};
}

// Two Nyaa-style rows. The download button sits before the magnet it belongs
// to, exactly as the site prints it, and the magnets carry their trackers.
const char* NYAA_BODY = R"HTML(
<table><tbody>
<tr class="default">
  <td><a href="/view/2180637" title="[NanakoRaws] One Piece - 994">[NanakoRaws] One Piece - 994</a></td>
  <td class="text-center">
    <a href="/download/2180637.torrent"><i class="fa fa-download"></i></a>
    <a href="magnet:?xt=urn:btih:13df974956a3ff5584247f850539c1a6dad6a7e4&amp;dn=%5BNanakoRaws%5D%20One%20Piece%20-%20994&amp;tr=http%3A%2F%2Fnyaa.tracker.wf%3A7777%2Fannounce&amp;tr=udp%3A%2F%2Ftracker.opentrackr.org%3A1337%2Fannounce">m</a>
  </td>
  <td class="text-center">1.4 GiB</td>
  <td class="text-center">42</td>
</tr>
<tr class="default">
  <td><a href="/view/2180638" title="[SubsPlease] One Piece - 995">[SubsPlease] One Piece - 995</a></td>
  <td class="text-center">
    <a href="/download/2180638.torrent"><i class="fa fa-download"></i></a>
    <a href="magnet:?xt=urn:btih:aaaabbbbccccddddeeeeffff0000111122223333&amp;dn=%5BSubsPlease%5D%20One%20Piece%20-%20995&amp;tr=udp%3A%2F%2Fopen.stealth.si%3A80%2Fannounce">m</a>
  </td>
  <td class="text-center">700 MiB</td>
  <td class="text-center">7</td>
</tr>
<tr class="default">
  <td><a href="/view/2180639" title="Trackerless row">Trackerless row</a></td>
  <td class="text-center">
    <a href="magnet:?xt=urn:btih:1111222233334444555566667777888899990000&amp;dn=Trackerless%20row">m</a>
  </td>
  <td class="text-center">12 MiB</td>
  <td class="text-center">1</td>
</tr>
</tbody></table>
)HTML";

// Two BitSearch-style cards: the shared backend prints the .torrent button
// with a ?title= tail, which is deliberately not carried into the URL.
const char* SOLID_BODY = R"HTML(
<div class="card">
  <h3><a href="/torrent/abc123">[Bitsearch.to] Adobe Premiere Pro 2024</a></h3>
  <div class="hidden sm:flex">
    <a href="/download/torrent/46B55A78EB8C1673083EEB924E4EFA64DAE2F05C?title=Adobe%20Premiere%20Pro">Torrent</a>
    <a href="magnet:?xt&#x3D;urn:btih:46B55A78EB8C1673083EEB924E4EFA64DAE2F05C&amp;dn=Adobe&amp;tr=udp%3A%2F%2Ftracker.opentrackr.org%3A1337%2Fannounce">m</a>
  </div>
  <span>1.5 GiB</span><span class="seeders"><span>12</span> <span>seeders</span></span>
</div>
<div class="card">
  <h3><a href="/torrent/def456">[Bitsearch.to] Adobe Premiere Pro CC 2018</a></h3>
  <div class="hidden sm:flex">
    <a href="/download/torrent/8A7A8C340B6E2D294AD8A359BEDDFE602B53F3DC?title=Adobe%20Premiere%20CC">Torrent</a>
    <a href="magnet:?xt&#x3D;urn:btih:8A7A8C340B6E2D294AD8A359BEDDFE602B53F3DC&amp;dn=Adobe%20CC&amp;tr=udp%3A%2F%2Ftracker.opentrackr.org%3A1337%2Fannounce">m</a>
  </div>
  <span>2.0 GiB</span><span class="seeders"><span>3</span> <span>seeders</span></span>
</div>
)HTML";

// apibay answers with a bare info hash and no trackers at all. The parser
// must still build a usable magnet for it.
const char* APIBAY_BODY = R"JSON(
[{"id":"1","name":"Ubuntu 22.04 LTS","info_hash":"2C6B6858D61DA9543D4231A71DB4B1C9264B0685","leechers":"11","seeders":"34","num_files":"1","size":"3654957056","username":"x","added":"1","status":"vip","category":"303"}]
)JSON";

std::vector<SearchResult> parse(const SearchEngine::Definition& def,
                                const std::string& body) {
    SearchEngine engine(def);
    std::vector<SearchResult> rows;
    engine.parse(body, rows);
    return rows;
}

} // namespace

int main() {
    section("a published magnet keeps its trackers");
    {
        const std::vector<SearchResult> rows = parse(builtin("Nyaa.si"), NYAA_BODY);
        ok(rows.size() == 3, "three Nyaa rows parsed");

        if (rows.size() == 3) {
            const std::string published =
                "magnet:?xt=urn:btih:13df974956a3ff5584247f850539c1a6dad6a7e4"
                "&dn=%5BNanakoRaws%5D%20One%20Piece%20-%20994"
                "&tr=http%3A%2F%2Fnyaa.tracker.wf%3A7777%2Fannounce"
                "&tr=udp%3A%2F%2Ftracker.opentrackr.org%3A1337%2Fannounce";
            ok(rows[0].magnet == published,
               "row 1 keeps the link exactly as published, trackers included");
            ok(rows[0].magnet.find("tr=") != std::string::npos,
               "row 1 still carries tr= entries");
            ok(rows[1].magnet.find("tr=udp%3A%2F%2Fopen.stealth.si%3A80%2Fannounce")
                   != std::string::npos,
               "row 2 keeps its own tracker");
            ok(rows[0].name == "[NanakoRaws] One Piece - 994",
               "the name still comes from the magnet's dn=");
        }
    }

    section("each row gets its own .torrent link");
    {
        const std::vector<SearchResult> rows = parse(builtin("Nyaa.si"), NYAA_BODY);
        if (rows.size() == 3) {
            ok(rows[0].torrentUrl == "https://nyaa.si/download/2180637.torrent",
               "row 1 points at its own .torrent");
            ok(rows[1].torrentUrl == "https://nyaa.si/download/2180638.torrent",
               "row 2 does not borrow row 1's .torrent");
            ok(rows[2].torrentUrl.empty(),
               "a row without a .torrent button gets none");
        }
    }

    section("the shared BitSearch markup");
    {
        const SearchEngine::Definition def = builtin("BitSearch");
        ok(!def.torrentSelector.empty(), "BitSearch declares a .torrent selector");

        const std::vector<SearchResult> rows = parse(def, SOLID_BODY);
        ok(rows.size() == 2, "two cards parsed");

        if (rows.size() == 2) {
            ok(rows[0].torrentUrl ==
                   "https://bitsearch.eu/download/torrent/"
                   "46B55A78EB8C1673083EEB924E4EFA64DAE2F05C",
               "the ?title= tail is left out of the .torrent URL");
            ok(rows[1].torrentUrl ==
                   "https://bitsearch.eu/download/torrent/"
                   "8A7A8C340B6E2D294AD8A359BEDDFE602B53F3DC",
               "the second card keeps its own hash");
            ok(rows[0].magnet.find("tr=udp%3A%2F%2Ftracker.opentrackr.org")
                   != std::string::npos,
               "explicit magnets keep their trackers too");
            ok(rows[0].name == "[Bitsearch.to] Adobe Premiere Pro 2024",
               "the title selector still wins over the magnet's dn=");
        }
    }

    section("engines without a .torrent button");
    {
        ok(builtin("The Pirate Bay (API)").torrentSelector.empty(),
           "the apibay API declares none");
        ok(builtin("The Pirate Bay").torrentSelector.empty(),
           "the HTML Pirate Bay declares none");
        ok(builtin("BTDig").torrentSelector.empty(),
           "BTDig declares none");

        const std::vector<SearchResult> rows =
            parse(builtin("The Pirate Bay (API)"), APIBAY_BODY);
        ok(rows.size() == 1, "one apibay row parsed");
        if (rows.size() == 1) {
            ok(rows[0].magnet ==
                   "magnet:?xt=urn:btih:2C6B6858D61DA9543D4231A71DB4B1C9264B0685"
                   "&dn=Ubuntu%2022.04%20LTS",
               "a bare info hash still becomes a magnet");
            ok(rows[0].torrentUrl.empty(), "and it carries no .torrent link");
        }
    }

    section("the .torrent selector survives the plugin file");
    {
        const SearchEngine::Definition original = builtin("Nyaa.si");
        const std::string ini = SearchEngine::serializeDefinition(original);
        ok(ini.find("torrent=") != std::string::npos,
           "serializeDefinition writes the torrent= key");

        const SearchEngine::Definition back = SearchEngine::parseDefinition(ini);
        ok(back.torrentSelector == original.torrentSelector,
           "parseDefinition reads it back unchanged");
    }

    section("a relative link is never guessed");
    {
        SearchEngine::Definition def;
        def.name = "Relative";
        def.type = "html";
        // No scheme, so there is no host to resolve against.
        def.urlTemplate = "search?q=%s";
        def.torrentSelector = "href=\"(/download/[0-9]+\\.torrent)\"";

        const std::vector<SearchResult> rows = parse(def, NYAA_BODY);
        ok(!rows.empty(), "the rows still parse");
        ok(rows[0].torrentUrl.empty(),
           "a root-relative link with no known host is dropped, not guessed");
    }

    std::cout << "\nsearch_parse checks=" << checks
              << " failures=" << failures << "\n";
    return failures ? 1 : 0;
}
