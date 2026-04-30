# This file will be configured to contain variables for CPack. These variables
# should be set in the CMake list file of the project before CPack module is
# included. The list of available CPACK_xxx variables and their associated
# documentation may be obtained using
#  cpack --help-variable-list
#
# Some variables are common to all generators (e.g. CPACK_PACKAGE_NAME)
# and some are specific to a generator
# (e.g. CPACK_NSIS_EXTRA_INSTALL_COMMANDS). The generator specific variables
# usually begin with CPACK_<GENNAME>_xxxx.


set(CPACK_BUILD_SOURCE_DIRS "D:/FTORRENT-client;D:/FTORRENT-client/build_test")
set(CPACK_CMAKE_GENERATOR "Visual Studio 17 2022")
set(CPACK_COMPONENT_UNSPECIFIED_HIDDEN "TRUE")
set(CPACK_COMPONENT_UNSPECIFIED_REQUIRED "TRUE")
set(CPACK_DEFAULT_PACKAGE_DESCRIPTION_FILE "C:/Program Files/CMake/share/cmake-4.2/Templates/CPack.GenericDescription.txt")
set(CPACK_DEFAULT_PACKAGE_DESCRIPTION_SUMMARY "FTorrent built using CMake")
set(CPACK_GENERATOR "NSIS;ZIP")
set(CPACK_INNOSETUP_ARCHITECTURE "x64")
set(CPACK_INSTALL_CMAKE_PROJECTS "D:/FTORRENT-client/build_test;FTorrent;ALL;/")
set(CPACK_INSTALL_PREFIX "C:/Program Files (x86)/FTorrent")
set(CPACK_MODULE_PATH "")
set(CPACK_NSIS_CONTACT "contact@ftorrent.org")
set(CPACK_NSIS_CREATE_ICONS_EXTRA "CreateShortCut '$SMPROGRAMS\\$STARTMENU_FOLDER\\FTorrent.lnk' '$INSTDIR\\bin\\FTorrent.exe'")
set(CPACK_NSIS_DISPLAY_NAME "FTorrent")
set(CPACK_NSIS_HELP_LINK "https://github.com/FTorrent/FTorrent")
set(CPACK_NSIS_INSTALLED_ICON_NAME "bin\\FTorrent.exe")
set(CPACK_NSIS_INSTALLER_ICON_CODE "")
set(CPACK_NSIS_INSTALLER_MUI_ICON_CODE "")
set(CPACK_NSIS_INSTALL_ROOT "$PROGRAMFILES64")
set(CPACK_NSIS_MUI_ICON "D:/FTORRENT-client/src/assets/ftorrent.ico")
set(CPACK_NSIS_PACKAGE_NAME "FTorrent")
set(CPACK_NSIS_UNINSTALL_NAME "Uninstall")
set(CPACK_NSIS_URL_INFO_ABOUT "https://ftorrent.org")
set(CPACK_OUTPUT_CONFIG_FILE "D:/FTORRENT-client/build_test/CPackConfig.cmake")
set(CPACK_PACKAGE_DEFAULT_LOCATION "/")
set(CPACK_PACKAGE_DESCRIPTION_FILE "C:/Program Files/CMake/share/cmake-4.2/Templates/CPack.GenericDescription.txt")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "A lightweight and fast BitTorrent client")
set(CPACK_PACKAGE_FILE_NAME "FTorrent-0.4.1-win64")
set(CPACK_PACKAGE_INSTALL_DIRECTORY "FTorrent")
set(CPACK_PACKAGE_INSTALL_REGISTRY_KEY "FTorrent")
set(CPACK_PACKAGE_NAME "FTorrent")
set(CPACK_PACKAGE_RELOCATABLE "true")
set(CPACK_PACKAGE_VENDOR "FTorrent Team")
set(CPACK_PACKAGE_VERSION "0.4.1")
set(CPACK_PACKAGE_VERSION_MAJOR "0")
set(CPACK_PACKAGE_VERSION_MINOR "4")
set(CPACK_PACKAGE_VERSION_PATCH "1")
set(CPACK_RESOURCE_FILE_LICENSE "C:/Program Files/CMake/share/cmake-4.2/Templates/CPack.GenericLicense.txt")
set(CPACK_RESOURCE_FILE_README "C:/Program Files/CMake/share/cmake-4.2/Templates/CPack.GenericDescription.txt")
set(CPACK_RESOURCE_FILE_WELCOME "C:/Program Files/CMake/share/cmake-4.2/Templates/CPack.GenericWelcome.txt")
set(CPACK_SET_DESTDIR "OFF")
set(CPACK_SOURCE_7Z "ON")
set(CPACK_SOURCE_GENERATOR "7Z;ZIP")
set(CPACK_SOURCE_OUTPUT_CONFIG_FILE "D:/FTORRENT-client/build_test/CPackSourceConfig.cmake")
set(CPACK_SOURCE_ZIP "ON")
set(CPACK_SYSTEM_NAME "win64")
set(CPACK_THREADS "1")
set(CPACK_TOPLEVEL_TAG "win64")
set(CPACK_WIX_SIZEOF_VOID_P "8")

if(NOT CPACK_PROPERTIES_FILE)
  set(CPACK_PROPERTIES_FILE "D:/FTORRENT-client/build_test/CPackProperties.cmake")
endif()

if(EXISTS ${CPACK_PROPERTIES_FILE})
  include(${CPACK_PROPERTIES_FILE})
endif()
