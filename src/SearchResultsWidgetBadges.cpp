#include "SearchResultsWidget.h"

#include <FL/Fl_Image.H>
#include <FL/Fl_RGB_Image.H>

#include "AssetLoader.h"
#include "ReputationService.h"
#include "SecurityAnalyzer.h"

#include <algorithm>
#include <memory>
#include <string>
#include <vector>

// A picture made of white or grey strokes has no colour of its own: max(r,g,b)
// and min(r,g,b) stay close on every visible pixel. A coloured icon such as a
// green archive fill does not. The 5% allowance keeps antialiased edges from
// classifying a coloured picture as ink.
bool isInkArt(const Fl_RGB_Image* rgb) {
    if (!rgb || !rgb->array || rgb->w() <= 0 || rgb->h() <= 0) return false;
    const int d = rgb->d();
    if (d != 3 && d != 4) return false;

    long visible = 0;
    long coloured = 0;
    for (int y = 0; y < rgb->h(); ++y) {
        for (int x = 0; x < rgb->w(); ++x) {
            const size_t i = (static_cast<size_t>(y) * rgb->w() + x) * d;
            if (d == 4 && rgb->array[i + 3] < 16) continue;
            const int r = rgb->array[i];
            const int g = rgb->array[i + 1];
            const int b = rgb->array[i + 2];
            const int high = std::max({r, g, b});
            const int low = std::min({r, g, b});
            if (high - low > 32) ++coloured;
            ++visible;
        }
    }
    return visible > 0 && coloured * 20 < visible;
}

// The badge asset may be neutral ink (white-on-black line art) or a coloured
// icon that carries its own fills. Ink art vanishes on a light row and has to
// be flipped; a coloured icon must be left exactly as designed, because
// inverting a green fill would turn it magenta. isInkArt() tells the two apart
// and variantCopy() applies the flip and/or the fade.
//
// 'alphaScale' fades the whole picture, which is how a badge taken from the
// title alone is drawn: present, but visibly not the confirmed one. Returns
// nullptr when the source cannot be transformed, and the caller falls back to
// the picture as loaded.
Fl_Image* variantCopy(Fl_Image* source, bool invert, double alphaScale) {
    const Fl_RGB_Image* rgb = dynamic_cast<const Fl_RGB_Image*>(source);
    if (!rgb || !rgb->array || rgb->w() <= 0 || rgb->h() <= 0) return nullptr;
    const int d = rgb->d();
    if (d != 3 && d != 4) return nullptr;
    if (alphaScale < 1.0 && d != 4) return nullptr; // no alpha to fade

    const int w = rgb->w();
    const int h = rgb->h();
    const size_t bytes = static_cast<size_t>(w) * static_cast<size_t>(h) *
                         static_cast<size_t>(d);
    unsigned char* pixels = new unsigned char[bytes];
    for (size_t i = 0; i < bytes; i += static_cast<size_t>(d)) {
        for (int channel = 0; channel < 3; ++channel) {
            const unsigned char value =
                static_cast<unsigned char>(rgb->array[i + channel]);
            pixels[i + channel] =
                invert ? static_cast<unsigned char>(255 - value) : value;
        }
        if (d == 4) {
            const double alpha = static_cast<unsigned char>(rgb->array[i + 3]);
            pixels[i + 3] =
                static_cast<unsigned char>(alpha * alphaScale + 0.5);
        }
    }

    Fl_RGB_Image* image = new Fl_RGB_Image(pixels, w, h, d);
    image->alloc_array = 1; // the image frees the buffer it was given
    return image;
}

void SearchResultsWidget::clearBadgeIcons() {
    for (BadgeArt& art : m_badgeArt) {
        delete art.confirmed;
        art.confirmed = nullptr;
        delete art.hint;
        art.hint = nullptr;
    }
}

