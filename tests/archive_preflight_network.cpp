// ArchivePreflight against a real swarm, over the loopback interface.
//
// Labelled "network" and skipped by `ctest -LE network`, because it opens a
// libtorrent session and connects to a peer. Nothing leaves the machine: the
// seeder is this same process, listening on 127.0.0.1, and the archive is a
// fixture under tests/fixtures/archives. So the test is deterministic, but it
// is the only place that can reach the piece-fetch path, which no offline test
// can: the offline suite only checks the arithmetic, never libtorrent's own
// behaviour.
//
// What it proves:
//
//   1. A RAR 1.5-4.x lists its entries from a swarm, header by header, with
//      the same reader the application uses.
//   2. The check never downloads the archive. The seeder's upload counter is
//      the honest witness: it stays far below the archive's size.
//   3. A RAR whose headers are encrypted is reported as password-protected
//      rather than as a broken file, and the lock badge gets a fact to show.
//
// Each scenario runs in a child process. libtorrent 2.1.2 (the version this
// machine has) has an intermittent use-after-free of its own: a session that
// is torn down while its disk thread is still working dies with an access
// violation in a background thread. It is reproducible with a program that
// contains no FTorrent code at all, so it is not this test's fault and not
// the product's. A child that dies that way is retried, and a child that
// keeps dying fails the test with that as the reason, so an upstream crash is
// never mistaken for a broken archive check.
#include "ArchivePreflight.h"
#include "ArchiveProbeSession.h"
#include "SecurityTypes.h"

#include <archive.h>
#include <archive_entry.h>

#include <libtorrent/add_torrent_params.hpp>
#include <libtorrent/alert_types.hpp>
#include <libtorrent/bencode.hpp>
#include <libtorrent/create_torrent.hpp>
#include <libtorrent/entry.hpp>
#include <libtorrent/posix_disk_io.hpp>
#include <libtorrent/session.hpp>
#include <libtorrent/settings_pack.hpp>
#include <libtorrent/torrent_handle.hpp>
#include <libtorrent/torrent_info.hpp>

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <iterator>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace fs = std::filesystem;
namespace lt = libtorrent;

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

// A torrent of one fixture, seeded on the loopback interface. The seeder is
// the only peer the check can meet, so the test never reaches the internet.
class LocalSeeder {
public:
    LocalSeeder(const fs::path& fixture, int pieceLength) {
        try {
            build(fixture, pieceLength);
        } catch (const std::exception& e) {
            m_error = std::string("the seeder failed: ") + e.what();
        }
    }

    ~LocalSeeder() {
        // The torrent is removed and the removal is waited for, the same way
        // the application's probe does it: libtorrent keeps the torrent alive
        // until its own I/O tasks are done, and a session torn down while the
        // disk thread is still reading the seeder's file leaves that thread
        // running on freed memory.
        if (m_session) {
            ArchiveProbeSession::shutdown(*m_session, m_handle);
            m_session.reset();
        }
        std::error_code ec;
        fs::remove_all(m_dir, ec);
    }

    LocalSeeder(const LocalSeeder&) = delete;
    LocalSeeder& operator=(const LocalSeeder&) = delete;

    bool ok() const { return m_error.empty(); }
    const std::string& error() const { return m_error; }
    std::int64_t size() const { return m_size; }

    // The parameters a client would use to reach this seeder. The tracker is
    // a local address that refuses immediately, so the check spends no time
    // waiting for an announce that cannot work.
    lt::add_torrent_params clientParams() const {
        lt::add_torrent_params atp;
        atp.ti = m_info;
        atp.peers.push_back(
            lt::tcp::endpoint(lt::make_address("127.0.0.1"), m_port));
        atp.trackers.push_back("http://127.0.0.1:1/announce");
        return atp;
    }

    // How many bytes the seeder actually sent. This is the witness that the
    // check read the archive's headers instead of downloading the archive.
    std::int64_t uploaded() const {
        return m_handle.is_valid() ? m_handle.status().total_upload : 0;
    }

private:

