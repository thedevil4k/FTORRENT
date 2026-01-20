cmake_minimum_required(VERSION 3.15)
project(FLTorrent VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(WIN32)
    add_compile_definitions(NOMINMAX)
endif()

# Find FLTK
find_package(FLTK REQUIRED COMPONENTS images)

# Find Dependencies
find_package(ZLIB REQUIRED)
find_package(PNG REQUIRED)
find_package(JPEG REQUIRED)

# Try to find Libtorrent via CMake config or pkg-config
find_package(LibtorrentRasterbar QUIET)
if(NOT LibtorrentRasterbar_FOUND)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(LIBTORRENT libtorrent-rasterbar REQUIRED IMPORTED_TARGET)
    add_library(LibtorrentRasterbar::torrent-rasterbar ALIAS PkgConfig::LIBTORRENT)
endif()

# Add source files
set(SOURCES
    src/main.cpp
    src/TorrentSession.cpp
    src/TorrentItem.cpp
    src/TorrentManager.cpp
    src/SettingsManager.cpp
    src/MainWindow.cpp
    src/TorrentListWidget.cpp
    src/PreferencesDialog.cpp
    src/AddTorrentDialog.cpp
    src/CreateTorrentDialog.cpp
    src/TorrentDetailsDialog.cpp
    src/Resources.cpp
    src/PathUtils.cpp
    src/RemoveConfirmDialog.cpp
)

if(WIN32)
    list(APPEND SOURCES src/resources.rc)
endif()

set(HEADERS
    src/TorrentSession.h
    src/TorrentItem.h
    src/TorrentManager.h
    src/SettingsManager.h
    src/MainWindow.h
    src/TorrentListWidget.h
    src/PreferencesDialog.h
    src/AddTorrentDialog.h
    src/CreateTorrentDialog.h
    src/TorrentDetailsDialog.h
    src/Resources.h
    src/Icons.h
    src/PathUtils.h
    src/RemoveConfirmDialog.h
)

# Add executable
add_executable(FLTorrent ${SOURCES} ${HEADERS})

# Include directories
target_include_directories(FLTorrent PRIVATE 
    ${FLTK_INCLUDE_DIRS}
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

# Link libraries
target_link_libraries(FLTorrent PRIVATE 
    ${FLTK_LIBRARIES}
    PNG::PNG
    JPEG::JPEG
    ZLIB::ZLIB
    LibtorrentRasterbar::torrent-rasterbar
)

# Platform-specific settings
if(WIN32)
    target_link_libraries(FLTorrent PRIVATE ws2_32 wsock32 comctl32)
    # Use standard windowed app (no console)
    set_target_properties(FLTorrent PROPERTIES WIN32_EXECUTABLE ON)
    # Define Windows version
    target_compile_definitions(FLTorrent PRIVATE _WIN32_WINNT=0x0A00)
else()
    # On Linux, we might need pthread
    find_package(Threads REQUIRED)
    target_link_libraries(FLTorrent PRIVATE Threads::Threads)
endif()

# Installation Rules
include(GNUInstallDirs)
install(TARGETS FLTorrent
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
