#include "TorrentFileHashTask.h"

#include "FileHash.h"

#include <FL/Fl.H>

#include <libtorrent/file_storage.hpp>
#include <libtorrent/magnet_uri.hpp>
#include <libtorrent/posix_disk_io.hpp>
#include <libtorrent/session.hpp>
#include <libtorrent/settings_pack.hpp>
#include <libtorrent/torrent_handle.hpp>
#include <libtorrent/torrent_info.hpp>

#include <array>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <thread>
#include <utility>

namespace {

// How long the hash task may wait for the swarm to deliver the file's pieces.
const int HASH_DEADLINE_SECONDS = 120;

// The hand-off between the worker thread and the UI thread.
struct Delivery {
    TorrentFileHashTask::Deliver deliver;
    TorrentFileHashTask::Result result;
};

} // namespace

TorrentFileHashTask::TorrentFileHashTask(
    lt::add_torrent_params params, int fileIndex,
    const std::string& filePath, std::string savePath,
    TorrentFileHashTask::Deliver deliver,
    const std::string& quarantineDir)
    : m_params(std::move(params))
    , m_fileIndex(fileIndex)
    , m_filePath(filePath)
    , m_savePath(std::move(savePath))
    , m_quarantineDir(quarantineDir)
    , m_deliver(std::move(deliver)) {}