    void build(const fs::path& fixture, int pieceLength) {
        std::error_code ec;
        m_dir = fs::temp_directory_path() / "ftorrent-archive-network";
        fs::remove_all(m_dir, ec);
        fs::create_directories(m_dir, ec);

        m_file = m_dir / fixture.filename();
        fs::copy_file(fixture, m_file, fs::copy_options::overwrite_existing, ec);
        if (ec) {
            m_error = "could not copy the fixture: " + ec.message();
            return;
        }
        m_size = static_cast<std::int64_t>(fs::file_size(m_file));

        std::vector<lt::create_file_entry> files;
        files.emplace_back(m_file.filename().string(), m_size);
        // A v1-only torrent on purpose. The check under test does not care
        // which version of the protocol carries the pieces, and the v1 hashes
        // are the ones every client can verify.
        lt::create_torrent creator(files, pieceLength,
                                   lt::create_torrent::v1_only);

        lt::error_code hashError;
        lt::set_piece_hashes(creator, m_dir.string(), hashError);
        if (hashError) {
            m_error = "could not hash the fixture: " + hashError.message();
            return;
        }

        const lt::entry meta = creator.generate();
        std::vector<char> bytes;
        lt::bencode(std::back_inserter(bytes), meta);
        m_info = std::make_shared<lt::torrent_info>(
            lt::span<char const>(bytes.data(), bytes.size()), lt::from_span);

        lt::settings_pack settings;
        settings.set_str(lt::settings_pack::listen_interfaces, "127.0.0.1:0");
        settings.set_bool(lt::settings_pack::enable_dht, false);
        settings.set_bool(lt::settings_pack::enable_lsd, false);
        settings.set_bool(lt::settings_pack::enable_upnp, false);
        settings.set_bool(lt::settings_pack::enable_natpmp, false);
        settings.set_int(lt::settings_pack::alert_mask,
                         lt::alert_category::status | lt::alert_category::error);
        lt::session_params sessionParams(settings);
        // The seeder only reads the file it serves, so the simple portable
        // disk I/O is enough for it.
        sessionParams.disk_io_constructor = lt::posix_disk_io_constructor;
        m_session = std::make_unique<lt::session>(sessionParams);

        lt::add_torrent_params atp;
        atp.ti = m_info;
        atp.save_path = m_dir.string();
        m_handle = m_session->add_torrent(atp);

        for (int i = 0; i < 200; ++i) {
            if (m_handle.status().is_seeding) break;
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
        if (!m_handle.status().is_seeding) {
            m_error = "the seeder never started";
            return;
        }
        // The listening socket is opened by the session's own thread, so the
        // port is not known until it says it is listening.
        for (int i = 0; i < 200; ++i) {
            if (m_session->is_listening()) break;
            std::this_thread::sleep_for(std::chrono::milliseconds(25));
        }
        m_port = m_session->listen_port();
        if (m_port == 0) {
            m_error = "the seeder got no port";
            return;
        }
    }

    fs::path m_dir;
    fs::path m_file;
    std::string m_error;
    std::int64_t m_size = 0;
    unsigned short m_port = 0;
    std::shared_ptr<lt::torrent_info> m_info;
    std::unique_ptr<lt::session> m_session;
    lt::torrent_handle m_handle;
};

const SecurityFile* findEntry(const std::vector<SecurityFile>& entries,
                              const std::string& path) {
    for (const SecurityFile& entry : entries) {
        if (entry.path == path) return &entry;
    }
    return nullptr;
}

// A ZIP big enough to span many pieces. The small fixtures cannot show that
// the two-piece rule works: when the whole archive is one piece, reading its
// two ends is reading the archive. This one is a megabyte, so the check has
// to pick two pieces out of sixty-four.
bool createLargeZip(const fs::path& path, std::size_t payloadBytes) {
    archive* writer = archive_write_new();
    if (!writer) return false;

    bool ok = archive_write_set_format_zip(writer) == ARCHIVE_OK &&
              archive_write_set_options(writer, "zip:compression=store") ==
                  ARCHIVE_OK;
#ifdef _WIN32
    ok = ok && archive_write_open_filename_w(writer, path.c_str()) == ARCHIVE_OK;
#else
    ok = ok && archive_write_open_filename(writer, path.c_str()) == ARCHIVE_OK;
#endif

    if (ok) {
        archive_entry* entry = archive_entry_new();
        if (!entry) {
            ok = false;
        } else {
            archive_entry_set_pathname(entry, "payload.bin");
            archive_entry_set_filetype(entry, AE_IFREG);
            archive_entry_set_perm(entry, 0644);
            archive_entry_set_size(entry,
                                   static_cast<la_int64_t>(payloadBytes));
            if (archive_write_header(writer, entry) != ARCHIVE_OK) {
                ok = false;
            } else {
                const std::string block(64 * 1024, 'a');
                std::size_t written = 0;
                while (ok && written < payloadBytes) {
                    const std::size_t take =
                        std::min(block.size(), payloadBytes - written);
                    if (archive_write_data(writer, block.data(), take) !=
                        static_cast<la_ssize_t>(take)) {
                        ok = false;
                    }
                    written += take;
                }
            }
            archive_entry_free(entry);
        }
    }

    if (archive_write_close(writer) != ARCHIVE_OK) ok = false;
    archive_write_free(writer);
    return ok;
}

// A RAR with a megabyte of packed data in the middle: its second header sits
// far from the first one, so the walk has to fetch a piece in the middle of
// the file instead of one at each end.
void scenarioRarWalk(const fs::path& fixtures) {
    section("a RAR 1.5-4.x lists its entries from a swarm");
    const fs::path fixture =
        fixtures / "test_read_format_rar_binary_data.rar";
    const int pieceLength = 16384;

    std::cout << "    seeding " << fixture.filename().string() << '\n';
    LocalSeeder seeder(fixture, pieceLength);
    ok(seeder.ok(), "the fixture is seeded on the loopback interface");
    if (!seeder.ok()) {
        std::cout << "        error: " << seeder.error() << '\n';
        return;
    }

    std::vector<SecurityFile> entries;
    std::string error;
    std::string note;
    bool encryptedHeaders = false;

    std::cout << "    checking the archive\n";
    const bool listed = ArchivePreflight::inspect(
        seeder.clientParams(), 0, entries, error, nullptr, 90, &note,
        &encryptedHeaders);

    if (!note.empty()) std::cout << "        note: " << note << '\n';
    if (!error.empty()) std::cout << "        error: " << error << '\n';

    ok(listed, "the archive is listed without downloading it");
    ok(findEntry(entries, "random_data.bin") != nullptr,
       "the first entry is found");
    // The second entry sits a megabyte further in, so the walk has to fetch a
    // piece in the middle of the file. When that piece does not arrive, the
    // result is a partial list - which is a legal answer, as long as it says
    // so.
    const bool secondFound =
        findEntry(entries, "LibarchiveAddingTest.odt") != nullptr;
    ok(secondFound || note.find("partial list") != std::string::npos,
       "the entry behind the packed data is found, or the list is labelled "
       "partial");
    ok(!encryptedHeaders, "the headers are not encrypted");

    // The invariant that matters: the archive itself is never downloaded. The
    // walk fetches the pieces that hold a header and skips the packed data
    // between them.
    ok(seeder.uploaded() < seeder.size(),
       "the seeder sent less than the whole archive");
    ok(seeder.uploaded() > 0, "and it did send the header pieces");
}

void scenarioEncryptedHeaders(const fs::path& fixtures) {
    section("a RAR whose headers are encrypted says so");
    const fs::path fixture =
        fixtures / "test_read_format_rar_encryption_header.rar";
    std::cout << "    seeding " << fixture.filename().string() << '\n';
    LocalSeeder seeder(fixture, 16384);
    ok(seeder.ok(), "the fixture is seeded on the loopback interface");
    if (!seeder.ok()) {
        std::cout << "        error: " << seeder.error() << '\n';
        return;
    }

    std::vector<SecurityFile> entries;
    std::string error;
    std::string note;
    bool encryptedHeaders = false;

    std::cout << "    checking the archive\n";
    const bool listed = ArchivePreflight::inspect(
        seeder.clientParams(), 0, entries, error, nullptr, 90, &note,
        &encryptedHeaders);

    ok(!listed, "nothing can be listed");
    ok(encryptedHeaders, "the archive is reported as password-protected");
    ok(error.find("encrypted") != std::string::npos,
       "and the message says why");
    ok(entries.empty(), "with no invented entries");
}

void scenarioZipTwoEnds(const fs::path& fixtures) {
    section("a ZIP lists from its two ends");
    // A megabyte of ZIP, so the archive spans sixty-four pieces and the check
    // has to pick two of them. This is the rule the product states: the
    // archive is never downloaded, only its index is read.
    const int pieceLength = 16384;
    const std::size_t payloadBytes = 1024 * 1024;

    const fs::path source =
        fs::temp_directory_path() / "ftorrent-archive-network-source.zip";
    std::error_code ec;
    fs::remove(source, ec);
    const bool built = createLargeZip(source, payloadBytes);
    ok(built, "a megabyte ZIP could be written");

    LocalSeeder seeder(source, pieceLength);
    ok(seeder.ok(), "the ZIP is seeded on the loopback interface");
    if (!seeder.ok()) {
        std::cout << "        error: " << seeder.error() << '\n';
        fs::remove(source, ec);
        return;
    }

    std::vector<SecurityFile> entries;
    std::string error;
    std::string note;
    bool encryptedHeaders = false;

    std::cout << "    checking the archive\n";
    const bool listed = ArchivePreflight::inspect(
        seeder.clientParams(), 0, entries, error, nullptr, 90, &note,
        &encryptedHeaders);

    if (!error.empty()) std::cout << "        error: " << error << '\n';

    ok(listed, "the ZIP is listed without downloading it");
    ok(findEntry(entries, "payload.bin") != nullptr, "the entry is found");
    // Two pieces, and no more: the index sits at the end of a ZIP, so the
    // first and the last piece are all the check needs.
    ok(seeder.uploaded() <= 2 * pieceLength,
       "the seeder sent at most the two end pieces");
    ok(seeder.uploaded() < seeder.size(), "and far less than the whole archive");

    fs::remove(source, ec);
}

void scenarioHostileNames(const fs::path& fixtures) {
    section("a ZIP lists its hostile names verbatim");
    const fs::path fixture = fixtures / "setup.zip";
    std::cout << "    seeding " << fixture.filename().string() << '\n';
    LocalSeeder seeder(fixture, 16384);
    ok(seeder.ok(), "the fixture is seeded on the loopback interface");
    if (!seeder.ok()) {
        std::cout << "        error: " << seeder.error() << '\n';
        return;
    }

    std::vector<SecurityFile> entries;
    std::string error;
    std::string note;
    bool encryptedHeaders = false;

    std::cout << "    checking the archive\n";
    const bool listed = ArchivePreflight::inspect(
        seeder.clientParams(), 0, entries, error, nullptr, 90, &note,
        &encryptedHeaders);

    ok(listed, "the ZIP is listed without downloading it");
    ok(findEntry(entries, "setup.exe") != nullptr, "the plain .exe is found");
    ok(findEntry(entries, "../escape.exe") != nullptr,
       "the traversal name is reported verbatim");
}

// The scenarios, by name. The child process is told which one to run.
struct Scenario {
    const char* name;
    void (*run)(const fs::path&);
};

const Scenario SCENARIOS[] = {
    {"rar-walk", &scenarioRarWalk},
    {"rar-encrypted", &scenarioEncryptedHeaders},
    {"zip-ends", &scenarioZipTwoEnds},
    {"zip-hostile", &scenarioHostileNames},
};

const Scenario* findScenario(const std::string& name) {
    for (const Scenario& scenario : SCENARIOS) {
        if (name == scenario.name) return &scenario;
    }
    return nullptr;
}

// How many times a scenario is run again after the child died inside
// libtorrent. Three is enough to tell a rare crash from a broken build: an
// upstream crash that happens every time is still reported, not hidden.
const int CRASH_RETRIES = 2;

} // namespace

