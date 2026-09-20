// PagerIndicator.cpp - Merged workspace-pager/taskbar implementation.
#include "ui/indicators/PagerIndicator.hpp"

#include <algorithm>
#include <ranges>

#include "render/Painter.hpp"
#include "ui/Theme.hpp"
#include "ui/indicators/AppTile.hpp"
#include "ui/statusbar/IndicatorRegistry.hpp"

namespace qypr {

namespace {
constexpr double kExpandDurationMs = 200.0;
constexpr double kMinimizedAlpha = 0.45;
constexpr double kDotChipW = 10.0;   // empty collapsed chip: a bare dot
constexpr double kUnderlineH = 2.0;  // focused-window marker under its icon
constexpr double kLooseAlpha = 0.85;

TextStyle nameStyle(double px, const Color& c) {
    return {theme::font::family, px, PANGO_WEIGHT_MEDIUM, c};
}
TextStyle badgeStyle(double px, const Color& c) {
    return {theme::font::family, px, PANGO_WEIGHT_BOLD, c};
}
}  // namespace

PagerIndicator::PagerIndicator(const SystemBackends& backends)
    : StatusIndicator("pager", Zone::Left, -100),
      ws_(backends.workspace),
      tl_(backends.toplevel) {
    visible = false;  // hidden until either protocol reports something
}

PagerIndicator::Metrics PagerIndicator::metricsFor(double t) {
    return {.iconPx = lerp(13.0, 17.0, t),
            .chipPadX = lerp(6.0, 9.0, t),
            .iconGap = lerp(2.0, 3.0, t),
            .chipGap = lerp(4.0, 7.0, t),
            .chipH = lerp(20.0, 26.0, t),
            .fontPx = lerp(12.0, 13.0, t),
            .badgePx = 9.0,
            .sidePad = 4.0};
}

std::string PagerIndicator::tooltip() const {
    const SessionView& v = mapper_.view();
    std::string tip;
    auto addApps = [&](const std::vector<uint64_t>& ids) {
        bool first = true;
        for (uint64_t id : ids) {
            const ToplevelWindow* w = v.window(id);
            if (w == nullptr) { continue; }
            if (!first) { tip += ", "; }
            first = false;
            tip += apptile::prettyApp(w->appId);
        }
    };
    for (const auto& c : v.clusters) {
        if (!tip.empty()) { tip += "\n"; }
        tip += c.name + ": ";
        if (c.windows.empty()) {
            tip += "empty";
        } else {
            addApps(c.windows);
            if (c.active) { tip += "  ●"; }
        }
    }
    if (!v.unassigned.empty()) {
        if (!tip.empty()) { tip += "\n"; }
        tip += "· ";
        addApps(v.unassigned);
    }
    if (tip.empty()) { return v.hasWorkspaces ? "Workspaces" : "No open windows"; }
    return tip;
}

double PagerIndicator::clusterWidth(Painter& p, const SessionCluster& c, const Metrics& m,
                                    bool expanded) {
    const bool showName = expanded || c.active;
    const size_t cap = expanded ? kMaxIconsExpanded : kMaxIconsCollapsed;
    const size_t n = std::min(c.windows.size(), cap);
    const bool overflow = c.windows.size() > n;

    double w = 2 * m.chipPadX;
    if (showName) {
        const Size ts = p.measureText(c.name, nameStyle(m.fontPx, theme::color::text));
        w += ts.w + (m.iconGap * 2);
    }
    if (n > 0) {
        w += (static_cast<double>(n) * m.iconPx) + (static_cast<double>(n - 1) * m.iconGap);
        if (overflow) {
            const std::string badge = "+" + std::to_string(c.windows.size() - n);
            w += m.iconGap + p.measureText(badge, badgeStyle(m.badgePx, theme::color::text)).w;
        }
    } else if (!showName) {
        return kDotChipW;
    }
    return std::max(w, kDotChipW);
}

double PagerIndicator::measureState(Painter& p, bool expanded) const {
    const SessionView& v = mapper_.view();
    const Metrics m = metricsFor(expanded ? 1.0 : 0.0);
    double w = 2 * m.sidePad;
    bool first = true;
    for (const auto& c : v.clusters) {
        if (!first) { w += m.chipGap; }
        w += clusterWidth(p, c, m, expanded);
        first = false;
    }
    if (!v.unassigned.empty()) {
        if (!first) { w += m.chipGap; }
        const size_t n =
            std::min(v.unassigned.size(), expanded ? kMaxIconsExpanded : kMaxIconsCollapsed);
        w += (static_cast<double>(n) * m.iconPx) +
             (static_cast<double>(n > 0 ? n - 1 : 0) * m.iconGap * 2);
    }
    return w;
}

double PagerIndicator::measureWidth(Painter& p) {
    const SessionView& v = mapper_.view();
    if (!visible || (v.clusters.empty() && v.unassigned.empty())) { return 0; }
    const double t = expandProgress_.value(nowMs());
    return lerp(measureState(p, false), measureState(p, true), t);
}

void PagerIndicator::drawChip(Painter& p, double x, const SessionCluster& c, const Metrics& m,
                              bool expanded) {
    const SessionView& v = mapper_.view();
    const bool showName = expanded || c.active;
    const size_t cap = expanded ? kMaxIconsExpanded : kMaxIconsCollapsed;
    const size_t n = std::min(c.windows.size(), cap);
    const bool overflow = c.windows.size() > n;
    const double chipY = bounds.y + ((bounds.h - m.chipH) / 2.0);

    // Width — identical arithmetic to clusterWidth().
    double w = 2 * m.chipPadX;
    Size ts{};
    if (showName) {
        ts = p.measureText(c.name, nameStyle(m.fontPx, theme::color::text));
        w += ts.w + (m.iconGap * 2);
    }
    w += (static_cast<double>(n) * m.iconPx) + (static_cast<double>(n > 0 ? n - 1 : 0) * m.iconGap);
    if (overflow) {
        const std::string badge = "+" + std::to_string(c.windows.size() - n);
        w += m.iconGap + p.measureText(badge, badgeStyle(m.badgePx, theme::color::text)).w;
    }
    if (n == 0 && !showName) { w = kDotChipW; }
    w = std::max(w, kDotChipW);

    // Chip body.
    Color txtColor = theme::color::textSubtle;
    if (c.active) {
        p.fillRoundedRect({x, chipY, w, m.chipH}, m.chipH / 2.0, theme::color::primary);
        txtColor = theme::color::background;
    } else if (c.urgent) {
        p.fillRoundedRect({x, chipY, w, m.chipH}, m.chipH / 2.0,
                          theme::color::warning.withAlpha(0.28));
        txtColor = theme::color::warning;
    } else if (!c.windows.empty()) {
        p.fillRoundedRect({x, chipY, w, m.chipH}, m.chipH / 2.0,
                          theme::color::glassHover.withAlpha(0.55));
        txtColor = theme::color::text;
    }

    double cx = x + m.chipPadX;
    if (showName) {
        const TextStyle st = nameStyle(m.fontPx, txtColor);
        const double ty = bounds.y + ((bounds.h - ts.h) / 2.0);
        if (c.active || c.urgent || !c.windows.empty()) {
            p.drawText(cx, ty, c.name, st, HAlign::Left);
        } else {
            p.drawTextShadowed(cx, ty, c.name, st, HAlign::Left, theme::effects::shadowOpacity,
                               theme::effects::shadowOffset);
        }
        cx += ts.w + (m.iconGap * 2);
    } else if (n == 0) {
        p.fillCircle(x + (w / 2.0), bounds.y + (bounds.h / 2.0), 2.0,
                     c.urgent ? theme::color::warning : theme::color::textMuted);
    }

    // Resident app icons.
    for (size_t i = 0; i < n; ++i) {
        const ToplevelWindow* win = v.window(c.windows[i]);
        if (win == nullptr) { continue; }
        const double iconY = bounds.y + ((bounds.h - m.iconPx) / 2.0);
        cairo_surface_t* s = apptile::resolveIcon(win->appId);
        const double alpha = win->minimized ? kMinimizedAlpha : 1.0;

        if (s != nullptr) {
            if (alpha < 1.0) { p.pushGroup(); }
            p.drawSurface(s, {cx, iconY, m.iconPx, m.iconPx});
            if (alpha < 1.0) { p.popGroupWithAlpha(alpha); }
        } else {
            const Rect tile{cx, iconY, m.iconPx, m.iconPx};
            p.fillRoundedRect(tile, 4.0, apptile::fallbackColor(win->appId).withAlpha(alpha));
            const TextStyle st =
                badgeStyle(m.iconPx * 0.62, Color::fromHex("#1e1e2e").withAlpha(alpha));
            const std::string ch = apptile::initialFor(win->appId);
            const Size cs = p.measureText(ch, st);
            p.drawText(cx + ((m.iconPx - cs.w) / 2.0), iconY + ((m.iconPx - cs.h) / 2.0), ch, st);
        }

        // Focused-window marker: a short underline beneath the icon. Inside the
        // accented active chip the accent would vanish, so invert there.
        if (win->active) {
            const Color uc = c.active ? theme::color::background : theme::color::primary;
            p.fillRoundedRect({cx + 3.0, iconY + m.iconPx + 1.5, m.iconPx - 6.0, kUnderlineH},
                              kUnderlineH / 2.0, uc.withAlpha(alpha));
        }

        hits_.push_back(Hit{cx - 1.0, cx + m.iconPx + 1.0, c.name, win->id});
        cx += m.iconPx + m.iconGap;
    }

    if (overflow) {
        const std::string badge = "+" + std::to_string(c.windows.size() - n);
        p.drawText(cx, bounds.y + ((bounds.h - (m.badgePx * 1.4)) / 2.0), badge,
                   badgeStyle(m.badgePx, txtColor));
    }

    // The whole chip body switches workspace — recorded last so icon hits
    // (pushed earlier) win the linear scan in onClick().
    hits_.push_back(Hit{x, x + w, c.name, 0});
}

void PagerIndicator::drawLooseIcons(Painter& p, double x, const Metrics& m) {
    const SessionView& v = mapper_.view();
    const size_t n = std::min(v.unassigned.size(), kMaxIconsCollapsed);
    for (size_t i = 0; i < n; ++i) {
        const ToplevelWindow* win = v.window(v.unassigned[i]);
        if (win == nullptr) { continue; }
        const double iconY = bounds.y + ((bounds.h - m.iconPx) / 2.0);
        cairo_surface_t* s = apptile::resolveIcon(win->appId);
        if (s != nullptr) {
            p.pushGroup();
            p.drawSurface(s, {x, iconY, m.iconPx, m.iconPx});
            p.popGroupWithAlpha(kLooseAlpha);
        } else {
            const Rect tile{x, iconY, m.iconPx, m.iconPx};
            p.fillRoundedRect(tile, 4.0, apptile::fallbackColor(win->appId).withAlpha(kLooseAlpha));
            const TextStyle st =
                badgeStyle(m.iconPx * 0.62, Color::fromHex("#1e1e2e").withAlpha(kLooseAlpha));
            const std::string ch = apptile::initialFor(win->appId);
            const Size cs = p.measureText(ch, st);
            p.drawText(x + ((m.iconPx - cs.w) / 2.0), iconY + ((m.iconPx - cs.h) / 2.0), ch, st);
        }
        hits_.push_back(Hit{x - 1.0, x + m.iconPx + 1.0, "", win->id});
        x += m.iconPx + (m.iconGap * 2);
    }
}

void PagerIndicator::draw(Painter& p, int64_t now) {
    if (!visible) { return; }

    expandProgress_.animateTo(hovered ? 1.0 : 0.0, kExpandDurationMs,
                              hovered ? ease::outBack : ease::inOutQuad);
    const double t = expandProgress_.value(now);
    const bool expanded = t >= 0.5;  // threshold content, lerped reservation

    hits_.clear();
    const SessionView& v = mapper_.view();
    const Metrics m = metricsFor(expanded ? 1.0 : 0.0);

    double x = bounds.x + m.sidePad;
    for (const auto& c : v.clusters) {
        drawChip(p, x, c, m, expanded);
        x += clusterWidth(p, c, m, expanded) + m.chipGap;
    }
    if (!v.unassigned.empty()) { drawLooseIcons(p, x, m); }
}

void PagerIndicator::onBackendUpdate() {
    snapWs_ = (ws_ != nullptr) ? ws_->snapshot() : WorkspaceSnapshot{};
    snapTl_ = (tl_ != nullptr) ? tl_->snapshot() : ToplevelSnapshot{};
    mapper_.ingest(snapWs_, snapTl_);
    const SessionView& v = mapper_.view();
    visible = v.available && !(v.clusters.empty() && v.unassigned.empty());
}

bool PagerIndicator::onClick(double x, double y) {
    (void)y;
    if (!visible) { return false; }
    for (const Hit& h : hits_) {
        if (x < h.x0 || x > h.x1) { continue; }
        if (h.win != 0) {
            if (tl_ != nullptr) { tl_->activate(h.win); }
            return true;
        }
        if (ws_ != nullptr && !h.ws.empty()) { ws_->activate(h.ws); }
        return true;
    }
    return false;
}

bool PagerIndicator::onMiddleClick(double x, double y) {
    (void)y;
    if (!visible || tl_ == nullptr) { return false; }
    for (const Hit& h : hits_) {
        if (x >= h.x0 && x <= h.x1 && h.win != 0) {
            tl_->close(h.win);
            return true;
        }
    }
    return false;
}

bool PagerIndicator::onSecondaryClick(double x, double y) {
    (void)y;
    if (!visible || tl_ == nullptr) { return false; }
    for (const Hit& h : hits_) {
        if (x >= h.x0 && x <= h.x1 && h.win != 0) {
            tl_->toggleMinimize(h.win);
            return true;
        }
    }
    return false;
}

bool PagerIndicator::onScroll(double dx, double dy, double x, double y) {
    (void)x;
    (void)y;
    (void)dx;
    if (!visible || ws_ == nullptr || snapWs_.workspaces.empty() || dy == 0) { return false; }
    const int dir = dy < 0 ? -1 : 1;  // wheel up = previous, down = next
    int cur = 0;
    for (size_t i = 0; i < snapWs_.workspaces.size(); ++i) {
        if (snapWs_.workspaces[i].active) { cur = static_cast<int>(i); }
    }
    const int n = static_cast<int>(snapWs_.workspaces.size());
    const int next = (((cur + dir) % n) + n) % n;  // wrap
    ws_->activate(snapWs_.workspaces[static_cast<size_t>(next)].name);
    return true;
}

bool PagerIndicator::animating(int64_t now) const {
    return expandProgress_.active(now);
}

REGISTER_INDICATOR("pager", Zone::Left, -100, PagerIndicator)

}  // namespace qypr
