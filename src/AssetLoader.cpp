#include "AssetLoader.h"

#include <FL/Fl_PNG_Image.H>
#include <algorithm>

#include "PathUtils.h"

namespace AssetLoader {

const std::string& dir() {
    static const std::string path = [] {
        std::string resolved = PathUtils::getAppDirPath() + "/assets/";
        // FLTK's image loader only understands forward slashes.
        std::replace(resolved.begin(), resolved.end(), '\\', '/');
        return resolved;
    }();
    return path;
}

Fl_Image* load(const std::string& file, int size) {
    Fl_PNG_Image image((dir() + file).c_str());
    if (image.w() <= 0 || image.h() <= 0) return nullptr;
    return image.copy(size, size);
}

Fl_Image* loadFit(const std::string& file, int width, int height) {
    Fl_PNG_Image image((dir() + file).c_str());
    if (image.w() <= 0 || image.h() <= 0) return nullptr;
    const double scale =
        std::min(static_cast<double>(width) / image.w(),
                 static_cast<double>(height) / image.h());
    const int w = std::max(1, static_cast<int>(image.w() * scale + 0.5));
    const int h = std::max(1, static_cast<int>(image.h() * scale + 0.5));
    // copy() never returns an image larger than the one it came from, so a box
    // bigger than the file just yields the file at its own size.
    return image.copy(w, h);
}

}  // namespace AssetLoader