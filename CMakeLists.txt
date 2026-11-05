cmake_minimum_required(VERSION 3.15)
project(FTorrent VERSION 0.6.0 LANGUAGES CXX)

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

# libarchive is optional. It backs the internal read-only RAR/ZIP engine used by
# the pre-download analysis: entries are streamed through SHA-256 in memory and
# are never extracted or executed. Without it the application still builds and
# the engine reports that the reader is unavailable; it never falls back to
# running an external unrar/7z tool.
find_package(LibArchive QUIET)
if(NOT CMAKE_DISABLE_FIND_PACKAGE_LibArchive
   AND NOT TARGET LibArchive::LibArchive
   AND NOT LibArchive_FOUND
   AND NOT LIBARCHIVE_FOUND)
    # FindLibArchive was added in CMake 3.17, while this project supports 3.15.
    # Fall back to the pkg-config file so the optional reader is still detected
    # on an older host instead of being silently disabled.
    find_package(PkgConfig QUIET)
    if(PkgConfig_FOUND OR PKG_CONFIG_FOUND)
        pkg_check_modules(LIBARCHIVE QUIET IMPORTED_TARGET libarchive)
    endif()
endif()
if(NOT CMAKE_DISABLE_FIND_PACKAGE_LibArchive
   AND (TARGET LibArchive::LibArchive
        OR TARGET PkgConfig::LIBARCHIVE
        OR LibArchive_FOUND
        OR LIBARCHIVE_FOUND))
    set(FTORRENT_HAVE_LIBARCHIVE TRUE)
endif()

if(FTORRENT_HAVE_LIBARCHIVE)
    message(STATUS "libarchive found: internal RAR/ZIP engine enabled")
else()
    message(STATUS "libarchive not found: internal RAR/ZIP engine disabled")
endif()

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
    src/MainWindowSearch.cpp
    src/MainWindowTheme.cpp
    src/MainWindowActions.cpp
    src/MainWindowTray.cpp
    src/TorrentListWidget.cpp
    src/PreferencesDialog.cpp
    src/AddTorrentDialog.cpp
    src/CreateTorrentDialog.cpp
    src/TorrentDetailsDialog.cpp
    src/ArchiveInspector.cpp
    src/FileHash.cpp
    src/VirusTotal.cpp
    src/Resources.cpp
    src/PathUtils.cpp
    src/RemoveConfirmDialog.cpp
    src/SystemUtils.cpp
    src/HttpClient.cpp
    src/SearchEngine.cpp
    src/SearchEngineParse.cpp
    src/SearchEngineCatalogue.cpp
    src/SearchManager.cpp
    src/SearchResultsWidget.cpp
    src/SearchResultsWidgetBadges.cpp
    src/SearchResultsWidgetArchive.cpp
    src/SearchResultsWidgetFiles.cpp
    src/TorrentFileList.cpp
    src/FileListService.cpp
    src/SecurityAnalyzer.cpp
    src/SecurityBadge.cpp
    src/SecurityKeys.cpp
    src/Reputation.cpp
    src/ReputationService.cpp
    src/SecurityHashes.cpp
    src/SecurityReputation.cpp
    src/ArchivePreflight.cpp
    src/ArchiveProbeSession.cpp
    src/ArchiveCheckTask.cpp
    src/SecurityEvents.cpp
    src/NetworkMonitorDialog.cpp
    src/SearchScope.cpp
    src/Format.cpp
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
    src/ArchiveInspector.h
    src/FileHash.h
    src/VirusTotal.h
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
    src/SecurityTypes.h
    src/SecurityAnalyzer.h
    src/SecurityBadge.h
    src/SecurityKeys.h
    src/Reputation.h
    src/ReputationService.h
    src/SecurityHashes.h
    src/SecurityReputation.h
    src/ArchivePreflight.h
    src/ArchiveProbeSession.h
    src/ArchiveCheckTask.h
    src/SecurityEvents.h
    src/NetworkMonitorDialog.h
    src/SearchScope.h
    src/Format.h
    src/Version.h
)

# The window logo is compiled into the binary: the window must carry its icon
# even when the assets folder is not next to the executable. The PNG itself is
# the source of truth, and this turns it into the C array that Resources.cpp
# hands to FLTK. Generating the header keeps a 40 KB blob out of the source
# tree, where it used to be 3 300 lines of hex nobody could read.
set(FTORRENT_LOGO_PNG "${CMAKE_CURRENT_SOURCE_DIR}/src/assets/window-logo.png")
file(SIZE "${FTORRENT_LOGO_PNG}" ftorrent_logo_size)
file(READ "${FTORRENT_LOGO_PNG}" ftorrent_logo_hex HEX)
string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1, " ftorrent_logo_bytes
    "${ftorrent_logo_hex}")
# Sixteen bytes per line: a source line has to stay inside what the compiler
# accepts, and the file is generated anyway.
set(ftorrent_logo_group "")
foreach(i RANGE 1 16)
    string(APPEND ftorrent_logo_group "0x.., ")
