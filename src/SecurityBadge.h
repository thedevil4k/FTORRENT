#ifndef SECURITYBADGE_H
#define SECURITYBADGE_H

#include "SecurityTypes.h"

#include <FL/Fl.H>
#include <FL/Fl_Box.H>

#include <string>

/**
 * @brief Drawing and display text for the security shield.
 *
 * The badge owns the parts that are easy to get wrong in every widget: the
 * risk palette, the shield shape, and the text sanitising that keeps an
 * attacker-controlled file name from reordering or breaking a row. Widgets
 * only decide where the shield goes.
 *
 * There is deliberately no green "safe" state. Low risk is a neutral slate;
 * the report itself says "no known indicators", never "safe".
 */
class SecurityBadge {
public:
    static Fl_Color riskColor(RiskLevel level);
    static Fl_Color riskColor(RiskLevel level, bool darkMode);
    static Fl_Color outlineColor(bool darkMode);

    // "Unknown", "Low risk", "Medium risk", "High risk", "Critical risk".
    static std::string riskLabel(RiskLevel level);

    // True only for High and Critical: the levels that deserve a confirmation.
    static bool hasWarning(RiskLevel level);

    // Replaces ASCII control characters and bidi controls with '?'. Everything
    // else, including valid UTF-8, is left byte-for-byte intact.
    static std::string displayText(const std::string& text);

    // Draws a shield into the given rectangle. Used by the tables directly and
    // by SecurityShieldBox.
    static void drawShield(int x, int y, int w, int h, RiskLevel level);

private:
    SecurityBadge() = delete;
};

/**
 * @brief A small shield widget for dialogs.
 *
 * It has no label of its own; callers put the summary next to it. setRisk()
 * also updates the tooltip, so hovering the shield explains the level.
 */
class SecurityShieldBox : public Fl_Box {
public:
    SecurityShieldBox(int x, int y, int w, int h, const char* label = nullptr);

    void setRisk(RiskLevel level, const std::string& tooltip = {});
    RiskLevel risk() const { return m_level; }

    void draw() override;

private:
    RiskLevel m_level = RiskLevel::Unknown;
};

#endif // SECURITYBADGE_H
