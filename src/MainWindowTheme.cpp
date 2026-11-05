#include "MainWindow.h"
#include "SettingsManager.h"
#include "Resources.h"
#include "SystemUtils.h"
#include "AssetLoader.h"
#include "ToolbarLayout.h"
#include "Format.h"
#include <FL/Fl.H>
#include <FL/Fl_Button.H>
#include <FL/Fl_Box.H>
#include <FL/Fl_Choice.H>
#include <FL/Fl_Image.H>
#include <algorithm>
#include <cctype>
#include <sstream>

// Helper to get country flag emoji
static std::string getFlagEmoji(std::string code) {
    if (code.length() != 2) return "";
    std::transform(code.begin(), code.end(), code.begin(), ::toupper);
    std::string emoji = "";
    for (char c : code) {
        if (c >= 'A' && c <= 'Z') {
            emoji += (char)0xF0;
            emoji += (char)0x9F;
            emoji += (char)0x87;
            emoji += (char)(0xA6 + (c - 'A'));
        }
    }
    return emoji;
}

void MainWindow::applyLimitStyle() {
    if (!m_btnLimit) return;

    if (m_limitModerate) {
        m_btnLimit->color(fl_rgb_color(255, 0, 255));
    } else {
        m_btnLimit->color(m_limitIdleColor);
    }
    m_btnLimit->redraw();
}

void MainWindow::updateCensorButtonState() {
    if (!m_btnCensor) return;
    
    // The eye button only makes sense while the public IP is shown at all
    if (m_showPublicIp) {
        m_btnCensor->show();
        if (m_censored && m_eyeClosedIcon) {
            m_btnCensor->image(m_eyeClosedIcon);
        } else if (!m_censored && m_eyeOpenedIcon) {
            m_btnCensor->image(m_eyeOpenedIcon);
        }
    } else {
        m_btnCensor->hide();
        m_btnCensor->image(nullptr);
    }
    
    // Give the freed space back to the status text
    if (m_statusBar && m_statusGroup) {
        int rightMargin = m_showPublicIp ? 35 : 8;
        m_statusBar->resize(2, m_statusBar->y(), m_statusGroup->w() - rightMargin, m_statusBar->h());
        m_statusBar->redraw();
    }
    
    // Keep the eye button pinned to the right edge of the status bar
    if (m_btnCensor && m_statusGroup) {
        m_btnCensor->resize(m_statusGroup->w() - 30, m_statusGroup->y() + 2, 26, STATUS_HEIGHT - 4);
    }
    
    m_btnCensor->redraw();
}

void MainWindow::updateToolbar() {
    if (!m_torrentList) return;
    
    bool hasSelection = m_torrentList->hasSelection();
    
    if (m_btnTogglePause) {
        if (hasSelection) {
            m_btnTogglePause->activate();
            
            // Check state of first selected item to determine button state
            bool isPaused = false;
            auto selected = m_torrentList->getSelectedTorrents();
            if (!selected.empty()) {
                isPaused = (selected[0]->getState() == TorrentItem::State::Paused);
            }
            
            if (isPaused) {
                m_btnTogglePause->tooltip("Resume");
                if (Resources::getPlayIcon()) m_btnTogglePause->image(Resources::getPlayIcon());
            } else {
                m_btnTogglePause->tooltip("Pause");
                if (Resources::getPauseIcon()) m_btnTogglePause->image(Resources::getPauseIcon());
            }
        } else {
            m_btnTogglePause->deactivate();
            m_btnTogglePause->tooltip("Pause");
            if (Resources::getPauseIcon()) m_btnTogglePause->image(Resources::getPauseIcon());
        }
        m_btnTogglePause->redraw();
    }
    
    if (m_btnRemove) hasSelection ? m_btnRemove->activate() : m_btnRemove->deactivate();
}

// The censoring eye icons are the only images that follow the theme, so they
// are loaded again whenever it changes. Owning both halves here is what keeps
// the constructor and applyTheme() from drifting apart.