endforeach()
string(REGEX REPLACE "(${ftorrent_logo_group})" "\\1\n" ftorrent_logo_body
    "${ftorrent_logo_bytes}")
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/generated/LogoIcon.h"
"// Generated from src/assets/window-logo.png. Do not edit.\n"
"#ifndef FTORRENT_LOGO_PNG_H\n"
"#define FTORRENT_LOGO_PNG_H\n"
"\n"
"static const unsigned char ftorrent_logo_png[] = {\n${ftorrent_logo_body}};\n"
"\n"
"static const int ftorrent_logo_png_size = ${ftorrent_logo_size};\n"
"\n"
"#endif\n")
# A change to the PNG has to reach the build.
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    "${FTORRENT_LOGO_PNG}")

# --- Warnings ---
#
# The compiler is asked for its warnings on every build, so a warning is
# visible while the code is being written instead of years later.
# FTORRENT_WARNINGS_AS_ERRORS turns them into failures, which is what the
# quality job in CI uses and what a developer can turn on locally:
#
#   cmake -S . -B build -DFTORRENT_WARNINGS_AS_ERRORS=ON
#
# The flags are the ones each compiler understands. GCC and Clang get
# -Wall -Wextra; MSVC gets /W4, its strictest level. The tree is warning-free
# under -Wall -Wextra, which is the level this project can actually check on
# every build.
option(FTORRENT_WARNINGS_AS_ERRORS
    "Treat compiler warnings as errors" OFF)

if(MSVC)
    set(FTORRENT_WARNING_FLAGS /W4)
    if(FTORRENT_WARNINGS_AS_ERRORS)
        list(APPEND FTORRENT_WARNING_FLAGS /WX)
    endif()
else()
    set(FTORRENT_WARNING_FLAGS -Wall -Wextra)
    if(FTORRENT_WARNINGS_AS_ERRORS)
        list(APPEND FTORRENT_WARNING_FLAGS -Werror)
    endif()
endif()

# The shared body of the app: everything except main(). An OBJECT library, so
# the executable and every test compile each translation unit once and link the
# same code, and a change to a source file is one rebuild rather than N.
add_library(ftorrent_core OBJECT ${SOURCES} ${HEADERS})

# PUBLIC, so the application and every test program are held to the same
# standard as the code they share: a warning in a test is as much a defect as
# a warning in the app.
target_compile_options(ftorrent_core PUBLIC ${FTORRENT_WARNING_FLAGS})

# One version, from project(FTorrent VERSION ...) above. src/Version.h turns it
# into the user agents the app reports, so a release bumps one number here.
target_compile_definitions(ftorrent_core PUBLIC
    APP_VERSION_STRING="${PROJECT_VERSION}")

target_include_directories(ftorrent_core PUBLIC
    ${FLTK_INCLUDE_DIRS}
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_BINARY_DIR}/generated
)

target_link_libraries(ftorrent_core PUBLIC
    ${FLTK_LIBRARIES}
    PNG::PNG
    JPEG::JPEG
    ZLIB::ZLIB
    LibtorrentRasterbar::torrent-rasterbar
)

# LibArchive::LibArchive exists from CMake 3.17. The project's floor is 3.15, so
# keep the plain variable fallback for an older host toolchain.
if(FTORRENT_HAVE_LIBARCHIVE)
    target_compile_definitions(ftorrent_core PUBLIC FTORRENT_HAVE_LIBARCHIVE)
    if(TARGET LibArchive::LibArchive)
        target_link_libraries(ftorrent_core PUBLIC LibArchive::LibArchive)
    elseif(TARGET PkgConfig::LIBARCHIVE)
        target_link_libraries(ftorrent_core PUBLIC PkgConfig::LIBARCHIVE)
    else()
        set(FTORRENT_LIBARCHIVE_INCLUDE_DIRS
            ${LibArchive_INCLUDE_DIRS} ${LIBARCHIVE_INCLUDE_DIRS})
        set(FTORRENT_LIBARCHIVE_LIBRARIES
            ${LibArchive_LIBRARIES} ${LIBARCHIVE_LIBRARIES})
        if(FTORRENT_LIBARCHIVE_INCLUDE_DIRS)
            target_include_directories(ftorrent_core PUBLIC
                ${FTORRENT_LIBARCHIVE_INCLUDE_DIRS})
        endif()
        target_link_libraries(ftorrent_core PUBLIC
            ${FTORRENT_LIBARCHIVE_LIBRARIES})
    endif()
endif()

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
    install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/io.github.thedevil4k.FTorrent.metainfo.xml"
        DESTINATION "${CMAKE_INSTALL_DATADIR}/metainfo"
        RENAME "io.github.thedevil4k.FTorrent.metainfo.xml"
    )

    # Icons (various sizes if available, but for now we use the one we have).
    # Installed under the app id: the .desktop file's Icon= key has to name a
    # file that actually ships, which is one of Flathub's checks.
    install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/src/assets/ftorrent.png"
        DESTINATION "${CMAKE_INSTALL_DATADIR}/icons/hicolor/256x256/apps"
        RENAME "io.github.thedevil4k.FTorrent.png"
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

