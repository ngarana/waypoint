// StatusIndicator.cpp - Base status bar applet implementation
#include "ui/statusbar/StatusIndicator.hpp"
#include "render/Painter.hpp"
#include "render/IconResolver.hpp"
#include "ui/Theme.hpp"

namespace qypr {

namespace {
constexpr double kContentGap = 8.0;  // between icon and label
constexpr double kSidePad = 8.0;     // hover-zone padding each side
// Symbolic-theme icon rasters are square; the on-strip render size comes from
// theme().statusbar.symbolicIconSize (bar.conf: bar-symbolic-icon-size) so it can
// be tuned independently of the bar height and the Nerd Font glyph size.
}  // namespace

Color StatusIndicator::iconColor() const {
    return theme().colors.text;
}

StatusIndicator::~StatusIndicator() {
    if (themedIconCache_ != nullptr) { cairo_surface_destroy(themedIconCache_); }
}

// Lazy theme-resolved surface for the current themedIcon() name; caches the
// (name → surface) pair so a battery tier change reuses the same raster. The
// cache key is the icon name (compared by string, since pointer identity is not
// viable for std::string).
cairo_surface_t* StatusIndicator::themedIconSurface() {
    const std::string name = themedIcon();
    if (name.empty()) {
        if (themedIconCache_ != nullptr) {
            cairo_surface_destroy(themedIconCache_);
            themedIconCache_ = nullptr;
        }
        themedIconCacheKey_.clear();
        return nullptr;
    }
    if (name == themedIconCacheKey_ && themedIconCache_ != nullptr) { return themedIconCache_; }

    if (themedIconCache_ != nullptr) {
        cairo_surface_destroy(themedIconCache_);
        themedIconCache_ = nullptr;
    }
    themedIconCacheKey_ = name;
    if (cairo_surface_t* resolved = IconResolver::instance().get(name)) {
        // IconResolver owns its cache reference and may destroy it during
        // eviction. Keep an independent reference while this indicator uses it.
        themedIconCache_ = cairo_surface_reference(resolved);
    }
    return themedIconCache_;
}

// Which representation to render this frame. In Glyph mode we never touch the
// theme; otherwise we try the active theme and return null when it does not
// carry the name (IconResolver caches the miss), so the caller falls back to the
// Nerd Font glyph. Shared by measureWidth() and draw() so they never disagree.
cairo_surface_t* StatusIndicator::displayIconSurface() {
    if (theme().icons.mode == theme::icons::Mode::Glyph) { return nullptr; }
    return themedIconSurface();
}

double StatusIndicator::measureWidth(Painter& p) {
    const cairo_surface_t* themed = displayIconSurface();
    const std::string ic = icon();
    const std::string lbl = label();
    double w = 0;

    // A resolved symbolic icon takes precedence over the Nerd Font glyph; if the
    // active theme lacks it (themed == null) we measure the glyph instead, so the
    // reserved width always matches what draw() paints.
    if (themed != nullptr) {
        // The themed surface measures square (symbolicIconSize); measured width
        // matches its eventual footprint so centring matches the draw path.
        w += theme().statusbar.symbolicIconSize;
    } else if (!ic.empty()) {
        const TextStyle iconStyle{.family = theme().font.iconFamily,
                                  .size = theme().statusbar.iconSize,
                                  .weight = PANGO_WEIGHT_NORMAL,
                                  .color = iconColor()};
        w += p.measureText(ic, iconStyle).w;
    }
    if (!lbl.empty()) {
        const TextStyle labelStyle{.family = theme().font.family,
                                   .size = labelFontSize(),
                                   .weight = PANGO_WEIGHT_NORMAL,
                                   .color = iconColor()};
        if (w > 0) { w += kContentGap; }
        w += p.measureText(lbl, labelStyle).w;
    }
    return w + (2 * kSidePad);
}

void StatusIndicator::draw(Painter& p, int64_t now) {
    if (!visible) { return; }

    const double alpha = hoverAlpha_.value(now);
    const double scale = hoverScale_.value(now);

    // 1. Hover background pill (subtle surface highlight inside the chip)
    if (alpha > 0.01) {
        const Color bg = theme().colors.surfaceHover.withAlpha(alpha * 0.5);
        Rect hoverRect = bounds;
        hoverRect.y += 2.0;
        hoverRect.h -= 4.0;
        p.fillRoundedRect(hoverRect, 8.0, bg);
    }

    // 2. Focus ring
    if (focused) {
        Rect focusRect = bounds;
        focusRect.y += 1.0;
        focusRect.h -= 2.0;
        p.strokeRoundedRect(focusRect, 8.0, theme().colors.primary, 1.5);
    }

    // 3. Content: optional icon + optional label, centered in bounds.
    // Resolve the display representation once — a themed symbolic surface (from
    // the active icon theme) when available and enabled, else the Nerd Font glyph.
    // measureWidth() used the same predicate, so layout and paint agree.
    cairo_surface_t* themedSurf = displayIconSurface();
    const std::string ic = icon();
    const std::string lbl = label();

    // Phase 6 polish: if the icon has changed since the last frame, kick off a
    // 300ms ease-in-out crossfade. The outgoing icon rerenders under the incoming
    // one at shrinking alpha; both share the new footprint. Battery level changes
    // and WiFi signal tiers go through this path for free. The crossfade tracks
    // the themed icon when one is shown, else the glyph.
    const std::string iconKey = themedSurf != nullptr ? ("theme:" + themedIcon()) : ic;
    if (!iconKey.empty() && iconKey != lastDrawnIcon_) {
        if (!lastDrawnIcon_.empty() && prevDrawnIcon_.empty()) {
            prevDrawnIcon_ = lastDrawnIcon_;
            crossfadeStartMs_ = now;
        }
        lastDrawnIcon_ = iconKey;
    }
    double crossfadeT = 1.0;
    if (!prevDrawnIcon_.empty()) {
        crossfadeT = clamp01(static_cast<double>(now - crossfadeStartMs_) /
                             static_cast<double>(kCrossfadeMs));
        if (crossfadeT >= 1.0) { prevDrawnIcon_.clear(); }
    }

    // themedSurf resolved above. A themed icon + label are drawn side by side,
    // both vertically centred.
    const double iconPx = theme().statusbar.symbolicIconSize * (scale > 1.001 ? scale : 1.0);

    TextStyle labelStyle{.family = theme().font.family,
                         .size = labelFontSize(),
                         .weight = PANGO_WEIGHT_NORMAL,
                         .color = iconColor()};
    if (scale > 1.001) { labelStyle.size *= scale; }

    Size labelSz{};
    if (!lbl.empty()) { labelSz = p.measureText(lbl, labelStyle); }

    double contentW = 0;
    if (themedSurf != nullptr) {
        contentW += iconPx;
    } else if (!ic.empty()) {
        TextStyle iconStyle{.family = theme().font.iconFamily,
                            .size = theme().statusbar.iconSize,
                            .weight = PANGO_WEIGHT_NORMAL,
                            .color = iconColor()};
        if (scale > 1.001) { iconStyle.size *= scale; }
        // Measure the glyph; the draw still uses drawTextShadowed.
        const Size iconSz = p.measureText(ic, iconStyle);
        contentW += iconSz.w;
    }
    if (!lbl.empty()) {
        if (contentW > 0) { contentW += kContentGap; }
        contentW += labelSz.w;
    }

    const double shadowA = theme().effects.shadowOpacity;
    const double shadowOff = theme().effects.shadowOffset;
    double x = bounds.x + ((bounds.w - contentW) / 2.0);

    if (themedSurf != nullptr) {
        // The themed icon is painted with iconColor() as its tint, so a
        // symbolic SVG (black-with-alpha) recolours to match the bar text.
        // Vertical-centre to the same baseline the glyph path uses.
        const double iconY = bounds.y + ((bounds.h - iconPx) / 2.0);
        p.drawSurfaceTinted(themedSurf, Rect{.x = x, .y = iconY, .w = iconPx, .h = iconPx},
                            iconColor());
        // Crossfade the outgoing themed icon under the incoming one.
        if (!prevDrawnIcon_.empty() && prevDrawnIcon_.starts_with("theme:") && crossfadeT < 1.0) {
            const double t = ease::inOutQuad(crossfadeT);
            const Color fade = iconColor().withAlpha((1.0 - t) * iconColor().a);
            p.drawSurfaceTinted(themedSurf, Rect{.x = x, .y = iconY, .w = iconPx, .h = iconPx},
                                fade);
        }
        x += iconPx + (lbl.empty() ? 0.0 : kContentGap);
    } else if (!ic.empty()) {
        TextStyle iconStyle{.family = theme().font.iconFamily,
                            .size = theme().statusbar.iconSize,
                            .weight = PANGO_WEIGHT_NORMAL,
                            .color = iconColor()};
        if (scale > 1.001) { iconStyle.size *= scale; }
        const Size iconSz = p.measureText(ic, iconStyle);
        p.drawTextShadowed(x, bounds.y + ((bounds.h - iconSz.h) / 2.0), ic, iconStyle, HAlign::Left,
                           shadowA, shadowOff);
        // During the swap, layer the outgoing glyph at shrinking alpha so the
        // two dissolve rather than blink.
        if (!prevDrawnIcon_.empty() && !prevDrawnIcon_.starts_with("theme:") && crossfadeT < 1.0) {
            const double t = ease::inOutQuad(crossfadeT);
            TextStyle prevStyle = iconStyle;
            prevStyle.color = iconColor().withAlpha((1.0 - t) * iconColor().a);
            p.drawText(x, bounds.y + ((bounds.h - iconSz.h) / 2.0), prevDrawnIcon_, prevStyle);
        }
        x += iconSz.w + (lbl.empty() ? 0.0 : kContentGap);
    }
    if (!lbl.empty()) {
        p.drawTextShadowed(x, bounds.y + ((bounds.h - labelSz.h) / 2.0), lbl, labelStyle,
                           HAlign::Left, shadowA, shadowOff);
    }
}

bool StatusIndicator::animating(int64_t now) const {
    return hoverAlpha_.active(now) || hoverScale_.active(now) || !prevDrawnIcon_.empty();
}

}  // namespace qypr
