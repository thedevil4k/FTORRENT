// TorrentFileList checks that need no network.
//
// The probe itself talks to swarms, but three of its pieces are pure and are
// what the rest of the app relies on:
//
//   - the tracker and DHT lists every probe now uses, so a trackerless magnet
//     can still meet a peer;
//   - the info hash of a .torrent file, which is what stops a mirror serving a
//     different torrent from being shown as the row the user clicked;
//   - reading a .torrent body into file entries, with the hash filled in.
#include "TorrentFileList.h"

#include <libtorrent/hasher.hpp>

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

std::string hexOf(const std::string& raw) {
    static const char digits[] = "0123456789abcdef";
    std::string out;
    for (unsigned char byte : raw) {
        out.push_back(digits[byte >> 4]);
        out.push_back(digits[byte & 0x0f]);
    }
    return out;
}

// A one-file torrent built by hand, so the test never depends on a downloaded
// file. The info dictionary is kept separately because its SHA-1 is the value
// every client calls the info hash.
const std::string INFO_DICT =
    "d5:filesld6:lengthi6e4:pathl5:a.txteee"
    "4:name4:test"
    "12:piece lengthi16384e"
    "6:pieces20:" +
    std::string("\x01\x02\x03\x04\x05\x06\x07\x08\x09\x0a\x0b\x0c\x0d\x0e"
                "\x0f\x10\x11\x12\x13\x14",
                20) +
    "e";

std::string torrentBody() {
    return "d8:announce20:http://t.example/ann4:info" + INFO_DICT + "e";
}

std::string expectedInfoHash() {
    libtorrent::hasher h;
    h.update(INFO_DICT.data(), static_cast<int>(INFO_DICT.size()));
    const libtorrent::sha1_hash hash = h.final();
    return hexOf(std::string(hash.data(), 20));
}

} // namespace

int main() {
    section("default trackers reach a trackerless magnet");
    {
        const std::vector<std::string>& trackers =
            TorrentFileList::defaultTrackers();
        ok(!trackers.empty(), "there is a default tracker list");
        bool allAnnounce = true;
        for (const std::string& tracker : trackers) {
            if (tracker.find("/announce") == std::string::npos)
                allAnnounce = false;
        }
        ok(allAnnounce, "every default tracker is an announce URL");

        std::vector<std::string> none;
        TorrentFileList::addDefaultTrackers(none);
        ok(none.size() == trackers.size(),
           "an empty list gets exactly the defaults");

        std::vector<std::string> withOne = {trackers[0],
                                            "udp://mine.example:80/announce"};
        TorrentFileList::addDefaultTrackers(withOne);
        ok(withOne.size() == trackers.size() + 1,
           "a tracker already present is not added twice");
        ok(withOne[0] == trackers[0] &&
               withOne[1] == "udp://mine.example:80/announce",
           "the magnet's own trackers keep their place at the front");
    }

    section("DHT bootstrap nodes");
    {
        const std::string nodes = TorrentFileList::dhtBootstrapNodes();
        ok(nodes.find("dht.libtorrent.org:25401") != std::string::npos,
           "the node measured to answer is listed");
        ok(nodes.find("dht.transmissionbt.com:6881") != std::string::npos,
           "and so is the second one");
        ok(nodes.find("router.bittorrent.com:6881") != std::string::npos,
           "libtorrent's default router is still there for other networks");
        ok(nodes.find(':') != std::string::npos &&
               nodes.find(' ') == std::string::npos,
           "host:port comma-separated, as the setting expects");
    }

    section("the cache key is one hash, however it was spelled");
    {
        const std::string lower = "13df974956a3ff5584247f850539c1a6dad6a7e4";
        ok(TorrentFileList::magnetHash(
               "magnet:?xt=urn:btih:13DF974956A3FF5584247F850539C1A6DAD6A7E4&"
               "dn=x") == lower,
           "hex is lowercased");
        ok(TorrentFileList::magnetHash(
               "magnet:?xt=urn:btih:CPPZOSKWUP7VLBBEP6CQKOOBU3NNNJ7E&dn=x") ==
               lower,
           "base32 becomes the same hex");
        ok(TorrentFileList::magnetHash(
               "magnet:?xt=urn:btih:AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA") ==
               std::string(40, '0'),
           "an all-zero base32 hash decodes to forty zeros");
        ok(TorrentFileList::magnetHash("magnet:?dn=nothing").empty(),
           "a link with no btih has no key");
    }

    section("a .torrent body becomes files and a hash");
    {
        const std::string body = torrentBody();
        std::vector<TorrentFileEntry> files;
        std::string error;
        std::string hash;
        const bool parsed =
            TorrentFileList::parseTorrentFile(body, files, error, &hash);
        ok(parsed, "the hand-built torrent parses");
        if (!parsed) std::cout << "        error: " << error << '\n';
        ok(files.size() == 1, "one file listed");
        if (files.size() == 1) {
            // libtorrent prefixes the torrent name, and the separator is the
            // platform's, so only the ending is portable.
            const std::string& path = files[0].path;
            ok(path.size() >= 5 &&
                   path.compare(path.size() - 5, 5, "a.txt") == 0,
               "the path ends with the file the torrent lists");
            ok(files[0].size == 6, "and so is the size");
        }
        ok(hash == expectedInfoHash(),
           "the hash is the SHA-1 of the info dictionary");

        ok(TorrentFileList::infoHashOfTorrentFile(body) == hash,
           "infoHashOfTorrentFile answers the same value");
        ok(TorrentFileList::infoHashOfTorrentFile("not a torrent").empty(),
           "garbage has no hash");
    }

    section("a broken body is refused, not guessed");
    {
        std::vector<TorrentFileEntry> files;
        std::string error;
        ok(!TorrentFileList::parseTorrentFile("<html>404</html>", files, error),
           "an error page is not a torrent");
        ok(!error.empty(), "and it says why");
        ok(files.empty(), "with no files invented");
    }

    section("resolve refuses what it cannot do safely");
    {
        std::vector<TorrentFileEntry> files;
        std::string error;

        ok(!TorrentFileList::resolve("", "http://example.com/a.torrent", files,
                                     error, 1),
           "a plain-http .torrent link is refused");
        ok(error.find("https") != std::string::npos,
           "the message names the https rule");

        error.clear();
        ok(!TorrentFileList::resolve("", "", files, error, 1),
           "nothing to fetch is a failure");
        ok(!error.empty(), "with an explanation");
    }

    std::cout << "\nfilelist checks=" << checks << " failures=" << failures
              << "\n";
    return failures ? 1 : 0;
}
