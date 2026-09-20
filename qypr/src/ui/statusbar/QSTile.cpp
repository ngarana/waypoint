// QSTile.cpp - Quick Settings tile implementations
#include "ui/statusbar/QSTile.hpp"
#include "render/Painter.hpp"
#include "ui/Theme.hpp"
#include "mpris/MprisController.hpp"
#include <xkbcommon/xkbcommon-keysyms.h>
#include <cmath>

namespace qypr {

namespace {
constexpr double kSliderArrowStep = 0.05;  // ±5%
// Tile corner radii. Deliberately a touch smaller than the panel's own
// qsCornerRadius (theme::statusbar::qsCornerRadius, 16) so the nested cards read
// as concentric inside the popover rather than fighting its corners. Kept as
// fixed panel-local layout constants; the colours (below) are what track the
// theme, not the geometry.
constexpr double kTileRadius = 12.0;
constexpr double kSectionRadius = 14.0;
}  // namespace

// ─── Toggle tile ──────────────────────────────────────────────────────────

void QSToggleTile::onClick(double, double) {
    if (onToggle_) onToggle_();
}

bool QSToggleTile::onScroll(double dx, double dy) {
    return onScroll_ ? onScroll_(dx, dy) : false;
}

void QSToggleTile::draw(Painter& p, int64_t now) {
    bool active = isActive_ && isActive_();
    double hAlpha = hoverAnim_.value(now);

    Color bg = theme::statusbar::panelSurface();
    if (hAlpha > 0.01) bg = theme::statusbar::panelSurfaceHover();

    p.fillRoundedRectSource(bounds, kTileRadius, bg);
    p.strokeRoundedRectSource(bounds, kTileRadius, theme::statusbar::panelSurfaceHover(), 1.0);

    // Left circular badge
    double badgeR = 16.0;
    double badgeCx = bounds.x + 22.0;
    double badgeCy = bounds.y + bounds.h / 2.0;

    if (active) {
        cairo_t* cr = p.cr();
        Color c1 = theme::color::primary;
        Color c2 = theme::color::mauve;
        if (title_.find("Wired") != std::string::npos || title_.find("WiFi") != std::string::npos ||
            title_.find("Wi-Fi") != std::string::npos ||
            title_.find("Network") != std::string::npos) {
            c1 = theme::color::blue;
            c2 = theme::color::teal;
        } else if (title_.find("Bluetooth") != std::string::npos ||
                   title_.find("BT") != std::string::npos) {
            c1 = theme::color::blue;
            c2 = theme::color::mauve;
        } else if (title_.find("Night") != std::string::npos ||
                   title_.find("Dark") != std::string::npos) {
            c1 = theme::color::warning;
            c2 = theme::color::peach;
        } else if (title_.find("Keep") != std::string::npos ||
                   title_.find("Idle") != std::string::npos ||
                   title_.find("Awake") != std::string::npos) {
            c1 = theme::color::success;
            c2 = theme::color::teal;
        } else if (title_.find("Screenshot") != std::string::npos) {
            c1 = theme::color::peach;
            c2 = theme::color::maroon;
        } else if (title_.find("Disturb") != std::string::npos ||
                   title_.find("DND") != std::string::npos) {
            c1 = theme::color::warning;
            c2 = theme::color::peach;
        }

        cairo_pattern_t* pat = cairo_pattern_create_linear(badgeCx - badgeR, badgeCy - badgeR,
                                                           badgeCx + badgeR, badgeCy + badgeR);
        cairo_pattern_add_color_stop_rgba(pat, 0.0, c1.r, c1.g, c1.b, c1.a);
        cairo_pattern_add_color_stop_rgba(pat, 1.0, c2.r, c2.g, c2.b, c2.a);
        cairo_arc(cr, badgeCx, badgeCy, badgeR, 0, 2 * M_PI);
        cairo_set_source(cr, pat);
        cairo_fill(cr);
        cairo_pattern_destroy(pat);
    } else {
        p.fillCircle(badgeCx, badgeCy, badgeR, theme::color::background);
    }

    Color iconCol = active ? theme::color::background : theme::color::textSubtle;
    TextStyle iconStyle{theme::font::iconFamily, 14.0, PANGO_WEIGHT_NORMAL, iconCol};
    Size iconSz = p.measureText(icon_, iconStyle);
    p.drawText(badgeCx - iconSz.w / 2.0, badgeCy - iconSz.h / 2.0, icon_, iconStyle);

    // Title & Subtitle column on the right
    double textX = bounds.x + 44.0;
    double maxW = bounds.w - 48.0;

    std::string sub = subtitle_ ? subtitle_() : "";
    if (sub.empty()) sub = active ? "On" : "Off";

    TextStyle titleStyle{theme::font::family, 11.5, PANGO_WEIGHT_BOLD, theme::color::text};
    TextStyle subStyle{theme::font::family, 10.0, PANGO_WEIGHT_NORMAL,
                       active ? theme::color::primary : theme::color::textSubtle};

    Size titleSz = p.measureText(title_, titleStyle, maxW);
    Size subSz = p.measureText(sub, subStyle, maxW);
    double startY = bounds.y + (bounds.h - (titleSz.h + subSz.h + 1.0)) / 2.0;

    p.drawText(textX, startY, title_, titleStyle, HAlign::Left, maxW);
    p.drawText(textX, startY + titleSz.h + 1.0, sub, subStyle, HAlign::Left, maxW);
}

// ─── Slider tile ──────────────────────────────────────────────────────────

void QSSliderTile::onClick(double x, double y) {
    if (onIconClick_ && iconBounds_.contains(x, y)) {
        onIconClick_();
        return;
    }
    updateValueFromCoord(x);
}

void QSSliderTile::onDrag(double x, double y) {
    if (onIconClick_ && iconBounds_.contains(x, y)) return;
    updateValueFromCoord(x);
}

bool QSSliderTile::handleKey(uint32_t keysym) {
    switch (keysym) {
        case XKB_KEY_Up:
        case XKB_KEY_Right:
            return stepValue(true);
        case XKB_KEY_Down:
        case XKB_KEY_Left:
            return stepValue(false);
    }
    return false;
}

bool QSSliderTile::stepValue(bool up) {
    if (!getValue_ || !onValueChange_) return false;
    const double cur = getValue_();
    const double next = clamp01(cur + (up ? kSliderArrowStep : -kSliderArrowStep));
    if (std::abs(next - cur) < 1e-9) return false;
    onValueChange_(next);
    return true;
}

void QSSliderTile::updateValueFromCoord(double x) {
    if (sliderTrackBounds_.w <= 0) return;
    double val = clamp01((x - sliderTrackBounds_.x) / sliderTrackBounds_.w);
    if (onValueChange_) onValueChange_(val);
}

void QSSliderTile::draw(Painter& p, int64_t now) {
    double val = getValue_ ? getValue_() : 0.8;

    p.fillRoundedRectSource(bounds, kTileRadius, theme::statusbar::panelSurface());
    p.strokeRoundedRectSource(bounds, kTileRadius, theme::statusbar::panelSurfaceHover(), 1.0);

    double pad = 12.0;
    std::string glyph = currentIcon();
    if (glyph.empty()) glyph = "󰃟";

    // Left Icon
    TextStyle iconStyle{theme::font::iconFamily, 16.0, PANGO_WEIGHT_NORMAL, theme::color::yellow};
    Size iconSz = p.measureText(glyph, iconStyle);
    double iconX = bounds.x + pad;
    double iconY = bounds.y + (bounds.h - iconSz.h) / 2.0;
    p.drawText(iconX, iconY, glyph, iconStyle);
    iconBounds_ = {bounds.x, bounds.y, iconX + iconSz.w + 6.0 - bounds.x, bounds.h};

    // Right Percentage Readout
    std::string pctText = std::to_string(static_cast<int>(std::round(val * 100))) + "%";
    TextStyle pctStyle{theme::font::family, 11.5, PANGO_WEIGHT_BOLD, theme::color::yellow};
    Size pctSz = p.measureText(pctText, pctStyle);
    double pctX = bounds.x + bounds.w - pad - pctSz.w;
    double pctY = bounds.y + (bounds.h - pctSz.h) / 2.0;
    p.drawText(pctX, pctY, pctText, pctStyle);

    // Track in middle
    double trackX = iconX + iconSz.w + 10.0;
    double trackW = pctX - 10.0 - trackX;
    double trackH = 8.0;
    double trackY = bounds.y + (bounds.h - trackH) / 2.0;
    sliderTrackBounds_ = {trackX, trackY, trackW, trackH};

    p.fillRoundedRectSource(sliderTrackBounds_, trackH / 2.0, theme::statusbar::panelBackground());
    Rect filled{trackX, trackY, trackW * val, trackH};
    p.fillRoundedRect(filled, trackH / 2.0, theme::color::yellow);
    p.fillCircle(trackX + trackW * val, trackY + trackH / 2.0, 5.5, theme::color::text);
}

// ─── Info tile ────────────────────────────────────────────────────────────

void QSInfoTile::draw(Painter& p, int64_t now) {
    double progress = getProgress_ ? getProgress_() : 0.5;
    std::string info = getInfo_ ? getInfo_() : "";

    p.fillRoundedRectSource(bounds, kTileRadius, theme::statusbar::panelSurface());
    p.strokeRoundedRectSource(bounds, kTileRadius, theme::statusbar::panelSurfaceHover(), 1.0);

    double pad = 12.0;

    Color icCol = progress > 0.5 ? theme::color::success
                                 : (progress > 0.2 ? theme::color::warning : theme::color::error);
    TextStyle iconStyle{theme::font::iconFamily, 16.0, PANGO_WEIGHT_NORMAL, icCol};
    const std::string ic = currentIcon();
    Size iconSz = p.measureText(ic, iconStyle);
    double iconX = bounds.x + pad;
    double iconY = bounds.y + (bounds.h - iconSz.h) / 2.0;
    p.drawText(iconX, iconY, ic, iconStyle);

    double textX = iconX + iconSz.w + 10.0;
    double maxW = bounds.w - (textX - bounds.x) - pad;

    TextStyle titleStyle{theme::font::family, 11.5, PANGO_WEIGHT_BOLD, theme::color::text};
    TextStyle infoStyle{theme::font::family, 10.0, PANGO_WEIGHT_NORMAL, theme::color::textSubtle};

    Size tSz = p.measureText(title_, titleStyle, maxW);
    Size iSz = p.measureText(info, infoStyle, maxW);
    double startY = bounds.y + (bounds.h - (tSz.h + iSz.h + 2.0)) / 2.0;

    p.drawText(textX, startY, title_, titleStyle, HAlign::Left, maxW);
    p.drawText(textX, startY + tSz.h + 2.0, info, infoStyle, HAlign::Left, maxW);
}

// ─── Header tile ──────────────────────────────────────────────────────────

void QSHeaderTile::draw(Painter& p, int64_t) {
    p.fillRoundedRectSource(bounds, kTileRadius, theme::statusbar::panelSurface());
    p.strokeRoundedRectSource(bounds, kTileRadius, theme::statusbar::panelSurfaceHover(), 1.0);

    // Avatar circle
    double avatarR = 17.0;
    double avatarCX = bounds.x + 24.0;
    double avatarCY = bounds.y + bounds.h / 2.0;

    // Gradient ring around avatar
    cairo_t* cr = p.cr();
    cairo_pattern_t* pat = cairo_pattern_create_linear(avatarCX - avatarR, avatarCY - avatarR,
                                                       avatarCX + avatarR, avatarCY + avatarR);
    cairo_pattern_add_color_stop_rgba(pat, 0.0, theme::color::primary.r, theme::color::primary.g,
                                      theme::color::primary.b, 1.0);
    cairo_pattern_add_color_stop_rgba(pat, 1.0, theme::color::mauve.r, theme::color::mauve.g,
                                      theme::color::mauve.b, 1.0);
    cairo_arc(cr, avatarCX, avatarCY, avatarR, 0, 2 * M_PI);
    cairo_set_source(cr, pat);
    cairo_fill(cr);
    cairo_pattern_destroy(pat);

    p.fillCircle(avatarCX, avatarCY, avatarR - 2.0, theme::color::background);

    TextStyle initStyle{theme::font::iconFamily, 14.0, PANGO_WEIGHT_NORMAL, theme::color::primary};
    Size initSz = p.measureText("󰀉", initStyle);
    p.drawText(avatarCX - initSz.w / 2.0, avatarCY - initSz.h / 2.0, "󰀉", initStyle);

    // Name + subtitle
    double textX = avatarCX + avatarR + 12.0;
    double maxW = bounds.w - (textX - bounds.x) - 10.0;
    TextStyle nameStyle{theme::font::family, 13.0, PANGO_WEIGHT_BOLD, theme::color::text};
    TextStyle subStyle{theme::font::family, 11.0, PANGO_WEIGHT_NORMAL, theme::color::textSubtle};

    Size nameSz = p.measureText(title_, nameStyle, maxW);
    Size subSz = p.measureText(subtitle_, subStyle, maxW);
    double startY = bounds.y + (bounds.h - (nameSz.h + subSz.h + 2.0)) / 2.0;
    p.drawText(textX, startY, title_, nameStyle, HAlign::Left, maxW);
    p.drawText(textX, startY + nameSz.h + 2.0, subtitle_, subStyle, HAlign::Left, maxW);
}

// ─── Power button ─────────────────────────────────────────────────────────

void QSPowerTile::onClick(double, double) {
    if (onClick_) onClick_();
}

void QSPowerTile::draw(Painter& p, int64_t now) {
    double hAlpha = hoverAnim_.value(now);
    Color bg = theme::statusbar::panelSurface();
    if (hAlpha > 0.01) bg = theme::statusbar::panelSurfaceHover();

    p.fillRoundedRectSource(bounds, kTileRadius, bg);
    p.strokeRoundedRectSource(bounds, kTileRadius, theme::statusbar::panelSurfaceHover(), 1.0);

    double cx = bounds.x + bounds.w / 2.0;
    double cy = bounds.y + bounds.h / 2.0;

    const char* glyph = "⏻";
    TextStyle iconStyle{theme::font::iconFamily, 18.0, PANGO_WEIGHT_NORMAL, theme::color::red};
    Size iconSz = p.measureText(glyph, iconStyle);
    p.drawText(cx - iconSz.w / 2.0, cy - iconSz.h / 2.0, glyph, iconStyle);
}

// ─── Wi-Fi combo tile ─────────────────────────────────────────────────────

void QSWifiComboTile::draw(Painter& p, int64_t now) {
    double hAlpha = hoverAnim_.value(now);
    Color bg = theme::statusbar::panelSurface();
    if (hAlpha > 0.01) bg = theme::statusbar::panelSurfaceHover();

    p.fillRoundedRectSource(bounds, kTileRadius, bg);
    p.strokeRoundedRectSource(bounds, kTileRadius, theme::statusbar::panelSurfaceHover(), 1.0);

    // Badge circle
    double badgeR = 16.0;
    double badgeCx = bounds.x + 22.0;
    double badgeCy = bounds.y + bounds.h / 2.0;

    if (enabled_) {
        cairo_t* cr = p.cr();
        Color c1 = theme::color::blue;
        Color c2 = theme::color::teal;
        cairo_pattern_t* pat = cairo_pattern_create_linear(badgeCx - badgeR, badgeCy - badgeR,
                                                           badgeCx + badgeR, badgeCy + badgeR);
        cairo_pattern_add_color_stop_rgba(pat, 0.0, c1.r, c1.g, c1.b, c1.a);
        cairo_pattern_add_color_stop_rgba(pat, 1.0, c2.r, c2.g, c2.b, c2.a);
        cairo_arc(cr, badgeCx, badgeCy, badgeR, 0, 2 * M_PI);
        cairo_set_source(cr, pat);
        cairo_fill(cr);
        cairo_pattern_destroy(pat);
    } else {
        p.fillCircle(badgeCx, badgeCy, badgeR, theme::color::background);
    }

    // Pick wifi glyph based on actual signal strength
    const char* wifiGlyph;
    if (!enabled_) {
        wifiGlyph = "󰤮";  // wifi-off
    } else if (!connected_) {
        wifiGlyph = "󰤭";  // wifi-disconnected
    } else if (strength_ >= 75) {
        wifiGlyph = "󰤨";  // excellent
    } else if (strength_ >= 50) {
        wifiGlyph = "󰤥";  // good
    } else if (strength_ >= 25) {
        wifiGlyph = "󰤢";  // fair
    } else {
        wifiGlyph = "󰤯";  // weak
    }

    TextStyle iconStyle{theme::font::iconFamily, 14.0, PANGO_WEIGHT_NORMAL,
                        enabled_ ? theme::color::background : theme::color::textSubtle};
    Size iconSz = p.measureText(wifiGlyph, iconStyle);

    if (enabled_ && scanning_) {
        // Scan in flight: orbiting arc instead of the static glyph.
        const double phase = static_cast<double>(now % 900) / 900.0;
        cairo_t* cr2 = p.cr();
        cairo_new_path(cr2);  // cairo_arc appends: detach from any leftover path
        cairo_set_source_rgba(cr2, theme::color::background.r, theme::color::background.g,
                              theme::color::background.b, theme::color::background.a);
        cairo_set_line_width(cr2, 2.0);
        cairo_set_line_cap(cr2, CAIRO_LINE_CAP_ROUND);
        const double a0 = phase * 2.0 * M_PI;
        cairo_arc(cr2, badgeCx, badgeCy, 7.0, a0, a0 + 1.4);
        cairo_stroke(cr2);
    } else {
        p.drawText(badgeCx - iconSz.w / 2.0, badgeCy - iconSz.h / 2.0, wifiGlyph, iconStyle);
    }

    double textX = bounds.x + 44.0;
    double maxW = bounds.w - 48.0 - kPowerZoneW;

    // Subtitle: connected SSID, connection state, or scan progress.
    std::string text;
    if (!enabled_) {
        text = "Off";
    } else if (scanning_) {
        text = "Scanning…";
    } else if (!connected_ || ssid_.empty()) {
        text = "Not connected";
    } else {
        text = ssid_;
    }

    TextStyle titleStyle{theme::font::family, 11.5, PANGO_WEIGHT_BOLD, theme::color::text};
    TextStyle subStyle{theme::font::family, 10.0, PANGO_WEIGHT_NORMAL,
                       enabled_ ? theme::color::primary : theme::color::textSubtle};

    Size titleSz = p.measureText("Wi-Fi", titleStyle, maxW);
    Size subSz = p.measureText(text, subStyle, maxW);
    double startY = bounds.y + (bounds.h - (titleSz.h + subSz.h + 1.0)) / 2.0;

    p.drawText(textX, startY, "Wi-Fi", titleStyle, HAlign::Left, maxW);
    p.drawText(textX, startY + titleSz.h + 1.0, text, subStyle, HAlign::Left, maxW);

    // Power zone affordance: a hairline divider and a radio glyph, so the two
    // hit zones are visible (body = picker, right strip = on/off).
    const double px = bounds.x + bounds.w - kPowerZoneW;
    p.fillRect({px, bounds.y + 10.0, 1.0, bounds.h - 20.0}, theme::statusbar::panelSurfaceHover());
    TextStyle const powerStyle{theme::font::iconFamily, 13.0, PANGO_WEIGHT_NORMAL,
                               enabled_ ? theme::color::primary : theme::color::textSubtle};
    const char* powerGlyph = enabled_ ? "󰐬" : "󰐭";  // power-off / power (plug glyphs)
    Size powerSz = p.measureText(powerGlyph, powerStyle);
    p.drawText(px + (kPowerZoneW - powerSz.w) / 2.0, bounds.y + (bounds.h - powerSz.h) / 2.0,
               powerGlyph, powerStyle);
}

// ─── Volume section tile ──────────────────────────────────────────────────

QSVolumeTile::QSVolumeTile(std::function<double()> getValue, std::function<void(double)> onChange,
                           std::function<std::string()> dynamicIcon,
                           std::function<void()> onIconClick, std::function<bool()> dimmed)
    : getValue_(std::move(getValue)),
      onValueChange_(std::move(onChange)),
      dynamicIcon_(std::move(dynamicIcon)),
      onIconClick_(std::move(onIconClick)),
      dimmed_(std::move(dimmed)) {}

void QSVolumeTile::onClick(double x, double y) {
    if (onIconClick_ && iconBounds_.contains(x, y)) {
        onIconClick_();
        return;
    }
    updateValueFromCoord(x);
}

void QSVolumeTile::onDrag(double x, double y) {
    if (onIconClick_ && iconBounds_.contains(x, y)) return;
    updateValueFromCoord(x);
}

bool QSVolumeTile::handleKey(uint32_t keysym) {
    switch (keysym) {
        case XKB_KEY_Up:
        case XKB_KEY_Right:
            return stepValue(true);
        case XKB_KEY_Down:
        case XKB_KEY_Left:
            return stepValue(false);
    }
    return false;
}

bool QSVolumeTile::stepValue(bool up) {
    if (!getValue_ || !onValueChange_) return false;
    const double cur = getValue_();
    const double next = clamp01(cur + (up ? kSliderArrowStep : -kSliderArrowStep));
    if (std::abs(next - cur) < 1e-9) return false;
    onValueChange_(next);
    return true;
}

void QSVolumeTile::updateValueFromCoord(double x) {
    if (trackBounds_.w <= 0) return;
    double val = clamp01((x - trackBounds_.x) / trackBounds_.w);
    if (onValueChange_) onValueChange_(val);
}

void QSVolumeTile::draw(Painter& p, int64_t) {
    double val = getValue_ ? getValue_() : 0.7;
    bool isMuted = dimmed_ && dimmed_();

    p.fillRoundedRectSource(bounds, kSectionRadius, theme::statusbar::panelSurface());
    p.strokeRoundedRectSource(bounds, kSectionRadius, theme::statusbar::panelSurfaceHover(), 1.0);

    double pad = 12.0;

    // Left Speaker Icon
    const std::string glyph = currentIcon();
    Color iconCol = isMuted ? theme::color::error : theme::color::primary;
    TextStyle iconStyle{theme::font::iconFamily, 16.0, PANGO_WEIGHT_NORMAL, iconCol};
    Size iconSz = p.measureText(glyph, iconStyle);
    double iconX = bounds.x + pad;
    double iconY = bounds.y + (bounds.h - iconSz.h) / 2.0;
    p.drawText(iconX, iconY, glyph, iconStyle);
    iconBounds_ = {bounds.x, bounds.y, iconX + iconSz.w + 6.0 - bounds.x, bounds.h};

    // Right chevron + percentage readout
    TextStyle arrStyle{theme::font::iconFamily, 12.0, PANGO_WEIGHT_NORMAL,
                       theme::color::textSubtle};
    Size arrSz = p.measureText("󰅂", arrStyle);
    double arrX = bounds.x + bounds.w - pad - arrSz.w;
    double arrY = bounds.y + (bounds.h - arrSz.h) / 2.0;
    p.drawText(arrX, arrY, "󰅂", arrStyle);

    std::string pctText = std::to_string(static_cast<int>(std::round(val * 100))) + "%";
    TextStyle pctStyle{theme::font::family, 11.5, PANGO_WEIGHT_BOLD,
                       isMuted ? theme::color::textMuted : theme::color::primary};
    Size pctSz = p.measureText(pctText, pctStyle);
    double pctX = arrX - 8.0 - pctSz.w;
    double pctY = bounds.y + (bounds.h - pctSz.h) / 2.0;
    p.drawText(pctX, pctY, pctText, pctStyle);

    // Track
    double trackX = iconX + iconSz.w + 10.0;
    double trackW = pctX - 10.0 - trackX;
    double trackH = 8.0;
    double trackY = bounds.y + (bounds.h - trackH) / 2.0;
    trackBounds_ = {trackX, trackY, trackW, trackH};

    p.fillRoundedRectSource(trackBounds_, trackH / 2.0, theme::statusbar::panelBackground());
    if (!isMuted && val > 0.0) {
        Rect filled{trackX, trackY, trackW * val, trackH};
        p.fillRoundedRect(filled, trackH / 2.0, theme::color::primary);
        p.fillCircle(trackX + trackW * val, trackY + trackH / 2.0, 5.5, theme::color::text);
    }
}

// ─── Media card ───────────────────────────────────────────────────────────

void QSMediaTile::onClick(double x, double y) {
    if (!mpris_ || !mpris_->active()) return;
    if (prevBounds_.contains(x, y))
        mpris_->previous();
    else if (playBounds_.contains(x, y))
        mpris_->togglePlaying();
    else if (nextBounds_.contains(x, y))
        mpris_->next();
}

void QSMediaTile::draw(Painter& p, int64_t) {
    p.fillRoundedRectSource(bounds, kSectionRadius, theme::statusbar::panelSurface());
    p.strokeRoundedRectSource(bounds, kSectionRadius, theme::statusbar::panelSurfaceHover(), 1.0);

    double pad = 12.0;

    // Artwork / Music box
    double artW = 40.0;
    double artH = 40.0;
    double artX = bounds.x + pad;
    double artY = bounds.y + (bounds.h - artH) / 2.0;
    Rect artRect{artX, artY, artW, artH};

    cairo_t* cr = p.cr();
    cairo_pattern_t* pat = cairo_pattern_create_linear(artX, artY, artX + artW, artY + artH);
    cairo_pattern_add_color_stop_rgba(pat, 0.0, theme::color::primary.r, theme::color::primary.g,
                                      theme::color::primary.b, 1.0);
    cairo_pattern_add_color_stop_rgba(pat, 1.0, theme::color::mauve.r, theme::color::mauve.g,
                                      theme::color::mauve.b, 1.0);
    cairo_new_sub_path(cr);
    cairo_arc(cr, artRect.x + artRect.w - 10.0, artRect.y + 10.0, 10.0, -M_PI / 2, 0);
    cairo_arc(cr, artRect.x + artRect.w - 10.0, artRect.y + artRect.h - 10.0, 10.0, 0, M_PI / 2);
    cairo_arc(cr, artRect.x + 10.0, artRect.y + artRect.h - 10.0, 10.0, M_PI / 2, M_PI);
    cairo_arc(cr, artRect.x + 10.0, artRect.y + 10.0, 10.0, M_PI, 3 * M_PI / 2);
    cairo_close_path(cr);
    cairo_set_source(cr, pat);
    cairo_fill(cr);
    cairo_pattern_destroy(pat);

    TextStyle noteStyle{theme::font::iconFamily, 18.0, PANGO_WEIGHT_NORMAL,
                        theme::color::background};
    Size noteSz = p.measureText("󰎈", noteStyle);
    p.drawText(artX + (artW - noteSz.w) / 2.0, artY + (artH - noteSz.h) / 2.0, "󰎈", noteStyle);

    // Track info
    std::string titleStr = "No Media Playing";
    std::string artistStr = "MPRIS";
    bool playing = false;
    if (mpris_ && mpris_->active()) {
        titleStr = mpris_->title();
        artistStr = mpris_->artist();
        if (artistStr.empty()) artistStr = mpris_->sourceLabel();
        playing = mpris_->playing();
    }

    double textX = artX + artW + 12.0;
    double ctrlBoxW = 90.0;
    double textMaxW = bounds.w - (textX - bounds.x) - ctrlBoxW - pad;

    TextStyle titleStyle{theme::font::family, 12.0, PANGO_WEIGHT_BOLD, theme::color::text};
    TextStyle subStyle{theme::font::family, 10.5, PANGO_WEIGHT_NORMAL, theme::color::textSubtle};

    Size tSz = p.measureText(titleStr, titleStyle, textMaxW);
    Size aSz = p.measureText(artistStr, subStyle, textMaxW);
    double startY = bounds.y + (bounds.h - (tSz.h + aSz.h + 2.0)) / 2.0;

    p.drawText(textX, startY, titleStr, titleStyle, HAlign::Left, textMaxW);
    p.drawText(textX, startY + tSz.h + 2.0, artistStr, subStyle, HAlign::Left, textMaxW);

    // Transport buttons (Prev, Play/Pause, Next)
    double btnY = bounds.y + (bounds.h - 28.0) / 2.0;
    double rightX = bounds.x + bounds.w - pad;

    nextBounds_ = {rightX - 22.0, btnY + 4.0, 20.0, 20.0};
    playBounds_ = {rightX - 58.0, btnY, 28.0, 28.0};
    prevBounds_ = {rightX - 86.0, btnY + 4.0, 20.0, 20.0};

    // Prev button
    TextStyle ctrlStyle{theme::font::iconFamily, 14.0, PANGO_WEIGHT_NORMAL,
                        theme::color::textSubtle};
    Size prevSz = p.measureText("󰒮", ctrlStyle);
    p.drawText(prevBounds_.x + (prevBounds_.w - prevSz.w) / 2.0,
               prevBounds_.y + (prevBounds_.h - prevSz.h) / 2.0, "󰒮", ctrlStyle);

    // Play/Pause circle button
    p.fillCircle(playBounds_.x + 14.0, playBounds_.y + 14.0, 14.0, theme::color::primary);
    const char* playGlyph = playing ? "󰏤" : "󰐊";
    TextStyle playStyle{theme::font::iconFamily, 13.0, PANGO_WEIGHT_NORMAL,
                        theme::color::background};
    Size playSz = p.measureText(playGlyph, playStyle);
    p.drawText(playBounds_.x + 14.0 - playSz.w / 2.0, playBounds_.y + 14.0 - playSz.h / 2.0,
               playGlyph, playStyle);

    // Next button
    Size nextSz = p.measureText("󰒭", ctrlStyle);
    p.drawText(nextBounds_.x + (nextBounds_.w - nextSz.w) / 2.0,
               nextBounds_.y + (nextBounds_.h - nextSz.h) / 2.0, "󰒭", ctrlStyle);
}

}  // namespace qypr
