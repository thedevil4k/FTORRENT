// Format checks.
//
// Every surface that shows a size or a rate now goes through this module, so
// the exact strings are pinned here: a change here is visible in the torrent
// list, the details window, a search row and an archive entry at once.
#include "Format.h"
#include "test_support.h"

#include <string>

int main() {
    using testsupport::ok;

    testsupport::section("bytes");
    ok(Format::bytes(0) == "0.00 B", "zero is 0.00 B");
    ok(Format::bytes(1023) == "1023.00 B", "below a kibibyte it stays in bytes");
    ok(Format::bytes(1024) == "1.00 KB", "a kibibyte switches the unit");
    ok(Format::bytes(1536) == "1.50 KB", "fractions keep two decimals");
    ok(Format::bytes(5LL * 1024 * 1024) == "5.00 MB", "megabytes are binary");
    ok(Format::bytes(1LL * 1024 * 1024 * 1024) == "1.00 GB",
       "gigabytes are binary");
    ok(Format::bytes(1LL * 1024 * 1024 * 1024 * 1024) == "1.00 TB",
       "terabytes are the largest unit");

    testsupport::section("perSecond");
    ok(Format::perSecond(0) == "0 B/s", "an idle rate has no decimals");
    ok(Format::perSecond(-1) == "0 B/s", "a negative rate reads as idle");
    ok(Format::perSecond(1024) == "1.00 KB/s", "a rate reuses the byte format");
    ok(Format::perSecond(5LL * 1024 * 1024) == "5.00 MB/s",
       "a fast rate keeps its unit");

    testsupport::section("duration");
    ok(Format::duration(-1) == "\u221e", "a negative ETA is unknown");
    ok(Format::duration(0) == "0s", "zero seconds is 0s");
    ok(Format::duration(59) == "59s", "seconds only below a minute");
    ok(Format::duration(60) == "1m 0s", "a minute keeps the seconds");
    ok(Format::duration(3661) == "1h 1m 1s", "hours carry minutes and seconds");

    return testsupport::failures == 0 ? 0 : 1;
}
