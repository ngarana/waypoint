// ListFlyout.cpp - Rows-and-buttons flyout.
#include "ui/statusbar/ListFlyout.hpp"

#include <algorithm>
#include <cmath>
#include <utility>

#include <xkbcommon/xkbcommon-keysyms.h>

#include "render/Painter.hpp"
#include "ui/Theme.hpp"
#include "ui/statusbar/PopoverLayout.hpp"

namespace qypr {

namespace {
constexpr double kMinW = 280.0;
constexpr double kMaxW = 460.0;
constexpr double kPad = 12.0;
constexpr double kHeaderH = 30.0;
constexpr double kSectionH = 24.0;
constexpr double kItemH = 44.0;
constexpr double kItemCompactH = 32.0;  // no subtitle
constexpr double kNoteH = 24.0;
constexpr double kGlyphCol = 28.0;
constexpr double kButtonH = 24.0;
constexpr double kButtonGap = 6.0;
constexpr double kButtonFont = 11.5;

Color toneColor(const theme::State& t, FlyoutRow::Tone tone) {
    switch (tone) {
        case FlyoutRow::Tone::Normal:
            return t.colors.text;
        case FlyoutRow::Tone::Error:
            return t.colors.error;
        case FlyoutRow::Tone::Success:
            return t.colors.success;
        case FlyoutRow::Tone::Subtle:
            break;
    }
    return t.colors.textSubtle;
}

void drawSpinner(Painter& p, double cx, double cy, double r, int64_t now, const Color& c) {
    const double phase = static_cast<double>(now % 900) / 900.0;
    cairo_t* cr = p.cr();
    cairo_new_path(cr);
    cairo_set_source_rgba(cr, c.r, c.g, c.b, c.a);
    cairo_set_line_width(cr, 2.0);
    cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
    const double a0 = phase * 2.0 * M_PI;
    cairo_arc(cr, cx, cy, r, a0, a0 + 1.4);
    cairo_stroke(cr);
}
}  // namespace

ListFlyout::ListFlyout(Builder build) : build_(std::move(build)) {
    rebuild();
}

void ListFlyout::rebuild() {
    if (build_) { model_ = build_(); }
}

double ListFlyout::rowHeight(const FlyoutRow& row) {
    switch (row.kind) {
        case FlyoutRow::Kind::Section:
            return kSectionH;
        case FlyoutRow::Kind::Note:
            return kNoteH;
        case FlyoutRow::Kind::Item:
            break;
    }
    return row.subtitle.empty() ? kItemCompactH : kItemH;
}

double ListFlyout::buttonWidth(const FlyoutAction& action) {
    return popover_layout::estimatedTextWidth(action.label, kButtonFont) + 20.0;
}

double ListFlyout::buttonsWidth(const FlyoutRow& row) {
    double w = 0;
    for (const auto& a : row.actions) { w += buttonWidth(a) + kButtonGap; }
    return w;
}

double ListFlyout::contentWidth() const {
    double required = popover_layout::estimatedTextWidth(model_.title, 12.0) +
                      popover_layout::estimatedTextWidth(model_.detail, 11.0) + 16.0;
    for (const auto& row : model_.rows) {
        const double text = std::max(popover_layout::estimatedTextWidth(row.title, 13.0),
                                     popover_layout::estimatedTextWidth(row.subtitle, 11.0));
        required = std::max(required, kGlyphCol + text + buttonsWidth(row) + 8.0);
    }
    return popover_layout::boundedWidth(kMinW, kMaxW, (2.0 * kPad) + required);
}

double ListFlyout::contentHeight() const {
    double h = (2.0 * kPad) + kHeaderH;
    if (model_.rows.empty()) { return h + kNoteH; }
    for (const auto& row : model_.rows) { h += rowHeight(row); }
    return h;
}

void ListFlyout::draw(Painter& p, int64_t now) {
    rebuild();
    Rect b = getBounds();
    b.y -= (1.0 - openProgress_.value(now)) * 6.0;
    if (!drawSharedBackdrop(p, b, theme().statusbar.popoverRadius)) {
        p.fillRoundedRectSource(b, theme().statusbar.popoverRadius, theme().panelSurface());
    }
    hits_.clear();

    double y = b.y + kPad;
    const TextStyle hdr{.family = theme().font.family,
                        .size = 12.0,
                        .weight = PANGO_WEIGHT_BOLD,
                        .color = theme().colors.text};
    p.drawText(b.x + kPad, y + 2.0, model_.title, hdr, HAlign::Left, b.w - (2.0 * kPad));
    if (!model_.detail.empty()) {
        const TextStyle det{.family = theme().font.family,
                            .size = 11.0,
                            .weight = PANGO_WEIGHT_NORMAL,
                            .color = theme().colors.textSubtle};
        p.drawText(b.x + b.w - kPad, y + 3.0, model_.detail, det, HAlign::Right);
    }
    y += kHeaderH;

    if (model_.rows.empty()) {
        const TextStyle e{.family = theme().font.family,
                          .size = 12.0,
                          .weight = PANGO_WEIGHT_NORMAL,
                          .color = theme().colors.textSubtle};
        p.drawText(b.x + kPad, y + 4.0, model_.empty, e);
        return;
    }

    const double innerW = b.w - (2.0 * kPad);
    for (size_t i = 0; i < model_.rows.size(); ++i) {
        const FlyoutRow& row = model_.rows[i];
        const double h = rowHeight(row);
        if (row.kind == FlyoutRow::Kind::Section) {
            const TextStyle s{.family = theme().font.family,
                              .size = 10.5,
                              .weight = PANGO_WEIGHT_BOLD,
                              .color = theme().colors.textMuted,
                              .letterSpacing = 0.6};
            p.drawText(b.x + kPad, y + 8.0, row.title, s, HAlign::Left, innerW);
            y += h;
            continue;
        }
        if (row.kind == FlyoutRow::Kind::Note) {
            const TextStyle n{.family = theme().font.family,
                              .size = 11.0,
                              .weight = PANGO_WEIGHT_NORMAL,
                              .color = toneColor(theme(), row.tone)};
            p.drawText(b.x + kPad + kGlyphCol, y + 4.0, row.title, n, HAlign::Left,
                       innerW - kGlyphCol);
            y += h;
            continue;
        }

        // Item: glyph | title / subtitle | buttons
        const double cy = y + (h / 2.0);
        if (row.busy) {
            drawSpinner(p, b.x + kPad + 9.0, cy, 6.5, now, theme().colors.primary);
        } else if (!row.glyph.empty()) {
            const TextStyle g{.family = theme().font.family,
                              .size = 16.0,
                              .weight = PANGO_WEIGHT_NORMAL,
                              .color = theme().colors.primary};
            p.drawText(b.x + kPad, cy - 10.0, row.glyph, g);
        }
        const double textX = b.x + kPad + kGlyphCol;
        const double textMax = std::max(40.0, innerW - kGlyphCol - buttonsWidth(row) - 4.0);
        const TextStyle t{.family = theme().font.family,
                          .size = 13.0,
                          .weight = PANGO_WEIGHT_NORMAL,
                          .color = theme().colors.text};
        if (row.subtitle.empty()) {
            p.drawText(textX, cy - 8.5, row.title, t, HAlign::Left, textMax);
        } else {
            p.drawText(textX, y + 5.0, row.title, t, HAlign::Left, textMax);
            const TextStyle st{.family = theme().font.family,
                               .size = 11.0,
                               .weight = PANGO_WEIGHT_NORMAL,
                               .color = toneColor(theme(), row.tone)};
            p.drawText(textX, y + 24.0, row.subtitle, st, HAlign::Left, textMax);
        }

        double bx = b.x + b.w - kPad;
        for (size_t a = row.actions.size(); a-- > 0;) {
            const FlyoutAction& act = row.actions[a];
            const double w = buttonWidth(act);
            bx -= w;
            const Rect br{bx, cy - (kButtonH / 2.0), w, kButtonH};
            bx -= kButtonGap;
            const bool hot = act.enabled && br.contains(hoverX_, hoverY_);
            p.fillRoundedRect(br, kButtonH / 2.0,
                              hot ? theme().colors.primary.withAlpha(0.28)
                                  : theme().colors.glassHover);
            p.strokeRoundedRect(br, kButtonH / 2.0, theme().colors.glassBorder, 1.0);
            const TextStyle bt{.family = theme().font.family,
                               .size = kButtonFont,
                               .weight = PANGO_WEIGHT_NORMAL,
                               .color =
                                   act.enabled ? theme().colors.text : theme().colors.textMuted};
            p.drawText(br.x + (br.w / 2.0), br.y + 4.0, act.label, bt, HAlign::Center);
            if (act.enabled) { hits_.push_back({.r = br, .row = i, .action = a}); }
        }
        y += h;
    }

    // Buttons were laid out right to left; keyboard order is reading order.
    std::ranges::sort(hits_, [](const Hit& l, const Hit& r) {
        return l.row != r.row ? l.row < r.row : l.action < r.action;
    });
    if (selected_ >= 0 && std::cmp_less(selected_, hits_.size())) {
        const Rect& r = hits_[static_cast<size_t>(selected_)].r;
        p.strokeRoundedRect({r.x - 2.0, r.y - 2.0, r.w + 4.0, r.h + 4.0}, (kButtonH / 2.0) + 2.0,
                            theme().colors.primary, 1.5);
    } else if (selected_ >= 0) {
        selected_ = hits_.empty() ? -1 : static_cast<int>(hits_.size()) - 1;
    }
}

bool ListFlyout::press(size_t row, size_t action) {
    if (row >= model_.rows.size() || action >= model_.rows[row].actions.size()) { return false; }
    const FlyoutAction act = model_.rows[row].actions[action];  // copy: run may rebuild
    if (!act.enabled) { return true; }
    if (act.run) { act.run(); }
    if (act.closes) { closeRequested_ = true; }
    return true;
}

bool ListFlyout::handleClick(double x, double y) {
    for (const Hit& h : hits_) {
        if (h.r.contains(x, y)) { return press(h.row, h.action); }
    }
    return contains(x, y);
}

bool ListFlyout::handleMotion(double x, double y) {
    hoverX_ = x;
    hoverY_ = y;
    return contains(x, y);
}

bool ListFlyout::handleKey(uint32_t keysym) {
    if (hits_.empty()) { return false; }
    const int count = static_cast<int>(hits_.size());
    const bool next = keysym == XKB_KEY_Down || keysym == XKB_KEY_Tab || keysym == XKB_KEY_Right;
    const bool prev =
        keysym == XKB_KEY_Up || keysym == XKB_KEY_ISO_Left_Tab || keysym == XKB_KEY_Left;
    if (next || prev) {
        if (selected_ < 0) {
            selected_ = prev ? count - 1 : 0;
        } else {
            selected_ = (selected_ + (prev ? -1 : 1) + count) % count;
        }
        return true;
    }
    if ((keysym == XKB_KEY_Return || keysym == XKB_KEY_KP_Enter || keysym == XKB_KEY_space) &&
        selected_ >= 0 && selected_ < count) {
        const Hit h = hits_[static_cast<size_t>(selected_)];
        return press(h.row, h.action);
    }
    return false;
}

bool ListFlyout::consumeCloseRequest() {
    return std::exchange(closeRequested_, false);
}

}  // namespace qypr
