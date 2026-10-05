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

}  // namespace AssetLoader