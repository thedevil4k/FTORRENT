#include "SearchResultsWidget.h"

#include <FL/Fl.H>
#include <FL/fl_draw.H>

#include "Format.h"
#include "SecurityAnalyzer.h"
#include "SecurityBadge.h"

#include <functional>
#include <string>
#include <vector>

// Truncates back to the previous UTF-8 character boundary so an elided name
// never ends in the middle of a multibyte sequence. Pure byte arithmetic,
// no locale or extra dependency, identical on every platform we ship.
size_t utf8Clip(const std::string& text, size_t pos) {
    while (pos > 0 && (static_cast<unsigned char>(text[pos]) & 0xC0) == 0x80)
        --pos;
    return pos;
}

// Ellipsis by pixels, not by characters: with a proportional font 60 chars
// can be far narrower (or wider) than the cell. The caller must have set the
// same fl_font() it will draw with, since fl_width() measures that font.
// Returns the full text when it fits, so a maximised window shows the
// absolute name, and only adds "..." when the cell is really too narrow
// (square window). Uses only fl_width(), available in FLTK 1.3 and 1.4 on
// every backend we build (Windows x86/ARM64, Linux 32/64, Arch, Flatpak).
std::string SearchResultsWidget::elideToWidth(const std::string& text,
                                              int maxPixels) {
    if (text.empty() || maxPixels <= 0)
        return text.empty() ? text : std::string();
    if (fl_width(text.c_str()) <= maxPixels) return text;
    const char* ellipsis = "...";
    const int ellipsisW = static_cast<int>(fl_width(ellipsis));
    if (maxPixels <= ellipsisW) return std::string();
    size_t lo = 0;
    size_t hi = text.size();
    // Binary search on bytes, then snap to a UTF-8 boundary.
    while (lo < hi) {
        size_t mid = (lo + hi + 1) / 2;
        size_t cut = utf8Clip(text, mid);
        // A cut that lands inside a multibyte char maps several mids to the
        // same boundary: force progress so the loop always terminates.
        if (cut <= lo) break;
        std::string cand = text.substr(0, cut) + ellipsis;
        if (fl_width(cand.c_str()) <= maxPixels) lo = cut;
        else hi = cut - 1;
    }
    size_t finalCut = utf8Clip(text, lo);
    if (finalCut == 0) return ellipsis;
    return text.substr(0, finalCut) + ellipsis;
}

std::string SearchResultsWidget::cacheKey(const SearchResult& r) {
    // The infohash is the one identifier two engines agree on, so the same
    // torrent reached from BitSearch and from SolidTorrents is fetched once.
    std::string hash = TorrentFileList::magnetHash(r.magnet);
    if (!hash.empty()) return hash;
    if (!r.torrentUrl.empty()) return "url:" + r.torrentUrl;
    return "row:" + r.name + ":" + std::to_string(r.size);
}

FileListService::Entry SearchResultsWidget::filesForRow(int row) const {
    const SearchResult* r = getResultAt(row);
    return r ? m_files.find(cacheKey(*r)) : FileListService::Entry{};
}

bool SearchResultsWidget::isExpanded(const std::string& key) const {
    auto it = m_expanded.find(key);
    return it != m_expanded.end() && it->second;
}

void SearchResultsWidget::setExpanded(const std::string& key, bool on) {
    if (!on) m_expanded.erase(key);
    else m_expanded[key] = true;
}

bool SearchResultsWidget::isRowExpanded(int row) const {
    const SearchResult* r = getResultAt(row);
    return r && isExpanded(cacheKey(*r));
}

void SearchResultsWidget::forEachRow(
    const std::function<void(int, const SearchResult&)>& fn) {
    for (int row = 0; row < rows(); ++row) {
        const SearchResult* r = getResultAt(row);
        if (r) fn(row, *r);
    }
}

void SearchResultsWidget::requestFiles(int row) {
    const SearchResult* r = getResultAt(row);
    if (!r) return;
    SearchResult copy = *r; // the fetch must not read the table
    std::string key = cacheKey(copy);

    if (m_files.request(key, copy.magnet, copy.torrentUrl,
                        [this, key](const FileListService::Entry&) {
                            filesArrived(key);
                        })) {
        // The row now has a "reading it" line under the name; make room for it.
        updateRowHeight(row);
        redraw();
    }
}

