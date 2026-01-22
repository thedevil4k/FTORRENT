#include "CreateTorrentDialog.h"
#include <FL/Fl.H>
#include <FL/Fl_Box.H>
#include <FL/Fl_File_Chooser.H>
#include <FL/fl_ask.H>
#include "PathUtils.h"
#include <libtorrent/create_torrent.hpp>
#include <libtorrent/torrent_info.hpp>
#include <libtorrent/bencode.hpp>
#include <libtorrent/file_storage.hpp>
#include <fstream>
#include <thread>
#include <iostream>
#include <unordered_set>
#include <sstream>
#include <algorithm>
#include <filesystem>

namespace lt = libtorrent;

CreateTorrentDialog::CreateTorrentDialog()
    : Fl_Window(500, 850, "Create Torrent")
    , m_okClicked(false)
{
    // Ensure trackers file exists with some defaults if not present
    std::string trackerFile = PathUtils::getAppDirPath() + "/trackersadd.txt";
    std::ifstream check(trackerFile);
    if (!check.good()) {
        std::ofstream create(trackerFile);
        if (create.is_open()) {
            create << "udp://tracker.opentrackr.org:1337/announce\n"
                      "udp://open.stealth.si:80/announce\n"
                      "udp://tracker.torrent.eu.org:451/announce\n"
                      "http://wegkxfcivgx.ydns.eu:80/announce\n"
                      "http://lucke.fenesisu.moe:6969/announce\n"
                      "udp://tracker.ducks.party:1984/announce\n"
                      "udp://extracker.dahrkael.net:6969/announce\n"
                      "udp://tracker.tvunderground.org.ru:3218/announce\n"
                      "http://tracker.xn--djrq4gl4hvoi.top:80/announce\n"
                      "udp://torrentclub.online:54123/announce\n"
                      "udp://tracker.flatuslifir.is:6969/announce\n"
                      "udp://ns575949.ip-51-222-82.net:6969/announce\n"
                      "udp://tracker.ixuexi.click:6969/announce\n"
                      "udp://tracker.orsvarn.com:6969/announce\n"
                      "udp://tracker.corpscorp.online:80/announce\n"
                      "http://open.acgtracker.com:1096/announce\n"
                      "https://tracker.moeblog.cn:443/announce\n"
                      "udp://tracker.tryhackx.org:6969/announce\n"
                      "udp://bittorrent-tracker.e-n-c-r-y-p-t.net:1337/announce\n"
                      "udp://tracker1.t-1.org:6969/announce\n"
                      "http://taciturn-shadow.spb.ru:6969/announce\n"
                      "http://tracker.renfei.net:8080/announce\n"
                      "http://ipv4.rer.lol:2710/announce\n"
                      "udp://tracker.playground.ru:6969/announce\n"
                      "udp://tracker.qu.ax:6969/announce\n"
                      "udp://d40969.acod.regrucolo.ru:6969/announce\n"
                      "udp://retracker.lanta.me:2710/announce\n"
                      "udp://tracker3.t-1.org:6969/announce\n"
                      "https://tracker.qingwa.pro:443/announce\n"
                      "udp://tracker.bluefrog.pw:2710/announce\n"
                      "udp://tracker.t-1.org:6969/announce\n"
                      "http://tracker.mywaifu.best:6969/announce\n"
                      "udp://martin-gebhardt.eu:25/announce\n"
                      "https://tracker.moeking.me:443/announce\n"
                      "udp://tracker.1h.is:1337/announce\n"
                      "udp://tracker.fnix.net:6969/announce\n"
                      "https://tracker.ghostchu-services.top:443/announce\n"
                      "http://tracker.dhitechnical.com:6969/announce\n"
                      "https://tracker.pmman.tech:443/announce\n"
                      "http://aboutbeautifulgallopinghorsesinthegreenpasture.online:80/announce\n"
                      "udp://tracker.gmi.gd:6969/announce\n"
                      "udp://tracker.opentorrent.top:6969/announce\n"
                      "https://shahidrazi.online:443/announce\n"
                      "udp://tracker.dler.com:6969/announce\n"
                      "http://trackr.appolon.dev:6969/announce\n"
                      "http://tr.highstar.shop:80/announce\n"
                      "http://bt.okmp3.ru:2710/announce\n"
                      "https://tracker.zhuqiy.com:443/announce\n"
                      "https://tracker.iochimari.moe:443/announce\n"
                      "udp://tr4ck3r.duckdns.org:6969/announce\n"
                      "http://bvarf.tracker.sh:2086/announce\n"
                      "https://cny.fan:443/announce\n"
                      "udp://public.demonoid.ch:6969/announce\n"
                      "udp://www.torrent.eu.org:451/announce\n"
                      "udp://retracker01-msk-virt.corbina.net:80/announce\n"
                      "udp://open.demonoid.ch:6969/announce\n"
                      "udp://tracker.srv00.com:6969/announce\n"
                      "udp://open.demonii.com:1337/announce\n"
                      "https://torrent.tracker.durukanbal.com:443/announce\n"
                      "udp://tracker.plx.im:6969/announce\n";
            create.close();
        }
    }
    
    createUI();
    end();
    set_modal();
}