Fl_Image* SearchResultsWidget::badgeIcon(ContentBadge badge, bool hinted) {
    const int index = static_cast<int>(badge);
    if (index < 0 || index >= static_cast<int>(ContentBadge::Count)) {
        return nullptr;
    }
    BadgeArt& art = m_badgeArt[index];
    Fl_Image*& cached = hinted ? art.hint : art.confirmed;
    if (cached) return cached;

    const char* asset = nullptr;
    switch (badge) {
        case ContentBadge::Archive:
            asset = "compressed.png";
            break;
        case ContentBadge::Encrypted:
            asset = "encrypted.png";
            break;
        case ContentBadge::DiscImage:
            asset = "iso.png";
            break;
        case ContentBadge::Video:
            asset = "video.png";
            break;
        case ContentBadge::NativeExecutable:
            asset = "executable.png";
            break;
        case ContentBadge::Count:
            break;
    }
    if (!asset) return nullptr;

    // Squared on purpose: every content badge and the shield share one 15x15
    // box, whatever the asset's aspect ratio. load() copies the PNG into
    // exactly BADGE_BOX x BADGE_BOX, so a landscape film strip and a square
    // disc end up the same size as the risk shield beside them.
    std::unique_ptr<Fl_Image> loaded(AssetLoader::load(asset, BADGE_BOX));
    if (!loaded) return nullptr; // no asset: no badge, never a blank square

    // Ink art needs its strokes flipped on a light row; a coloured icon is
    // drawn as designed on both. See isInkArt() and variantCopy().
    const Fl_RGB_Image* rgb = dynamic_cast<const Fl_RGB_Image*>(loaded.get());
    const bool invert = !m_darkMode && isInkArt(rgb);
    cached = variantCopy(loaded.get(), invert, hinted ? HINT_ALPHA : 1.0);
    if (!cached) cached = loaded.release();
    return cached;
}

const SecurityReport& SearchResultsWidget::reportForRow(int row) {
    static const SecurityReport unknown = [] {
        SecurityReport report;
        report.summary = "No file list available. Metadata risk is unknown.";
        return report;
    }();

    const SearchResult* r = getResultAt(row);
    if (!r) return unknown;

    const std::string key = cacheKey(*r);
    auto cached = m_reports.find(key);
    if (cached != m_reports.end()) return cached->second;

    const FileListService::Entry rf = filesForRow(row);
    if (rf.state != FileListState::Loaded) return unknown;

    auto inserted = m_reports.emplace(key, SecurityAnalyzer::analyze(rf.files));
    return inserted.first->second;
}

bool SearchResultsWidget::shieldHit(int row, int x, int y) {
    if (row < 0 || row >= rows()) return false;
    int cellX = 0, cellY = 0, cellW = 0, cellH = 0;
    if (!find_cell(CONTEXT_CELL, row, COL_NAME, cellX, cellY, cellW, cellH)) {
        return false;
    }
    (void)cellW;
    (void)cellH;
    // Same 15 px box the shield is drawn in, in the top slice of the row.
    return x >= cellX + 5 && x <= cellX + 5 + SHIELD_BOX && y >= cellY &&
           y <= cellY + SHIELD_BOX + 4;
}

void SearchResultsWidget::onShieldClicked(int row) {
    const SearchResult* result = getResultAt(row);
    if (!result) return;

    const std::string key = cacheKey(*result);
    const FileListService::Entry entry = filesForRow(row);
    if (entry.state != FileListState::Loaded || entry.files.empty()) {
        // No hashes yet. The click is the request for them: fetch the list and
        // run the lookup the moment it lands. This is still one user action,
        // not a background query.
        m_reputationPending[key] = true;
        requestFiles(row);
        redraw();
        return;
    }
    startReputationLookup(key, entry.files);
}

void SearchResultsWidget::startReputationLookup(
    const std::string& key, const std::vector<TorrentFileEntry>& files) {
    const ReputationTarget target = findReputationTarget(files);

    if (!target.found()) {
        m_reputation[key] = ReputationService::noHashResult();
        forEachRow([&](int row, const SearchResult& r) {
            if (cacheKey(r) == key) updateRowHeight(row);
        });
        redraw();
        return;
    }

    ReputationService& service = sharedReputationService();
    std::shared_ptr<bool> alive = m_alive;
    SearchResultsWidget* self = this;
    const ReputationService::Start start = service.request(
        target.sha256, [self, alive, key](const ReputationResult& answer) {
            if (!alive || !*alive) return;
            self->m_reputation[key] = answer;
            self->forEachRow([&](int row, const SearchResult& r) {
                if (SearchResultsWidget::cacheKey(r) == key) {
                    self->updateRowHeight(row);
                }
            });
            self->redraw();
        });

    switch (start) {
        case ReputationService::Start::Started:
        case ReputationService::Start::AlreadyRunning: {
            ReputationResult querying;
            querying.state = ReputationState::Querying;
            querying.sha256 = target.sha256;
            m_reputation[key] = querying;
            break;
        }
        case ReputationService::Start::Cached:
            m_reputation[key] = service.find(target.sha256);
            break;
        case ReputationService::Start::Rejected: {
            ReputationResult rejected;
            rejected.state = ReputationState::Error;
            rejected.sha256 = target.sha256;
            rejected.message = "Too many reputation checks are running. Try "
                               "again in a moment.";
            m_reputation[key] = rejected;
            break;
        }
    }

    forEachRow([&](int row, const SearchResult& r) {
        if (cacheKey(r) == key) updateRowHeight(row);
    });
    redraw();
}
