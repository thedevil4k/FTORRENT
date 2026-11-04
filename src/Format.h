#ifndef FTORRENT_FORMAT_H
#define FTORRENT_FORMAT_H

#include <cstdint>
#include <string>

/**
 * @brief How the interface prints a size, a rate or a duration.
 *
 * Every surface that shows one of these uses this module, so the torrent list,
 * the details window, a search row and an archive entry read the same way. It
 * has no FLTK dependency and is covered by its own test.
 */
namespace Format {

// "1.50 MB" with binary units (1024) and two decimals: "0.00 B", "512.00 KB".
std::string bytes(std::int64_t value);

// "1.50 MB/s"; zero or a negative rate prints "0 B/s" with no decimals, which
// is what an idle torrent should read as.
std::string perSecond(std::int64_t value);

// "1h 2m 3s", "2m 3s", "3s". A negative value is an unknown ETA and prints the
// infinity sign, as the torrent list has always shown it.
std::string duration(int seconds);

} // namespace Format

#endif // FTORRENT_FORMAT_H