CreateTorrentDialog::~CreateTorrentDialog() {
}

void CreateTorrentDialog::createUI() {
    int y = 20;

    // Source selection
    new Fl_Box(20, y, 460, 25, "Select File or Folder to share:");
    ((Fl_Box*)child(children()-1))->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
    y += 30;

    m_sourcePath = new Fl_Input(20, y, 300, 30);
    m_btnSelectFile = new Fl_Button(330, y, 70, 30, "File...");
    m_btnSelectFolder = new Fl_Button(410, y, 70, 30, "Folder...");
    
    m_btnSelectFile->callback(onSelectFile, this);
    m_btnSelectFolder->callback(onSelectFolder, this);
    y += 50;

    // Settings
    new Fl_Box(20, y, 100, 25, "Settings:");
    ((Fl_Box*)child(children()-1))->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
    y += 30;

    m_pieceSize = new Fl_Choice(120, y, 120, 25, "Piece Size:");
    m_pieceSize->add("Auto");
    m_pieceSize->add("16 KiB");
    m_pieceSize->add("32 KiB");
    m_pieceSize->add("64 KiB");
    m_pieceSize->add("128 KiB");
    m_pieceSize->add("256 KiB");
    m_pieceSize->add("512 KiB");
    m_pieceSize->add("1 MiB");
    m_pieceSize->add("2 MiB");
    m_pieceSize->add("4 MiB");
    m_pieceSize->value(0);
    y += 40;

    m_isPrivate = new Fl_Check_Button(20, y, 200, 25, "Private (no DHT/LSD)");
    y += 30;
    m_startSeeding = new Fl_Check_Button(20, y, 200, 25, "Start seeding immediately");
    m_startSeeding->value(1);
    y += 30;
    m_optimizeAlignment = new Fl_Check_Button(20, y, 200, 25, "Optimize alignment");
    m_optimizeAlignment->value(1);
    y += 40;

    // Trackers
    new Fl_Box(20, y, 100, 25, "Trackers (one per line):");
    ((Fl_Box*)child(children()-1))->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
    
    m_btnAddGenericTrackers = new Fl_Button(330, y, 150, 25, "Add Generic Trackers");
    m_btnAddGenericTrackers->labelsize(11);
    m_btnAddGenericTrackers->callback(onAddGenericTrackers, this);
    
    y += 25;
    m_trackers = new Fl_Multiline_Input(20, y, 460, 80);
    y += 90;
    
    // Web Seeds
    new Fl_Box(20, y, 150, 25, "Web Seeds (one per line):");
    ((Fl_Box*)child(children()-1))->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
    y += 25;
    m_webSeeds = new Fl_Multiline_Input(20, y, 460, 80);
    y += 90;

    // Comment
    new Fl_Box(20, y, 100, 25, "Comment:");
    ((Fl_Box*)child(children()-1))->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
    y += 25;
    m_comment = new Fl_Multiline_Input(20, y, 460, 50);
    y += 70;
    
    // Origin
    new Fl_Box(20, y, 100, 25, "Origin:");
    ((Fl_Box*)child(children()-1))->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
    y += 25;
    m_origin = new Fl_Input(20, y, 460, 30);
    y += 50;

    // Progress
    m_progress = new Fl_Progress(20, y, 460, 25);
    m_progress->minimum(0);
    m_progress->maximum(100);
    m_progress->value(0);
    m_progress->selection_color(fl_rgb_color(50, 150, 50));
    y += 40;

    // Buttons
    m_btnCreate = new Fl_Button(300, y, 80, 30, "Create");
    m_btnCancel = new Fl_Button(390, y, 80, 30, "Cancel");
    
    m_btnCreate->callback(onCreate, this);
    m_btnCancel->callback(onCancel, this);
}

