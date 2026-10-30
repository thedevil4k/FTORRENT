#ifndef ICONS_H
#define ICONS_H

/**
 * @brief Embedded icons for FLTorrent
 * 
 * This file contains XPM format icons that can be embedded directly
 * into the application without requiring external image files.
 */

// FLTorrent logo (32x32)
// inline constexpr, not static: a `static` array in a header is a private copy
// in every translation unit that includes it, so every .cpp that reached
// Resources.h duplicated all of these glyphs and warned about the copies it did
// not use. An inline variable is one object shared by the whole program.
inline constexpr const char* fltorrent_logo_xpm[] = {
"32 32 6 1",
" 	c None",
".	c #2196F3",
"+	c #00BCD4",
"@	c #1976D2",
"#	c #FFFFFF",
"$	c #0D47A1",
"                                ",
"                                ",
"         .......                ",
"       ..+++++++..              ",
"      .++++++++++.              ",
"     .++++++++++++.             ",
"     .++++@@@@@+++.             ",
"    .++++@@@@@@@+++.            ",
"    .+++@@@@$$$@@++.            ",
"    .+++@@@$$$$@@@+.            ",
"    .+++@@$$$$$$@@+.            ",
"    .++@@$$$$$$$$@+.            ",
"    .++@$$$$$$$$@@+.            ",
"     .+@$$$$$$$$@+.             ",
"     .+@$$$$$$$$@+.             ",
"     .+@$$$####$@+.             ",
"     .+@$$####$$@+.             ",
"     .++@$$##$$@@+.             ",
"      .+@@$$$$@@+.              ",
"      .++@@@@@@++.              ",
"       .+++++++++.              ",
"        .+++++++.               ",
"         .......                ",
"                                ",
"                                ",
"                                ",
"                                ",
"                                ",
"                                ",
"                                ",
"                                ",
"                                "
};

// Add button icon (16x16)
inline constexpr const char* icon_add_xpm[] = {
"16 16 3 1",
" 	c None",
".	c #2196F3",
"+	c #FFFFFF",
"                ",
"                ",
"      ....      ",
"     ......     ",
"    ..++++..    ",
"   ..++++++..   ",
"   ..++++++..   ",
"   ..++++++..   ",
"   ..++++++..   ",
"   ..++++++..   ",
"    ..++++..    ",
"     ......     ",
"      ....      ",
"                ",
"                ",
"                "
};

// Pause button icon (16x16)
inline constexpr const char* icon_pause_xpm[] = {
"16 16 3 1",
" 	c None",
".	c #FF9800",
"+	c #FFFFFF",
"                ",
"                ",
"   ....  ....   ",
"   ....  ....   ",
"   ....  ....   ",
"   ....  ....   ",
"   ....  ....   ",
"   ....  ....   ",
"   ....  ....   ",
"   ....  ....   ",
"   ....  ....   ",
"   ....  ....   ",
"   ....  ....   ",
"                ",
"                ",
"                "
};

// Play/Resume button icon (16x16)
inline constexpr const char* icon_play_xpm[] = {
"16 16 3 1",
" 	c None",
".	c #4CAF50",
"+	c #FFFFFF",
"                ",
"                ",
"    ..          ",
"    ....        ",
"    ......      ",
"    ........    ",
"    ..........  ",
"    ........... ",
"    ........... ",
"    ..........  ",
"    ........    ",
"    ......      ",
"    ....        ",
"    ..          ",
"                ",
"                "
};

// Remove button icon (16x16)
inline constexpr const char* icon_remove_xpm[] = {
"16 16 3 1",
" 	c None",
".	c #F44336",
"+	c #FFFFFF",
"                ",
"                ",
"   ..      ..   ",
"    ..    ..    ",
"     ..  ..     ",
"      ....      ",
"       ..       ",
"      ....      ",
"     ..  ..     ",
"    ..    ..    ",
"   ..      ..   ",
"                ",
"                ",
"                ",
"                ",
"                "
};

// Settings button icon (16x16)
inline constexpr const char* icon_settings_xpm[] = {
"16 16 3 1",
" 	c None",
".	c #757575",
"+	c #FFFFFF",
"                ",
"      ....      ",
"     ......     ",
"    ..++++..    ",
"   ...++++...   ",
"  ....++++....  ",
" .....++++..... ",
" .....++++..... ",
" .....++++..... ",
" .....++++..... ",
"  ....++++....  ",
"   ...++++...   ",
"    ..++++..    ",
"     ......     ",
"      ....      ",
"                "
};

// Download icon (16x16)
inline constexpr const char* icon_download_xpm[] = {
"16 16 3 1",
" 	c None",
".	c #2196F3",
"+	c #FFFFFF",
"                ",
"       ..       ",
"       ..       ",
"       ..       ",
"       ..       ",
"       ..       ",
"       ..       ",
"   ..  ..  ..   ",
"    .. .. ..    ",
"     ......     ",
"      ....      ",
"       ..       ",
"   ..........   ",
"   ..........   ",
"                ",
"                "
};

// Upload icon (16x16)
inline constexpr const char* icon_upload_xpm[] = {
"16 16 3 1",
" 	c None",
".	c #4CAF50",
"+	c #FFFFFF",
"                ",
"       ..       ",
"      ....      ",
"     ......     ",
"    .. .. ..    ",
"   ..  ..  ..   ",
"       ..       ",
"       ..       ",
"       ..       ",
"       ..       ",
"       ..       ",
"       ..       ",
"   ..........   ",
"   ..........   ",
"                ",
"                "
};

// Magnifier / search icon (16x16)
inline constexpr const char* icon_search_xpm[] = {
"16 16 2 1",
" 	c None",
".	c #2196F3",
"                ",
"      ....      ",
"    ..####..    ",
"   ..##..##..   ",
"  ..##....##..  ",
"  ..##....##..  ",
"  ..##....##..  ",
"   ..##..##..   ",
"    ..####..    ",
"      ..##..    ",
"        .##..  ",
"         .##.. ",
"          .##. ",
"           ..  ",
"                ",
"                "
};

// Network monitor icon (16x16): a globe with a meridian and an equator, so it
// reads as "connections" rather than as another magnifier.
inline constexpr const char* icon_network_xpm[] = {
"16 16 2 1",
" 	c None",
".	c #2196F3",
"                ",
"      ....      ",
"    ..    ..    ",
"   .   ..   .   ",
"  .    ..    .  ",
"  .  ......  .  ",
"  .    ..    .  ",
"  .    ..    .  ",
"  .    ..    .  ",
"  .  ......  .  ",
"  .    ..    .  ",
"   .   ..   .   ",
"    ..    ..    ",
"      ....      ",
"                ",
"                "
};

#endif // ICONS_H
