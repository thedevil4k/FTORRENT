cmake_minimum_required(VERSION 3.15)
project(FTorrent VERSION 0.5.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(WIN32)
    add_compile_definitions(NOMINMAX)
    # Automatically copy dependency DLLs to the install folder for CPack
    set(X_VCPKG_APPLOCAL_DEPS_INSTALL ON)
endif()

# Find FLTK
#
# FLTK 1.4 installations ship their own CMake package (FLTKConfig.cmake) which
# reports ALL include directories of the FLTK build, including those of optional
# dependencies. This is required on distributions that build FLTK with Cairo
# support enabled (e.g. Arch Linux): <FL/Fl_Cairo.H> then includes <cairo.h>,
# which lives in <prefix>/include/cairo. CMake's legacy FindFLTK module does not
# report that directory (and does not even set FLTK_INCLUDE_DIRS), which makes
# the build fail with "cairo.h: No such file or directory".
#
# On Windows (vcpkg) the module is kept because it provides absolute library
# paths, which is what the toolchain expects.
if(WIN32)
    find_package(FLTK REQUIRED COMPONENTS images)
else()
    find_package(FLTK CONFIG QUIET COMPONENTS images)
    if(NOT FLTK_FOUND OR NOT FLTK_INCLUDE_DIRS OR NOT FLTK_LIBRARIES)
        # Fallback: CMake's built-in FindFLTK module (FLTK 1.3.x < 1.4 installs)
        find_package(FLTK REQUIRED COMPONENTS images)
    endif()
endif()

# Include directory reported by CMake's legacy FindFLTK module; FLTK_INCLUDE_DIRS
# stays empty when FLTK is found through that module.
list(APPEND FLTK_INCLUDE_DIRS ${FLTK_INCLUDE_DIR})
list(REMOVE_DUPLICATES FLTK_INCLUDE_DIRS)

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
#
# main.cpp is kept out of this list on purpose: it is the app's entry point, and
# the tests supply their own. Everything else is a library both link, so a test
# exercises the same objects the shipping binary is built from rather than a
# second copy that can drift.
set(SOURCES
    src/TorrentSession.cpp
    src/TorrentItem.cpp
    src/TorrentManager.cpp
    src/SettingsManager.cpp
    src/AssetLoader.cpp
    src/ToolbarLayout.cpp
    src/MainWindow.cpp
    src/TorrentListWidget.cpp
    src/PreferencesDialog.cpp
    src/AddTorrentDialog.cpp
    src/CreateTorrentDialog.cpp
    src/TorrentDetailsDialog.cpp
    src/Resources.cpp
    src/PathUtils.cpp
    src/RemoveConfirmDialog.cpp
    src/SystemUtils.cpp
    src/HttpClient.cpp
    src/SearchEngine.cpp
    src/SearchManager.cpp
    src/SearchResultsWidget.cpp
    src/TorrentFileList.cpp
    src/FileListService.cpp
)

if(WIN32)
    list(APPEND SOURCES src/resources.rc)
endif()

set(APP_SOURCES src/main.cpp)

set(HEADERS
    src/TorrentSession.h
    src/TorrentItem.h
    src/TorrentManager.h
    src/SettingsManager.h
    src/AssetLoader.h
    src/ToolbarLayout.h
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
    src/SystemUtils.h
    src/HttpClient.h
    src/SearchEngine.h
    src/SearchManager.h
    src/SearchResultsWidget.h
    src/TorrentFileList.h
    src/FileListService.h
)

# The shared body of the app: everything except main(). An OBJECT library, so
# the executable and every test compile each translation unit once and link the
# same code, and a change to a source file is one rebuild rather than N.
add_library(ftorrent_core OBJECT ${SOURCES} ${HEADERS})

target_include_directories(ftorrent_core PUBLIC
    ${FLTK_INCLUDE_DIRS}
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(ftorrent_core PUBLIC
    ${FLTK_LIBRARIES}
    PNG::PNG
    JPEG::JPEG
    ZLIB::ZLIB
    LibtorrentRasterbar::torrent-rasterbar
)

# Add executable
add_executable(FTorrent ${APP_SOURCES})
target_link_libraries(FTorrent PRIVATE ftorrent_core)

# Platform-specific settings
if(WIN32)
    target_link_libraries(ftorrent_core PUBLIC ws2_32 wsock32 comctl32 wininet)
    # Use standard windowed app (no console)
    set_target_properties(FTorrent PROPERTIES WIN32_EXECUTABLE ON)
    # Define Windows version
    target_compile_definitions(ftorrent_core PUBLIC _WIN32_WINNT=0x0A00)
else()
    # On Linux, we might need pthread
    find_package(Threads REQUIRED)
    target_link_libraries(ftorrent_core PUBLIC Threads::Threads)

    # HTTP client for the torrent search (Windows uses WinInet instead)
    find_package(CURL REQUIRED)
    target_link_libraries(ftorrent_core PUBLIC CURL::libcurl)
endif()
# --- Tests ---
#
# Off by default: a normal build should be a plain application build, and the
# tests link the whole libtorrent stack, which is not something a packager needs
# just to produce a binary.
#
#   cmake -S . -B build -DFTORRENT_BUILD_TESTS=ON
#   cmake --build build
#   ctest --test-dir build --output-on-failure
#
# scripts/linux/tests/run-tests.sh does the same in one step, and finds a
# display for the suites when the machine has none.
#
# They run headless and need no display, but on a Wayland-only session FLTK will
# still try to reach a compositor, so FTORRENT_TEST_ENV carries the same overrides
# the probes were run with. `flows` needs the network for its search and torrent
# steps and is labelled accordingly, so it can be excluded where there is none.
option(FTORRENT_BUILD_TESTS "Build the FTorrent test programs" OFF)

if(FTORRENT_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

# Installation Rules
include(GNUInstallDirs)

# On Windows the runtime DLLs are resolved by CMake instead of a hand-written list
# of names.
#
# A whitelist of patterns cannot express this dependency graph. Every one of these
# changes the DLLs the executable needs without touching a single line of our code:
#
#   * libtorrent 2.1 enables WebTorrent by default, and vcpkg's port keeps that
#     default, so torrent-rasterbar.dll now links libdatachannel and demands
#     datachannel.dll at load time. The Windows installer never shipped it, so the
#     application died before main() with "datachannel.dll not found".
#   * zlib 1.3.2 renamed the Windows DLL from zlib1.dll to z.dll (z.lib is the new
#     import library; vcpkg's own ZLIB wrapper searches for "z"). The pattern
#     "zlib*.dll" matched nothing, and because the install was OPTIONAL the miss
#     was silent: no warning, no failure, just a broken package.
#
# RUNTIME_DEPENDENCIES walks the real import graph of FTorrent (including the
# transitively loaded torrent-rasterbar.dll, PNG, JPEG, OpenSSL and the rest), so
# it stays correct whatever the dependencies are called in the next release. The
# version floor matters: RUNTIME_DEPENDENCIES needs CMake 3.21, and on an older
# CMake the keyword is not an error but "RUNTIME" plus stray arguments, which
# would install the binary and silently drop every DLL. Hence the hard failure.
if(WIN32 AND CMAKE_VERSION VERSION_LESS 3.21)
    message(FATAL_ERROR
        "Packaging the Windows runtime DLLs requires CMake 3.21 or newer "
        "(install(TARGETS ... RUNTIME_DEPENDENCIES)). Found ${CMAKE_VERSION}. "
        "Without it the installer ships FTorrent.exe alone and every dependency "
        "is missing at startup.")
endif()

# The argument order is part of the syntax, not a style choice: in
# install(TARGETS) the RUNTIME_DEPENDENCIES clause has to appear before the
# artifact options. Written after "RUNTIME DESTINATION" CMake rejects it with
# install TARGETS given unknown argument "RUNTIME_DEPENDENCIES", which is how a
# build fails at configure time rather than shipping a package with no DLLs.
set(FTORRENT_INSTALL_ARGS
    TARGETS FTorrent
)

if(WIN32)
    # The API set stubs (api-ms-*, ext-ms-*) are virtual: they live in the OS and
    # are resolved by the loader, so copying them would be wrong. Anything pulled
    # out of System32 is part of Windows and must not be redistributed either.
    #
    # RUNTIME_DEPENDENCIES cannot be used while cross-compiling, because CMake
    # would have to run the target platform's dumpbin to read the import tables.
    if(CMAKE_CROSSCOMPILING)
        message(WARNING
            "Cross-compiling: skipping RUNTIME_DEPENDENCIES, so the packaged "
            "installer will not contain the dependency DLLs.")
    else()
        list(APPEND FTORRENT_INSTALL_ARGS
            RUNTIME_DEPENDENCIES
                PRE_EXCLUDE_REGEXES  "api-ms-" "ext-ms-"
                POST_EXCLUDE_REGEXES ".*[Ss]ystem32.*"
        )
    endif()
endif()

list(APPEND FTORRENT_INSTALL_ARGS
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
)

install(${FTORRENT_INSTALL_ARGS})

if(WIN32)
    # Reported for diagnosing a package: the DLLs themselves are collected by the
    # install rule above, from wherever they were actually built.
    if(NOT VCPKG_INSTALLED_DIR)
        if(DEFINED ENV{VCPKG_INSTALLATION_ROOT})
            set(VCPKG_INSTALLED_DIR "$ENV{VCPKG_INSTALLATION_ROOT}/installed")
        elseif(DEFINED ENV{VCPKG_ROOT})
            set(VCPKG_INSTALLED_DIR "$ENV{VCPKG_ROOT}/installed")
        endif()
    endif()

    if(VCPKG_INSTALLED_DIR AND VCPKG_TARGET_TRIPLET)
        message(STATUS "VCPKG runtime DLLs resolved from ${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/bin")
    else()
        message(WARNING "VCPKG_INSTALLED_DIR or VCPKG_TARGET_TRIPLET not set. Cannot report the vcpkg DLL directory.")
    endif()
endif()


# Install assets and config (Common)
install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/src/assets/"
    DESTINATION "${CMAKE_INSTALL_BINDIR}/assets"
)
install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/trackersadd.txt"
    DESTINATION "${CMAKE_INSTALL_BINDIR}"
)

# --- CPACK CONFIGURATION ---
set(CPACK_PACKAGE_NAME "FTorrent")
set(CPACK_PACKAGE_VENDOR "FTorrent Team")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "A lightweight and fast BitTorrent client")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_INSTALL_DIRECTORY "FTorrent")