void MainWindow::reloadEyeIcons() {
    const bool dark = m_darkMode;

    delete m_eyeOpenedIcon;
    delete m_eyeClosedIcon;
    m_eyeOpenedIcon = AssetLoader::load(dark ? "eye_opened_bright.png" : "eye_opened_dark.png", ToolbarLayout::kIconSize);
    m_eyeClosedIcon = AssetLoader::load(dark ? "eye_closed_bright.png" : "eye_closed_dark.png", ToolbarLayout::kIconSize);
}

void MainWindow::applyTheme() {
    bool darkMode = m_darkMode;
    
    if (darkMode) {
        // Dark Mode Colors
        Fl::background(0, 0, 0);       // Absolute black background for window
        Fl::background2(20, 20, 20);   // Near black for lists
        Fl::foreground(255, 255, 255); // Pure white for text
        
        // Selection colors
        Fl::set_color(FL_SELECTION_COLOR, 40, 80, 160);
        
        // 3D effect colors (redefining grays for borders)
        Fl::set_color(FL_GRAY0, 70, 70, 70);   // Dark-mid gray for borders
        Fl::set_color(FL_DARK3, 40, 40, 40);   // Darker gray for deep shadows
        Fl::set_color(FL_LIGHT3, 100, 100, 100); // Lighter gray for highlights
        
        if (m_darkModeBtn) {
            m_darkModeBtn->label(nullptr);
            if (m_darkIcon) m_darkModeBtn->image(m_darkIcon);
            else m_darkModeBtn->label("Light Mode");
        }
    } else {
        // Light Mode Colors
        Fl::background(225, 225, 225); // Standard light gray
        Fl::background2(255, 255, 255);
        Fl::foreground(0, 0, 0);
        
        // Reset colors to defaults
        Fl::set_color(FL_SELECTION_COLOR, 0, 81, 255); // Standard Blue
        
        if (m_darkModeBtn) {
            m_darkModeBtn->label(nullptr);
            if (m_brightIcon) m_darkModeBtn->image(m_brightIcon);
            else m_darkModeBtn->label("Dark Mode");
        }
    }

    // The eye icons are the only images that follow the theme.
    reloadEyeIcons();

    if (m_btnCensor) {
        if (darkMode) {
            m_btnCensor->color(fl_rgb_color(20, 20, 20));
        } else {
            m_btnCensor->color(fl_rgb_color(225, 225, 225));
        }
    }
    updateCensorButtonState();
    
    // Aplicar a todos los widgets existentes
    for (int i = 0; i < children(); i++) {
        child(i)->redraw();
    }
    
    // Forzamos el rediseño de la ventana y sus hijos
    redraw();
    if (m_torrentList) {
        m_torrentList->refreshThemeColors(darkMode);
    }
    if (m_searchResults) {
        m_searchResults->applyTheme(darkMode);
    }
    
    // The limit button keeps its magenta while the limit is on, so its colour
    // is owned by applyLimitStyle(), which the toggle also goes through.
    if (m_btnLimit) applyLimitStyle();

    // The RAM button deliberately owns no colours: like every other toolbar
    // button it draws FL_BACKGROUND_COLOR with the default label color, so its
    // box is indistinguishable from the bar in both themes. Painting a custom
    // gray here is what left a visible box around it.
}

void MainWindow::toggleDarkMode() {
    // Session only, on purpose. This used to be written to settings.ini, which
    // is how a machine that had ever been left in light mode started light
    // from then on. Nothing is persisted now: the next launch is dark whatever
    // this button did here.
    m_darkMode = !m_darkMode;
    applyTheme();
}

void MainWindow::toggleNetworkLimit() {
    m_limitModerate = !m_limitModerate;
    
    if (m_btnLimit) {
        applyLimitStyle();
    }
    
    if (m_manager) {
        if (m_limitModerate) {
            // Half the speed of what is in settings (or a default if 0)
            int maxDown = SettingsManager::instance().getMaxDownloadRate();
            int maxUp = SettingsManager::instance().getMaxUploadRate();
            
            // If settings say unlimited (0), we pick a reasonable high value to halve, 
            // but usually limit means some fixed restriction.
            // Let's assume moderate is halving the current settings, 
            // and if they are 0, we set a specific moderate limit (e.g. 500 KB/s down, 100 KB/s up)
            int limitDown = (maxDown > 0) ? maxDown / 2 : 1000; 
            int limitUp = (maxUp > 0) ? maxUp / 2 : 200;
            
            m_manager->setRateLimits(limitDown, limitUp);
        } else {
            // Restore from settings
            m_manager->setRateLimits(
                SettingsManager::instance().getMaxDownloadRate(),
                SettingsManager::instance().getMaxUploadRate()
            );
        }
    }
}

