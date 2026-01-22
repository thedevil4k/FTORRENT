#include <FL/Fl.H>
#include "MainWindow.h"
#include "TorrentManager.h"
#include "SettingsManager.h"
#include "Resources.h"
#include <memory>
#include <iostream>

int main(int argc, char **argv) {
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