bool CreateTorrentDialog::show_modal() {
    show();
    while (shown()) {
        Fl::wait();
    }
    return m_okClicked;
}

void CreateTorrentDialog::onSelectFile(Fl_Widget* w, void* data) {
    const char* f = PathUtils::showFileChooser("Select File", "*", nullptr, Fl_File_Chooser::SINGLE);
    if (f) ((CreateTorrentDialog*)data)->m_sourcePath->value(f);
}

void CreateTorrentDialog::onSelectFolder(Fl_Widget* w, void* data) {
    const char* d = PathUtils::showDirChooser("Select Folder", nullptr);
    if (d) ((CreateTorrentDialog*)data)->m_sourcePath->value(d);
}

void CreateTorrentDialog::onCreate(Fl_Widget* w, void* data) {
    ((CreateTorrentDialog*)data)->processCreate();
}

void CreateTorrentDialog::onCancel(Fl_Widget* w, void* data) {
    ((CreateTorrentDialog*)data)->hide();
}

void CreateTorrentDialog::onAddGenericTrackers(Fl_Widget* w, void* data) {
    CreateTorrentDialog* dlg = (CreateTorrentDialog*)data;
    
    // Get existing trackers and put them in a set for easy lookup
    std::unordered_set<std::string> existing;
    std::string currentText = dlg->m_trackers->value();
    std::stringstream ss(currentText);
    std::string line;
    while (std::getline(ss, line)) {
        // Simple trim
        size_t first = line.find_first_not_of(" \t\r\n");
        if (first != std::string::npos) {
            size_t last = line.find_last_not_of(" \t\r\n");
            existing.insert(line.substr(first, (last - first + 1)));
        }
    }
    
    std::string toAdd;
    std::string trackerFile = PathUtils::getAppDirPath() + "/trackersadd.txt";
    std::ifstream file(trackerFile);
    if (file.is_open()) {
        while (std::getline(file, line)) {
            size_t first = line.find_first_not_of(" \t\r\n");
            if (first != std::string::npos) {
                size_t last = line.find_last_not_of(" \t\r\n");
                std::string trimmed = line.substr(first, (last - first + 1));
                if (existing.find(trimmed) == existing.end()) {
                    toAdd += trimmed + "\n";
                    existing.insert(trimmed); // Prevent adding duplicates from the file itself too
                }
            }
        }
        file.close();
    } else {
        fl_alert("Could not open trackersadd.txt. Please ensure it exists in the application directory.");
        return;
    }
    
    if (toAdd.empty()) {
        fl_message("All generic trackers from the list are already present.");
        return;
    }

    if (!currentText.empty() && currentText.back() != '\n') currentText += "\n";
    dlg->m_trackers->value((currentText + toAdd).c_str());
}