void MainWindow::toggleCensorship() {
    if (!m_showPublicIp) {
        return; // Nothing to censor while the public IP is disabled
    }
    
    m_censored = !m_censored;
    SettingsManager::instance().setIpCensored(m_censored);
    SettingsManager::instance().save();
    
    updateCensorButtonState();
    updateStatusBar();
}

std::string MainWindow::formatStatusBar() const {
    if (!m_manager) {
        return "Not initialized";
    }
    
    int totalTorrents = m_manager->getTorrentCount();
    int activeTorrents = m_manager->getActiveTorrentsCount();
    int downRate = m_manager->getTotalDownloadRate();
    int upRate = m_manager->getTotalUploadRate();
    
    std::ostringstream oss;
    oss << "Torrents: " << totalTorrents 
        << " (Active: " << activeTorrents << ")  |  ";
    
    if (downRate > 0 || upRate > 0) {
        oss << "↓ " << Format::perSecond(downRate) << "  "
            << "↑ " << Format::perSecond(upRate);
    } else {
        oss << "Idle";
    }
    
    // Add RAM usage
    oss << "  |  RAM: " << SystemUtils::getRamUsage();
    
    // Add Application Version
    oss << "  |  FTORRENT " << VERSION;
    
    // Public IP (only when enabled in Preferences; the flag is hidden too while censored)
    if (m_showPublicIp) {
        std::string ip = m_manager->getPublicIp();
        if (!ip.empty()) {
            if (m_censored) {
                oss << "  |  IP: ***.***.***.***";
            } else {
                oss << "  |  IP: " << ip << " " << getFlagEmoji(m_manager->getCountryCode());
            }
        }
    }
    
    if (m_latency >= 0) {
        oss << "  |  LATENCY: " << m_latency << " ms";
    } else if (m_latency == -2) {
        oss << "  |  LATENCY: -- ms";
    }
    
    return oss.str();
}

void MainWindow::updateRamModeButton() {
    if (!m_btnRamMode) return;
    // SettingsManager: 0 = ECO, 1 = Normal, 2 = TURBO. Anything else falls
    // back to Normal, which is also the fresh-install default.
    int mode = SettingsManager::instance().getRamMode();
    if (mode < 0 || mode > 2) mode = 1;
    Fl_Image* icon = nullptr;
    const char* label = " NORMAL";
    const char* tip = "RAM Usage Mode NORMAL (Balanced) — click for TURBO";
    if (mode == 0) {
        icon = m_ecoIcon;
        label = " ECO";
        tip = "RAM Usage Mode ECO (Zero Buffer) — click for NORMAL";
    } else if (mode == 2) {
        icon = m_turboIcon;
        label = " TURBO";
        tip = "RAM Usage Mode TURBO (Max Buffer) — click for ECO";
    } else {
        icon = m_normalIcon;
    }
    if (icon) m_btnRamMode->image(icon);
    m_btnRamMode->label(label);
    m_btnRamMode->tooltip(tip);
    m_btnRamMode->redraw();
}

void MainWindow::onRamModeCycle(Fl_Widget* w, void* data) {
    MainWindow* win = (MainWindow*)data;
    if (!win || !win->m_manager) return;
    (void)w;
    int cur = SettingsManager::instance().getRamMode();
    int next = (cur == 1) ? 2 : (cur == 2) ? 0 : 1;   // NORMAL->TURBO->ECO
    SettingsManager::instance().setRamMode(next);
    SettingsManager::instance().save();
    win->m_manager->setRamMode(next);
    win->updateRamModeButton();
}
