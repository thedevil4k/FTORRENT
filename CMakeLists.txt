cmake_minimum_required(VERSION 3.15)
project(FTorrent VERSION 0.4.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(WIN32)
    add_compile_definitions(NOMINMAX)
    # Automatically copy dependency DLLs to the install folder for CPack
    set(X_VCPKG_APPLOCAL_DEPS_INSTALL ON)
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
    src/SystemUtils.cpp
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
    src/SystemUtils.h
)

# Add executable
add_executable(FTorrent ${SOURCES} ${HEADERS})

# Include directories
target_include_directories(FTorrent PRIVATE 
    ${FLTK_INCLUDE_DIRS}
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

# Link libraries
target_link_libraries(FTorrent PRIVATE 
    ${FLTK_LIBRARIES}
    PNG::PNG
    JPEG::JPEG
    ZLIB::ZLIB
    LibtorrentRasterbar::torrent-rasterbar
)

# Platform-specific settings
if(WIN32)
    target_link_libraries(FTorrent PRIVATE ws2_32 wsock32 comctl32)
    # Use standard windowed app (no console)
    set_target_properties(FTorrent PROPERTIES WIN32_EXECUTABLE ON)
    # Define Windows version
    target_compile_definitions(FTorrent PRIVATE _WIN32_WINNT=0x0A00)
else()
    # On Linux, we might need pthread
    find_package(Threads REQUIRED)
    target_link_libraries(FTorrent PRIVATE Threads::Threads)
endif()
# Installation Rules
include(GNUInstallDirs)
install(TARGETS FTorrent
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
)

if(WIN32)
    # Force inclusion of critical DLLs in the installer package
    # This works as a fallback if the automatic vcpkg bundling misses them
    set(DLL_LIST 
        "torrent-rasterbar.dll"
        "libcrypto-3-x64.dll"
        "libssl-3-x64.dll"
        "libpng16.dll"
        "jpeg62.dll"
        "zlib1.dll"
    )
    
    foreach(dll ${DLL_LIST})
        install(FILES "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/bin/${dll}"
            DESTINATION ${CMAKE_INSTALL_BINDIR}
            OPTIONAL
        )
    endforeach()


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