void SearchResultsWidget::filesArrived(const std::string& key) {
    // The list changed, so the cached report for it is stale.
    m_reports.erase(key);

    // A shield clicked before the list was known asked for this: now that the
    // hashes are here, run it.
    auto pending = m_reputationPending.find(key);
    if (pending != m_reputationPending.end()) {
        m_reputationPending.erase(pending);
        const FileListService::Entry entry = m_files.find(key);
        if (entry.state == FileListState::Loaded) {
            startReputationLookup(key, entry.files);
        }
    }

    forEachRow([&](int row, const SearchResult& r) {
        if (cacheKey(r) == key) updateRowHeight(row);
    });
    redraw();
}

void SearchResultsWidget::toggleExpand(int row) {
    const SearchResult* r = getResultAt(row);
    if (!r) return;
    std::string key = cacheKey(*r);
    bool nowExpanded = !isExpanded(key);
    setExpanded(key, nowExpanded);
    updateRowHeight(row);
    redraw();
    if (nowExpanded) {
        // Starts the fetch and returns at once; the row redraws on its own
        // when the answer lands.
        requestFiles(row);
    }
}

void SearchResultsWidget::drawFileList(int row, int x, int y, int w, int h) {
    const SearchResult* r = getResultAt(row);
    if (!r || !isExpanded(cacheKey(*r))) return;

    FileListService::Entry rf = filesForRow(row);
    fl_font(FL_HELVETICA, 11);

    int ly = y + BASE_ROW_HEIGHT;
    int limit = y + h;
    int avail = w - 26;
    if (avail <= 0) return;

    // One-pixel separator between file lines so a torrent with several files
    // reads as distinct rows instead of a block of text. Derived from the
    // live background colour, so it stays subtle in both themes without
    // storing any extra theme state.
    uchar br = 255, bg = 255, bb = 255;
    Fl::get_color(FL_BACKGROUND2_COLOR, br, bg, bb);
    const bool darkBg = br < 128;
    const Fl_Color sepColor =
        darkBg ? fl_rgb_color(70, 70, 70) : fl_rgb_color(210, 210, 210);

    if (rf.state == FileListState::Loading ||
        rf.state == FileListState::Queued) {
        fl_color(fl_rgb_color(140, 140, 140));
        fl_draw("Reading the file list...", x + 24, ly, avail, FILE_LINE_HEIGHT,
                FL_ALIGN_LEFT | FL_ALIGN_CLIP);
        return;
    }
    if (rf.state == FileListState::Failed) {
        std::string msg = rf.error.empty()
                              ? "The file list could not be obtained"
                              : ("File list unavailable: " + rf.error);
        msg = elideToWidth(msg, avail);
        fl_color(FL_FOREGROUND_COLOR);
        fl_draw(msg.c_str(), x + 24, ly, avail, FILE_LINE_HEIGHT,
                FL_ALIGN_LEFT | FL_ALIGN_CLIP);
        return;
    }
    if (rf.state == FileListState::NotRequested) return;

    // Reputation, when the user asked for it, sits above the metadata summary:
    // it is the answer to the question the shield click asked.
    {
        auto asked = m_reputation.find(cacheKey(*r));
        if (asked != m_reputation.end()) {
            const std::string line =
                elideToWidth(reputationLine(asked->second), avail);
            fl_color(reputationHasDetections(asked->second)
                         ? SecurityBadge::riskColor(RiskLevel::Critical)
                         : FL_FOREGROUND_COLOR);
            fl_font(FL_HELVETICA, 11);
            fl_draw(line.c_str(), x + 24, ly, avail, FILE_LINE_HEIGHT,
                    FL_ALIGN_LEFT | FL_ALIGN_CLIP);
            int x2 = x + w - 6;
            if (x2 < x + 24) x2 = x + 24;
            fl_color(sepColor);
            fl_line(x + 24, ly + FILE_LINE_HEIGHT - 1, x2,
                    ly + FILE_LINE_HEIGHT - 1);
            ly += FILE_LINE_HEIGHT;
        }
    }

    // Summary line first: the user expanded the row to see what is inside,
    // and the answer to "is any of this runnable?" belongs at the top.
    const SecurityReport& report = reportForRow(row);
    {
        const std::string summary = elideToWidth(
            SecurityBadge::riskLabel(report.level) + "  -  " + report.summary,
            avail);
        fl_color(SecurityBadge::riskColor(report.level));
        fl_font(FL_HELVETICA_BOLD, 11);
        fl_draw(summary.c_str(), x + 24, ly, avail, FILE_LINE_HEIGHT,
                FL_ALIGN_LEFT | FL_ALIGN_CLIP);
        fl_color(sepColor);
        int x2 = x + w - 6;
        if (x2 < x + 24) x2 = x + 24;
        fl_line(x + 24, ly + FILE_LINE_HEIGHT - 1, x2,
                ly + FILE_LINE_HEIGHT - 1);
        ly += FILE_LINE_HEIGHT;
    }

    // The small print: which whole-file hash the metadata actually carries.
    // The user is entitled to know whether a hash was found, whether or not a
    // provider was ever asked about it.
    {
        const std::string note = elideToWidth(
            hashSmallPrint(report.sha256, report.sha256Path), avail);
        fl_color(fl_rgb_color(140, 140, 140));
        fl_font(FL_HELVETICA, 10);
        fl_draw(note.c_str(), x + 24, ly, avail, FILE_LINE_HEIGHT,
                FL_ALIGN_LEFT | FL_ALIGN_CLIP);
        fl_color(sepColor);
        int x2 = x + w - 6;
        if (x2 < x + 24) x2 = x + 24;
        fl_line(x + 24, ly + FILE_LINE_HEIGHT - 1, x2,
                ly + FILE_LINE_HEIGHT - 1);
        ly += FILE_LINE_HEIGHT;
        fl_font(FL_HELVETICA, 11);
    }

    int shown = static_cast<int>(rf.files.size());
    if (shown > MAX_SHOWN_FILES) shown = MAX_SHOWN_FILES;
    for (int i = 0; i < shown && ly + FILE_LINE_HEIGHT <= limit + 1; ++i) {
        const TorrentFileEntry& f = rf.files[static_cast<size_t>(i)];
        // The size suffix is never cut: only the path is elided, measured
        // against what is left after the "- " bullet, the suffix and the
        // per-file tags. Full path shows when the window is wide; "..." only
        // when narrow.
        fl_font(FL_HELVETICA, 11);
        const std::string suffix = "  (" + Format::bytes(f.size) + ")";
        std::string tagsText;
        const std::vector<std::string> tags = SecurityAnalyzer::tagsForFile(f);
        if (!tags.empty()) {
            tagsText = "  [";
            for (std::size_t t = 0; t < tags.size() && t < 2; ++t) {
                if (t) tagsText += ", ";
                tagsText += tags[t];
            }
            tagsText += "]";
        }
        const int bulletW = static_cast<int>(fl_width("- "));
        const int suffixW = static_cast<int>(fl_width(suffix.c_str()));
        const int tagsW = static_cast<int>(fl_width(tagsText.c_str()));
        std::string path = SecurityBadge::displayText(f.path);
        path = elideToWidth(path, avail - bulletW - suffixW - tagsW);
        const std::string label = "- " + path + suffix + tagsText;
        const FileAssessment assessment = SecurityAnalyzer::assessFile(f);
        fl_color(SecurityBadge::hasWarning(assessment.level)
                     ? SecurityBadge::riskColor(assessment.level)
                     : FL_FOREGROUND_COLOR);
        fl_draw(label.c_str(), x + 24, ly, avail, FILE_LINE_HEIGHT,
                FL_ALIGN_LEFT | FL_ALIGN_CLIP);
        // Separator under every file line: makes each file its own row.
        int x2 = x + w - 6;
        if (x2 < x + 24) x2 = x + 24;
        fl_color(sepColor);
        fl_line(x + 24, ly + FILE_LINE_HEIGHT - 1, x2,
                ly + FILE_LINE_HEIGHT - 1);
        ly += FILE_LINE_HEIGHT;
    }
    if (static_cast<int>(rf.files.size()) > MAX_SHOWN_FILES && ly < limit) {
        fl_color(fl_rgb_color(140, 140, 140));
        fl_font(FL_HELVETICA, 11);
        std::string more = "... and " +
                           std::to_string(static_cast<int>(rf.files.size()) -
                                          MAX_SHOWN_FILES) +
                           " more";
        more = elideToWidth(more, avail);
        fl_draw(more.c_str(), x + 24, ly, avail, FILE_LINE_HEIGHT,
                FL_ALIGN_LEFT | FL_ALIGN_CLIP);
        ly += FILE_LINE_HEIGHT;
    }

    // Level 2: one clickable line for the archive in this list. It asks before
    // anything is fetched; the words say what will happen.
    const int archiveIndex = archiveIndexFor(rf.files);
    ArchiveAction& action = m_archive[cacheKey(*r)];
    if (archiveIndex < 0) {
        action.visible = false;
        return;
    }

    const TorrentFileEntry& archive =
        rf.files[static_cast<std::size_t>(archiveIndex)];
    action.fileIndex = archive.fileIndex;
    action.path = archive.path;
    if (action.magnet.empty()) action.magnet = r->magnet;
    if (action.torrentUrl.empty()) action.torrentUrl = r->torrentUrl;

    std::string text;
    Fl_Color color = fl_rgb_color(30, 90, 160);
    if (action.running) {
        text = "Archive: reading the archive index...";
        color = SecurityBadge::riskColor(RiskLevel::Medium);
    } else if (!action.line.empty()) {
        text = action.line;
        color = FL_FOREGROUND_COLOR;
    } else {
        text = "+ Check archive " + archive.path + " (index only)";
    }

    if (ly + FILE_LINE_HEIGHT <= limit + 1) {
        fl_font(FL_HELVETICA, 11);
        fl_color(color);
        const std::string shown = elideToWidth(text, avail);
        fl_draw(shown.c_str(), x + 24, ly, avail, FILE_LINE_HEIGHT,
                FL_ALIGN_LEFT | FL_ALIGN_CLIP);
        action.visible = true;
        action.top = ly;
        action.bottom = ly + FILE_LINE_HEIGHT;
        ly += FILE_LINE_HEIGHT;
    } else {
        action.visible = false;
    }

    // What the index holds, one line per entry: names and sizes only, because
    // the check reads headers, never file data. The rows and the height of the
    // row both come from archiveEntryRows(), so they cannot disagree.
    if (action.running || action.line.empty() || action.entries.empty()) return;

    const int shownEntries = static_cast<int>(action.entries.size());
    const int listed = shownEntries > MAX_SHOWN_ARCHIVE_ENTRIES
                           ? MAX_SHOWN_ARCHIVE_ENTRIES
                           : shownEntries;
    for (int i = 0; i < listed && ly + FILE_LINE_HEIGHT <= limit + 1; ++i) {
        const SecurityFile& entry = action.entries[static_cast<std::size_t>(i)];
        const FileAssessment assessment = SecurityAnalyzer::assessFile(entry);
        fl_font(FL_HELVETICA, 11);
        // The same rule as the torrent's own file list: a real warning gets
        // the risk colour, everything else stays plain. Being encrypted is an
        // indicator (tag and answer line), not a verdict, so it is not painted
        // as one.
        fl_color(SecurityBadge::hasWarning(assessment.level)
                     ? SecurityBadge::riskColor(assessment.level)
                     : FL_FOREGROUND_COLOR);
        const std::string line =
            elideToWidth(SecurityBadge::archiveEntryLine(entry), avail);
        fl_draw(line.c_str(), x + 24, ly, avail, FILE_LINE_HEIGHT,
                FL_ALIGN_LEFT | FL_ALIGN_CLIP);
        int x2 = x + w - 6;
        if (x2 < x + 24) x2 = x + 24;
        fl_color(sepColor);
        fl_line(x + 24, ly + FILE_LINE_HEIGHT - 1, x2,
                ly + FILE_LINE_HEIGHT - 1);
        ly += FILE_LINE_HEIGHT;
    }
    if (shownEntries > listed && ly + FILE_LINE_HEIGHT <= limit + 1) {
        fl_color(fl_rgb_color(140, 140, 140));
        fl_font(FL_HELVETICA, 11);
        std::string more =
            "... and " + std::to_string(shownEntries - listed) + " more";
        more = elideToWidth(more, avail);
        fl_draw(more.c_str(), x + 24, ly, avail, FILE_LINE_HEIGHT,
                FL_ALIGN_LEFT | FL_ALIGN_CLIP);
        ly += FILE_LINE_HEIGHT;
    }
}