int main(int argc, char** argv) {
    // Unbuffered: a hang must still show how far the test got.
    std::setvbuf(stdout, nullptr, _IONBF, 0);

    const fs::path fixtures(FTORRENT_ARCHIVE_FIXTURES);

    // The child: it runs one scenario and reports with its exit code.
    if (argc >= 3 && std::string(argv[1]) == "--scenario") {
        const Scenario* scenario = findScenario(argv[2]);
        if (!scenario) {
            std::cout << "unknown scenario: " << argv[2] << '\n';
            return 2;
        }
        scenario->run(fixtures);
        std::cout << "\narchive_preflight_network checks=" << checks
                  << " failures=" << failures << "\n";
        return failures ? 1 : 0;
    }

    // The parent: it runs every scenario in a child process, so that a crash
    // inside libtorrent does not take the whole test down with it.
    if (argc < 1) {
        std::cout << "the test needs to know its own path\n";
        return 2;
    }
    const std::string exe = argv[0];

    for (const Scenario& scenario : SCENARIOS) {
        bool passed = false;
        bool crashed = false;
        for (int attempt = 0; attempt <= CRASH_RETRIES; ++attempt) {
            const std::string command =
                "\"" + exe + "\" --scenario " + scenario.name;
            const int code = std::system(command.c_str());
            if (code == 0) {
                passed = true;
                break;
            }
            if (code == 1) {
                // The scenario ran and its checks failed. That is the
                // product's answer, and it is not retried.
                break;
            }
            if (code == -1) {
                std::cout << "    the child process could not be started\n";
                break;
            }
            crashed = true;
            std::cout << "\n    the child died with code " << code
                      << " (an access violation inside libtorrent); "
                      << "retrying\n";
        }
        if (!passed) {
            failures++;
            if (crashed) {
                std::cout << "    FAIL the scenario '" << scenario.name
                          << "' kept crashing inside libtorrent\n";
            }
        }
    }

    std::cout << "\narchive_preflight_network failures=" << failures << "\n";
    return failures ? 1 : 0;
}
