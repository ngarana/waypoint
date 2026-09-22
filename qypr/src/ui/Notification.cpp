#include "ui/Notification.hpp"

#include <cctype>
#include <numeric>

#include "render/Painter.hpp"
#include "render/IconResolver.hpp"
#include "ui/Theme.hpp"

namespace qypr {

namespace {
TextStyle titleStyle(const theme::State& theme) {
    return TextStyle{.family = theme.font.family,
                     .size = static_cast<double>(theme.notification.titleSize),
                     .weight = PANGO_WEIGHT_BOLD,
                     .color = theme.colors.text};
}
TextStyle bodyStyle(const theme::State& theme) {
    return TextStyle{.family = theme.font.family,
                     .size = static_cast<double>(theme.notification.bodySize),
                     .weight = PANGO_WEIGHT_NORMAL,
                     .color = theme.colors.textSubtle};
}
TextStyle iconStyle(const theme::State& theme, double tile) {
    return TextStyle{.family = theme.font.family,
                     .size = tile * 0.5,
                     .weight = PANGO_WEIGHT_BOLD,
                     .color = theme.colors.text};
}

// First printable character of `s`, uppercased — the tile fallback glyph.
std::string tileGlyph(const std::string& s) {
    for (const unsigned char c : s) {
        if (std::isspace(c) == 0) { return std::string{1, static_cast<char>(std::toupper(c))}; }
    }
    return "?";
}
}  // namespace

void NotificationView::update(std::vector<Notification> notes) {
    std::vector<Card> next;
    next.reserve(notes.size());
    for (auto& n : notes) {
        auto it = std::ranges::find_if(cards_, [&](const Card& c) { return c.note.id == n.id; });
        if (it != cards_.end()) {
            Card c = *it;           // keep its animation state (no re-fade)
            c.note = std::move(n);  // refresh text/icon/accent
            next.push_back(std::move(c));
        } else {
            Card c{.note = std::move(n),
                   .appear = Animated{0},
                   .expandProgress = Animated{0.0},
                   .hovered = false,
                   .closeHovered = false,
                   .expanded = false};
            c.appear.animateTo(1.0, theme().anim.reveal, ease::inOutQuad);
            next.push_back(std::move(c));
        }
    }
    cards_ = std::move(next);
    layout_.clear();
}

double NotificationView::cardHeight(const theme::State& theme, const Card& c, Painter& p, double w,
                                    int64_t now) {
    const double pad = theme.notification.padding;
    const double icon = theme.notification.iconSize;
    const double textW = w - (2 * pad) - icon - pad;
    std::string head = "New Notification";
    if (!c.note.sensitive) { head = c.note.title.empty() ? c.note.app : c.note.title; }
    const std::string body = c.note.sensitive ? "Contents hidden" : c.note.body;

    const double titleW = textW - 24;  // reserve space for close button
    const double titleH = p.measureText(head, titleStyle(theme), titleW).h;

    const double collapsedTextH = titleH;
    const double bodyH = p.measureText(body, bodyStyle(theme), textW).h;
    const double expandedTextH = titleH + theme.spacing.small + bodyH;

    const double progress = clamp01(c.expandProgress.value(now));
    const double textH = collapsedTextH + ((expandedTextH - collapsedTextH) * progress);

    return (2 * pad) + std::max(icon, textH);
}

void NotificationView::drawCard(const theme::State& theme, Card& c, Painter& p, int64_t now,
                                const Rect& r) {
    const double a = clamp01(c.appear.value(now));
    if (a <= 0.01) { return; }

    const double pad = theme.notification.padding;
    const double icon = theme.notification.iconSize;
    const double textW = r.w - (2 * pad) - icon - pad;
    std::string head = "New Notification";
    if (!c.note.sensitive) { head = c.note.title.empty() ? c.note.app : c.note.title; }
    const std::string body = c.note.sensitive ? "Contents hidden" : c.note.body;

    auto paint = [&] {
        const Color fill = c.hovered ? theme.colors.glassHover : theme.colors.glass;
        const Color border = c.hovered ? theme.colors.primary : theme.colors.glassBorder;
        p.fillRoundedRect(r, theme.notification.radius, fill);
        p.strokeRoundedRect(r, theme.notification.radius, border, 1);

        // App tile (coloured square with a glyph).
        const Rect tile{.x = r.x + pad, .y = r.y + pad, .w = icon, .h = icon};
        p.fillRoundedRect(tile, theme.radius.medium, c.note.accent.withAlpha(0.9));
        // Never reveal the real app icon for a sensitive notification — the
        // icon alone would disclose which app it came from. Otherwise try the
        // notification's own icon hint first, then fall back to the app name
        // (e.g. blueman sends "audio-card" as its hint, which isn't a shipped
        // PNG, but "blueman" resolves to blueman.png).
        cairo_surface_t* iconSurf = nullptr;
        if (!c.note.sensitive) {
            iconSurf = IconResolver::instance().get(c.note.icon);
            if (iconSurf == nullptr) { iconSurf = IconResolver::instance().get(c.note.app); }
        }
        if (iconSurf != nullptr) {
            const Rect iconDest{.x = tile.x + 2, .y = tile.y + 2, .w = tile.w - 4, .h = tile.h - 4};
            p.drawSurface(iconSurf, iconDest);
        } else {
            const std::string glyph = tileGlyph(c.note.app);
            const TextStyle is = iconStyle(theme, icon);
            const Size gs = p.measureText(glyph, is);
            p.drawText(tile.cx() - (gs.w / 2.0), tile.cy() - (gs.h / 2.0), glyph, is, HAlign::Left);
        }

        // Text block, vertically centred against the tile dynamically based on expandProgress.
        const double titleW = textW - 24;  // reserve space for close button
        const Size ts = p.measureText(head, titleStyle(theme), titleW);
        const Size bs = p.measureText(body, bodyStyle(theme), textW);

        const double progress = clamp01(c.expandProgress.value(now));
        const double textH = ts.h + ((theme.spacing.small + bs.h) * progress);
        const double ty = r.y + pad + std::max(0.0, (icon - textH) / 2.0);
        const double tx = r.x + pad + icon + pad;

        p.drawText(tx, ty, head, titleStyle(theme), HAlign::Left, titleW);
        if (progress > 0.01) {
            p.pushGroup();
            p.drawText(tx, ty + ts.h + theme.spacing.small, body, bodyStyle(theme), HAlign::Left,
                       textW);
            p.popGroupWithAlpha(progress);
        }

        // Close button (×) on hover.
        if (c.hovered) {
            const Rect closeRect{.x = r.x + r.w - pad - 24, .y = r.y + pad, .w = 24, .h = 24};
            if (c.closeHovered) {
                p.fillRoundedRect(closeRect, theme.radius.small,
                                  theme.colors.surfaceHover.withAlpha(0.5));
            }
            const TextStyle cs{.family = theme.font.family,
                               .size = 12,
                               .weight = PANGO_WEIGHT_BOLD,
                               .color =
                                   c.closeHovered ? theme.colors.error : theme.colors.textMuted};
            const std::string closeGlyph = "×";
            const Size gs = p.measureText(closeGlyph, cs);
            p.drawText(closeRect.cx() - (gs.w / 2.0), closeRect.cy() - (gs.h / 2.0) - 1.0,
                       closeGlyph, cs, HAlign::Left);
        }
    };

    if (a >= 0.999) {
        paint();
        return;
    }
    p.pushGroup();
    paint();
    p.popGroupWithAlpha(a);
}

void NotificationView::draw(Painter& p, int64_t now, double left, double bottom, double maxWidth) {
    layout_.clear();
    if (cards_.empty()) { return; }

    const auto w = std::min(maxWidth, static_cast<double>(theme().notification.cardWidth));
    const double gap = theme().notification.gap;
    const auto maxVisible = static_cast<size_t>(theme().notification.maxVisible);
    const size_t start =
        cards_.size() > maxVisible ? cards_.size() - maxVisible : static_cast<size_t>(0);

    // Visible slice, oldest card at the top, newest at the bottom.
    std::vector<double> heights;
    for (size_t i = start; i < cards_.size(); ++i) {
        heights.push_back(cardHeight(theme(), cards_.at(i), p, w, now));
    }
    double total = std::accumulate(heights.begin(), heights.end(), 0.0);
    if (heights.size() > 1) { total += gap * static_cast<double>(heights.size() - 1); }

    double y = bottom - total;
    for (size_t k = 0; k < heights.size(); ++k) {
        const size_t idx = start + k;
        const double h = heights.at(k);
        const Rect r{.x = left, .y = y, .w = w, .h = h};
        layout_.emplace_back(idx, r);
        drawCard(theme(), cards_.at(idx), p, now, r);
        y += h + gap;
    }
}

bool NotificationView::handlePress(double x, double y, int64_t now) {
    (void)now;
    for (const auto& [idx, r] : layout_) {
        if (r.contains(x, y) && idx < cards_.size()) {
            const double pad = theme().notification.padding;
            const Rect closeRect{.x = r.x + r.w - pad - 24, .y = r.y + pad, .w = 24, .h = 24};
            if (closeRect.contains(x, y)) {
                cards_.erase(cards_.begin() + static_cast<std::vector<Card>::difference_type>(idx));
                layout_.clear();
                return true;
            }
            Card& c = cards_.at(idx);
            c.expanded = !c.expanded;
            const double target = c.expanded ? 1.0 : 0.0;
            c.expandProgress.animateTo(target, theme().anim.medium, ease::inOutQuad);
            return true;
        }
    }
    return false;
}

void NotificationView::updateHover(double x, double y, int64_t now) {
    (void)now;
    for (const auto& [idx, r] : layout_) {
        if (idx < cards_.size()) {
            cards_.at(idx).hovered = r.contains(x, y);
            if (cards_.at(idx).hovered) {
                const double pad = theme().notification.padding;
                const Rect closeRect{.x = r.x + r.w - pad - 24, .y = r.y + pad, .w = 24, .h = 24};
                cards_.at(idx).closeHovered = closeRect.contains(x, y);
            } else {
                cards_.at(idx).closeHovered = false;
            }
        }
    }
}

void NotificationView::clearHover(int64_t now) {
    (void)now;
    for (auto& c : cards_) {
        c.hovered = false;
        c.closeHovered = false;
    }
}

bool NotificationView::animating(int64_t now) const {
    return std::ranges::any_of(cards_, [now](const Card& c) {
        return c.appear.active(now) || c.expandProgress.active(now);
    });
}

std::vector<Notification> demoNotifications() {
    return {
        Notification{.id = 1,
                     .postedAt = 0,
                     .app = "Calendar",
                     .title = "Team Standup",
                     .body = "10:30 AM — Daily sync in Meeting Room B",
                     .icon = "",
                     .accent = theme::kDefaultState.colors.blue},
        Notification{.id = 2,
                     .postedAt = 0,
                     .app = "Mail",
                     .title = "New message from Priya",
                     .body = "Re: Q3 roadmap — please review the attached draft",
                     .icon = "",
                     .accent = theme::kDefaultState.colors.green,
                     .daemonId = 0,
                     .urgency = 1,
                     .sensitive = true},
        Notification{.id = 3,
                     .postedAt = 0,
                     .app = "System",
                     .title = "Update available",
                     .body = "Hyprland 0.41.0 can be installed",
                     .icon = "",
                     .accent = theme::kDefaultState.colors.mauve},
        Notification{.id = 4,
                     .postedAt = 0,
                     .app = "Weather",
                     .title = "Rain expected",
                     .body = "Showers this afternoon, high of 18°C",
                     .icon = "",
                     .accent = theme::kDefaultState.colors.yellow},
    };
}

}  // namespace qypr
