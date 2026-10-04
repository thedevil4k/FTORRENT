#include <FL/Fl.H>
#include "MainWindow.h"
#include "TorrentManager.h"
#include "SettingsManager.h"
#include "Resources.h"
#include <memory>
#include <iostream>

int main(int argc, char **argv) {
    // Makes the FLTK toolkit usable from the worker threads below.
    //
    // The search feature hands its results back to this thread with
    // Fl::awake(), and every Fl::awake() call was being dropped on the floor:
    // without this call the event loop never drains the awake queue, so the
    // searches did run and did parse, but no row ever reached the table and the
    // status line stayed on "Searching..." forever.
    //
    // It has to come before any thread is created (libtorrent starts its own in
    // TorrentManager::initialize(), and the search and latency workers call
    // Fl::awake()), which is why it is the first thing in main().
    //
    // No Fl::mutex() is needed anywhere else: the callbacks registered with
    // Fl::awake() are already run on this thread by Fl::wait().
    Fl::lock();

    // Initialize resources (icons, etc.)
    Resources::initialize();
    auto& settings = SettingsManager::instance();
    settings.load();
    
    // Create torrent manager
    auto manager = std::make_unique<TorrentManager>();
    
    // Initialize torrent session
    if (!manager->initialize()) {
        std::cerr << "Failed to initialize TorrentManager" << std::endl;
        return 1;
    }
    
    std::cout << "FTorrent initialized successfully" << std::endl;
    
    // Create main window
    MainWindow* window = new MainWindow(
        settings.getWindowWidth(),
        settings.getWindowHeight(),
        "FTorrent"
    );
    
    // Connect manager to window
    window->setTorrentManager(manager.get());
    
    // Show window
    window->show();
    
    // Run FLTK event loop
    int result = Fl::run();
    
    // Cleanup
    manager->shutdown();
    settings.save();
    Resources::cleanup();
    
    std::cout << "FTorrent shutdown complete" << std::endl;
    
    return result;
}