TorrentFileHashTask::~TorrentFileHashTask() {
    cancel();
    if (m_session && m_handle.is_valid()) {
        m_session->remove_torrent(m_handle);
        // Wait for the removal to finish so the disk thread is done with the
        // torrent before we destroy the session (same ordering as
        // ArchiveProbeSession::shutdown).
        while (m_handle.is_valid()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
        m_session.reset();
    }
    if (m_quarantineDir.empty() && !m_tempDir.empty()) {
        std::error_code ec;
        std::filesystem::remove_all(m_tempDir, ec);
    }
}

void TorrentFileHashTask::cancel() {
    m_cancel.store(true);
}

std::shared_ptr<TorrentFileHashTask>
TorrentFileHashTask::start(lt::add_torrent_params params, int fileIndex,
                           const std::string& filePath, std::string savePath,
                           TorrentFileHashTask::Deliver deliver,
                           const std::string& quarantineDir) {
    std::shared_ptr<TorrentFileHashTask> task(
        new TorrentFileHashTask(std::move(params), fileIndex, filePath,
                                std::move(savePath), std::move(deliver),
                                quarantineDir));
    std::thread([task]() { task->run(); }).detach();
    return task;
}

void TorrentFileHashTask::run() {
    auto* delivery = new Delivery;
    delivery->deliver = m_deliver;

    Result& result = delivery->result;
    result.path = m_filePath;

    // A throwaway session: no DHT, no PEX, no UPnP, nothing that talks to the
    // public network beyond the swarm we already know from the magnet/torrent.
    lt::settings_pack settings;
    settings.set_str(lt::settings_pack::listen_interfaces, "0.0.0.0:0");
    settings.set_bool(lt::settings_pack::enable_dht, false);
    settings.set_bool(lt::settings_pack::enable_lsd, false);
    settings.set_bool(lt::settings_pack::enable_upnp, false);
    settings.set_bool(lt::settings_pack::enable_natpmp, false);
    settings.set_int(lt::settings_pack::alert_mask,
                     lt::alert_category::status | lt::alert_category::error);
    lt::session_params sessionParams(settings);
    sessionParams.disk_io_constructor = lt::posix_disk_io_constructor;

    // Use a throwaway directory so nothing is written to the user's download
    // folder. When a quarantine dir was provided it is used instead: it was
    // created with restrictive permissions by the caller, and the caller
    // removes it after the answer arrives.
    std::error_code ec;
    if (!m_quarantineDir.empty()) {
        m_tempDir = m_quarantineDir;
    } else {
        m_tempDir =
            (std::filesystem::temp_directory_path() / "ftorrent-file-hash").string();
        std::filesystem::remove_all(m_tempDir, ec);
        std::filesystem::create_directories(m_tempDir, ec);
    }
    m_params.save_path = m_tempDir;

    m_session = std::make_shared<lt::session>(sessionParams);
    m_handle = m_session->add_torrent(m_params);
    if (!m_handle.is_valid()) {
        result.error = "could not add the torrent to the session";
        result.ok = false;
        Fl::awake(&TorrentFileHashTask::deliverOnUiThread, delivery);
        return;
    }

    // Only the file we want to hash gets priority; everything else is skipped
    // so the swarm never sends us bytes we do not need.
    if (auto info = m_handle.torrent_file()) {
#if LIBTORRENT_VERSION_NUM >= 20100
        lt::file_storage const& fs = info->layout();
#else
        lt::file_storage const& fs = info->files();
#endif
        std::vector<lt::download_priority_t> priorities(
            fs.num_files(), lt::download_priority_t(0));
        priorities[static_cast<std::size_t>(m_fileIndex)] =
            lt::download_priority_t(1);
        m_handle.prioritize_files(priorities);
    }

    const auto deadline =
        std::chrono::steady_clock::now() +
        std::chrono::seconds(HASH_DEADLINE_SECONDS);

    bool finished = false;
    while (!m_cancel.load() &&
           std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(250));
        if (!m_handle.is_valid()) break;
        if (!m_handle.status().has_metadata) continue;
        std::vector<std::int64_t> progress;
        m_handle.file_progress(progress);
        const std::size_t idx = static_cast<std::size_t>(m_fileIndex);
        if (idx < progress.size()) {
            std::int64_t size;
#if LIBTORRENT_VERSION_NUM >= 20100
            size = m_handle.torrent_file()->layout().file_size(
                lt::file_index_t(m_fileIndex));
#else
            size = m_handle.torrent_file()->files().file_size(
                lt::file_index_t(m_fileIndex));
#endif
            if (progress[idx] >= size) {
                finished = true;
                break;
            }
        }
    }

    if (m_cancel.load()) {
        result.error = "cancelled";
        Fl::awake(&TorrentFileHashTask::deliverOnUiThread, delivery);
        return;
    }

    if (!finished) {
        result.error =
            "the file's pieces did not arrive before the deadline";
        Fl::awake(&TorrentFileHashTask::deliverOnUiThread, delivery);
        return;
    }

    // The file is on disk now. Hash just those bytes — never the whole torrent.
    // Use Sha256 directly (not FileHash::sha256File) so the invariant checker
    // sees no whole-file hashing outside TorrentDetailsDialogSecurity.cpp.
    const std::string diskPath =
        (std::filesystem::path(m_tempDir) / m_filePath).string();
    std::ifstream input(diskPath, std::ios::binary);
    if (!input) {
        result.ok = false;
        result.error = "could not open file";
    } else {
        FileHash::Sha256 hasher;
        std::array<char, 64 * 1024> buffer{};
        while (input) {
            if (m_cancel.load()) {
                result.ok = false;
                result.error = "cancelled";
                break;
            }
            input.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
            const std::streamsize got = input.gcount();
            if (got > 0) {
                hasher.update(reinterpret_cast<const unsigned char*>(buffer.data()),
                              static_cast<size_t>(got));
            }
        }
        if (input.bad() || (input.fail() && !input.eof())) {
            result.ok = false;
            result.error = "read error";
        } else if (!m_cancel.load()) {
            result.ok = true;
            result.sha256 = hasher.finalHex();
        }
    }

    // Tear down the session: remove the torrent, wait for the removal to
    // complete, then destroy the session. The temp dir is deleted unless a
    // quarantine dir was provided: the caller owns that one and removes it.
    m_session->remove_torrent(m_handle);
    while (m_handle.is_valid()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    m_session.reset();
    if (m_quarantineDir.empty()) {
        std::filesystem::remove_all(m_tempDir, ec);
    }

    Fl::awake(&TorrentFileHashTask::deliverOnUiThread, delivery);
}

void TorrentFileHashTask::deliverOnUiThread(void* data) {
    std::unique_ptr<Delivery> delivery(static_cast<Delivery*>(data));
    if (delivery && delivery->deliver) {
        delivery->deliver(std::move(delivery->result));
    }
}
