#ifndef FTORRENT_ASSETLOADER_H
#define FTORRENT_ASSETLOADER_H

#include <FL/Fl_Image.H>
#include <string>

// File-based assets: the PNGs installed next to the executable under assets/.
//
// This is deliberately not Resources. Resources owns what is compiled INTO the
// binary (the XPM set and the logo PNG); this owns what is read from DISK at
// runtime. Two different stores, so two different owners.
//
// Every icon in the app goes through here, which is what keeps the failure
// mode in one place: a missing asset yields nullptr and the caller decides
// what to do, instead of every call site inventing its own guard.
namespace AssetLoader {

// Directory the assets are read from, always with forward slashes and always
// with a trailing one, so callers can concatenate a file name onto it.
// Resolved once from the running executable's own path.
const std::string& dir();

// Decodes assets/<file> and returns a size x size copy the caller owns.
// Returns nullptr when the file is missing or unreadable.
//
// The guard is on the decoded size rather than on d(): FLTK reports d()==3 for
// a file that is not there, so a `d() == 0` guard never fires for a missing
// asset and copy() then hands back a blank square image that gets drawn as if
// it were the icon.
Fl_Image* load(const std::string& file, int size);

}  // namespace AssetLoader

#endif  // FTORRENT_ASSETLOADER_H