void CreateTorrentDialog::processCreate() {
    std::string source = m_sourcePath->value();
    if (source.empty()) {
        fl_alert("Please select a source file or folder");
        return;
    }

    const char* saveFile = PathUtils::showFileChooser("Save Torrent As", "Torrent Files (*.torrent)", "new_torrent.torrent", Fl_File_Chooser::CREATE);
    if (!saveFile) return;

    std::string savePath = saveFile;
    m_btnCreate->deactivate();
    m_btnCancel->deactivate();

    // Hashing process in a basic way for now
    // In a real app, this should be in a thread with UI updates
    try {
        lt::file_storage fs;
        lt::add_files(fs, source);

        int pieceSize = 0;
        int choice = m_pieceSize->value();
        if (choice > 0) pieceSize = (16 * 1024) << (choice - 1);

        lt::create_flags_t flags = {};
        // optimize_alignment is deprecated or removed in newer libtorrent versions.
        // canonical_files is the modern equivalent for aligning and ordering files.
        if (m_optimizeAlignment->value()) flags |= lt::create_torrent::canonical_files;

        lt::create_torrent ct(fs, pieceSize, flags);
        
        // Add trackers
        std::stringstream ss(m_trackers->value());
        std::string line;
        while (std::getline(ss, line)) {
            if (!line.empty()) ct.add_tracker(line);
        }
        
        // Add Web Seeds
        std::stringstream ssWeb(m_webSeeds->value());
        while (std::getline(ssWeb, line)) {
            if (!line.empty()) ct.add_url_seed(line);
        }

        if (m_isPrivate->value()) ct.set_priv(true);
        ct.set_comment(m_comment->value());
        ct.set_creator("fTorrent");

        // Hash pieces
        m_progress->label("Hashing pieces...");
        Fl::check();
        
        // On Windows, libtorrent's set_piece_hashes needs the parent directory 
        // because add_files adds files with the source directory name as a prefix.
        // We use std::filesystem to get the parent path safely.
#if defined(_WIN32) && defined(__cpp_lib_char8_t)
        std::filesystem::path sourcePath(std::u8string_view((const char8_t*)source.c_str(), source.length()));
#else
        std::filesystem::path sourcePath = std::filesystem::u8path(source);
#endif
        std::filesystem::path parent = sourcePath.parent_path();
        
        std::string parentPathStr;
#if defined(_WIN32) && defined(__cpp_lib_char8_t)
        std::u8string u8Parent = parent.u8string();
        parentPathStr.assign((const char*)u8Parent.data(), u8Parent.size());
#else
        parentPathStr = parent.u8string();
#endif
        if (parentPathStr.empty()) parentPathStr = ".";

        lt::error_code ec;
        lt::set_piece_hashes(ct, parentPathStr, [this](lt::piece_index_t p) {
            // Update progress if we were in a thread, but here we just keep it blocking
        }, ec);

        if (ec) {
            throw std::system_error(ec.value(), std::generic_category(), ec.message());
        }

        lt::entry e = ct.generate();
        if (strlen(m_origin->value()) > 0) e["source"] = m_origin->value();

        std::vector<char> torrent;
        lt::bencode(std::back_inserter(torrent), e);

        // Use std::filesystem::path to handle UTF-8 paths on Windows correctly
        std::filesystem::path pSave;
#if defined(_WIN32) && defined(__cpp_lib_char8_t)
        pSave = std::filesystem::path(std::u8string_view((const char8_t*)savePath.c_str(), savePath.length()));
#else
        pSave = std::filesystem::u8path(savePath);
#endif
        std::ofstream out(pSave, std::ios_base::binary);
        if (!out) {
            fl_alert("Could not open file for writing: %s", savePath.c_str());
            return;
        }
        out.write(torrent.data(), torrent.size());
        out.close();

        fl_message("Torrent created successfully!");
        m_okClicked = true;
        hide();

    } catch (const std::exception& e) {
        fl_alert("Error creating torrent: %s", e.what());
        m_btnCreate->activate();
        m_btnCancel->activate();
    }
}