# Windows NSIS Installer
if(WIN32)
    set(CPACK_GENERATOR "NSIS;ZIP")
    set(CPACK_NSIS_MUI_ICON "${CMAKE_CURRENT_SOURCE_DIR}/src/assets/ftorrent.ico") 

    set(CPACK_NSIS_HELP_LINK "https://github.com/FTorrent/FTorrent")
    set(CPACK_NSIS_URL_INFO_ABOUT "https://ftorrent.org")
    set(CPACK_NSIS_CONTACT "contact@ftorrent.org")
    set(CPACK_NSIS_CREATE_ICONS_EXTRA "CreateShortCut '$SMPROGRAMS\\\\$STARTMENU_FOLDER\\\\FTorrent.lnk' '$INSTDIR\\\\bin\\\\FTorrent.exe'")
    
    # This sets the icon in 'Add/Remove Programs' (Installed Apps)
    set(CPACK_NSIS_INSTALLED_ICON_NAME "bin\\\\FTorrent.exe")
endif()

# Linux Packages
if(UNIX AND NOT APPLE)
    # Desktop integration
    install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/io.github.thedevil4k.FTorrent.desktop"
        DESTINATION "${CMAKE_INSTALL_DATADIR}/applications"
        RENAME "io.github.thedevil4k.FTorrent.desktop"
    )

    # Appstream metadata: software centers read it on the desktop, and
    # Flathub rejects submissions that ship without it.
    install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/io.github.thedevil4k.FTorrent.appdata.xml"
        DESTINATION "${CMAKE_INSTALL_DATADIR}/metainfo"
        RENAME "io.github.thedevil4k.FTorrent.appdata.xml"
    )
    
    # Icons (various sizes if available, but for now we use the one we have)
    install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/src/assets/ftorrent.png"
        DESTINATION "${CMAKE_INSTALL_DATADIR}/icons/hicolor/128x128/apps"
        RENAME "ftorrent.png"
    )

    set(CPACK_GENERATOR "DEB;RPM;TGZ")
    
    # Debian specific
    set(CPACK_DEBIAN_PACKAGE_MAINTAINER "FTorrent Maintainers <dev@ftorrent.org>")
    set(CPACK_DEBIAN_PACKAGE_SECTION "net")
    set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)
    set(CPACK_DEBIAN_PACKAGE_DEPENDS "libfltk1.3, libtorrent-rasterbar-dev") # Fallback dependencies
    set(CPACK_DEBIAN_FILE_NAME DEB-DEFAULT)

    # RPM specific
    set(CPACK_RPM_PACKAGE_LICENSE "MIT")
    set(CPACK_RPM_PACKAGE_GROUP "Applications/Internet")
    set(CPACK_RPM_PACKAGE_AUTOREQPROV ON)
    set(CPACK_RPM_FILE_NAME RPM-DEFAULT)
endif()

# macOS Disk Image
if(APPLE)
    set(CPACK_GENERATOR "DragNDrop")
endif()

include(CPack)

