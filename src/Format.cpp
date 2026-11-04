#include "Format.h"

#include <iomanip>
#include <sstream>

namespace Format {

std::string bytes(std::int64_t value) {
    const char* units[] = {"B", "KB", "MB", "GB", "TB"};
    int unit = 0;
    double size = static_cast<double>(value);

    while (size >= 1024 && unit < 4) {
        size /= 1024;
        unit++;
    }

    std::ostringstream out;
    out << std::fixed << std::setprecision(2) << size << " " << units[unit];
    return out.str();
}

std::string perSecond(std::int64_t value) {
    if (value <= 0) return "0 B/s";
    return bytes(value) + "/s";
}

std::string duration(int seconds) {
    if (seconds < 0) return "\u221e";

    const int hours = seconds / 3600;
    const int minutes = (seconds % 3600) / 60;
    const int rest = seconds % 60;

    std::ostringstream out;
    if (hours > 0) {
        out << hours << "h " << minutes << "m " << rest << "s";
    } else if (minutes > 0) {
        out << minutes << "m " << rest << "s";
    } else {
        out << rest << "s";
    }
    return out.str();
}

} // namespace Format
