// PowerDialog.cpp

#include "ui/PowerDialog.hpp"

#include <cmath>
#include <cstdio>

#include "render/Painter.hpp"

namespace qypr {

// ---------------------------------------------------------------------------
// Geometry
// ---------------------------------------------------------------------------

// Card rect: right edge sits at anchoredX_, vertically centred on anchoredY_.
// We clamp so it never goes above the top of the screen.
static Rect makeCardRect(double anchoredX, double anchoredY, int /*w*/, int h) {
    constexpr double kCardW = PowerDialog::kCardW;
    constexpr double kCardH = PowerDialog::kCardH;
    constexpr double kGap = 10.0;  // gap between card right edge and pill left edge
    double x = anchoredX - kCardW - kGap;
    double y = anchoredY - kCardH / 2.0;
    // Keep card on screen vertically.
    if (y < 8.0) y = 8.0;
    if (y + kCardH > h - 8.0) y = h - 8.0 - kCardH;
    return {x, y, kCardW, kCardH};
}

Rect PowerDialog::cancelBtnRect(const Rect& card) const {
    const double btnW = (card.w - kPad * 2.0 - 8.0) / 2.0;
    return {card.x + kPad, card.y + card.h - kPad - kBtnH, btnW, kBtnH};
}

Rect PowerDialog::confirmBtnRect(const Rect& card) const {
    Rect c = cancelBtnRect(card);
    return {c.x + c.w + 8.0, c.y, c.w, kBtnH};
}

Rect PowerDialog::barRect(const Rect& card) const {
    // Bar sits just above the button row.
    Rect c = cancelBtnRect(card);
    return {card.x + kPad, c.y - kBarH - 6.0, card.w - kPad * 2.0, kBarH};
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

void PowerDialog::show(const std::string& icon, const std::string& title,
                       const std::string& confirmLabel, std::function<void()> onConfirm,
                       const Rect& anchorRect, double pillLeft) {
    icon_ = icon;
    title_ = title;
    confirmLabel_ = confirmLabel;
    onConfirm_ = std::move(onConfirm);

    // Right edge of the card = left edge of the pill.
    anchoredX_ = pillLeft;
    // Vertical centre = centre of the clicked action button.
    anchoredY_ = anchorRect.cy();

    visible_ = true;

    confirmHovered_ = false;
    cancelHovered_ = false;
    confirmScale_.set(1.0);
    cancelScale_.set(1.0);

    // Start countdown bar full (1.0) and drain to 0.
    barAnim_.set(1.0);
    if (autoConfirmMs > 0) barAnim_.animateTo(0.0, autoConfirmMs, ease::linear);

    fadeAnim_.animateTo(1.0, theme::anim::fast, ease::inOutQuad);
}

void PowerDialog::dismiss() {
    visible_ = false;
    fadeAnim_.animateTo(0.0, theme::anim::fast, ease::inOutQuad);
}

void PowerDialog::confirm() {
    if (onConfirm_) onConfirm_();
    dismiss();
}

// ---------------------------------------------------------------------------
// Input
// ---------------------------------------------------------------------------

bool PowerDialog::handlePress(double x, double y, int64_t /*now*/) {
    if (!active()) return false;
    const Rect& card = cardBounds_;
    Rect cancel = cancelBtnRect(card);
    Rect confirm = confirmBtnRect(card);

    if (confirm.contains(x, y)) {
        confirmScale_.animateTo(0.92, theme::anim::fast, ease::inOutQuad);
        this->confirm();
        return true;
    }
    if (cancel.contains(x, y)) {
        cancelScale_.animateTo(0.92, theme::anim::fast, ease::inOutQuad);
        dismiss();
        return true;
    }
    // Outside card → dismiss.
    if (!card.contains(x, y)) {
        dismiss();
        return true;
    }
    return true;  // consume clicks inside the card
}

void PowerDialog::updateHover(double x, double y, int64_t now) {
    if (!active()) return;
    const Rect& card = cardBounds_;
    Rect cancel = cancelBtnRect(card);
    Rect confirm = confirmBtnRect(card);

    bool nc = confirm.contains(x, y);
    bool nl = cancel.contains(x, y);

    if (nc != confirmHovered_) {
        confirmHovered_ = nc;
        confirmScale_.animateTo(nc ? 1.05 : 1.0, theme::anim::fast, ease::outBack);
    }
    if (nl != cancelHovered_) {
        cancelHovered_ = nl;
        cancelScale_.animateTo(nl ? 1.05 : 1.0, theme::anim::fast, ease::outBack);
    }
}

// ---------------------------------------------------------------------------
// Draw
// ---------------------------------------------------------------------------

void PowerDialog::draw(Painter& p, int w, int h, int64_t now) {
    if (!active()) return;

    const double alpha = clamp01(fadeAnim_.value(now));
    if (alpha < 0.001) return;

    // Auto-cancel: bar drained to zero and animation finished — dismiss without acting.
    if (autoConfirmMs > 0 && !barAnim_.active(now) && barAnim_.target() < 0.001) {
        barAnim_.set(1.0);  // prevent re-trigger
        dismiss();
        return;
    }

    p.pushGroup();

    // ── Card ─────────────────────────────────────────────────────────────
    Rect card = makeCardRect(anchoredX_, anchoredY_, w, h);
    cardBounds_ = card;

    // Glass card — no backdrop scrim, floats above the lock screen.
    p.fillRoundedRect(card, theme::radius::large, Color::fromHex("#e0181825"));
    p.strokeRoundedRect(card, theme::radius::large, theme::color::glassBorder, 1.0);

    // ── Header: icon + title on one line ─────────────────────────────────
    {
        const double lineY = card.y + kPad;
        const double iconSz = 15.0;
        TextStyle is{theme::font::iconFamily, iconSz, PANGO_WEIGHT_NORMAL, theme::color::primary};
        Size iconSzM = p.measureText(icon_, is);
        p.drawText(card.x + kPad, lineY, icon_, is, HAlign::Left);

        TextStyle ts{theme::font::family, 13.0, PANGO_WEIGHT_SEMIBOLD, theme::color::text};
        double titleX = card.x + kPad + iconSzM.w + 7.0;
        p.drawText(titleX, lineY, title_, ts, HAlign::Left, card.w - kPad * 2.0 - iconSzM.w - 7.0);
    }

    // ── Countdown bar ────────────────────────────────────────────────────
    if (autoConfirmMs > 0) {
        const double remaining = clamp01(barAnim_.value(now));
        const Rect bar = barRect(card);

        // Track (empty bar background).
        p.fillRoundedRect(bar, bar.h / 2.0, theme::color::glass);
        // Fill (remaining time).
        if (remaining > 0.005) {
            Rect fill{bar.x, bar.y, bar.w * remaining, bar.h};
            p.fillRoundedRect(fill, bar.h / 2.0, theme::color::primary);
        }
    }

    // ── Buttons ──────────────────────────────────────────────────────────
    const Rect cancelR = cancelBtnRect(card);
    const Rect confirmR = confirmBtnRect(card);

    auto drawBtn = [&](const Rect& r, bool hovered, double scale, const std::string& label,
                       bool isPrimary) {
        const double sx = r.cx(), sy = r.cy();
        Rect sr{sx - r.w * scale / 2.0, sy - r.h * scale / 2.0, r.w * scale, r.h * scale};

        Color bg, border, fg;
        if (isPrimary) {
            bg = hovered ? Color::fromHex("#cc89b4fa") : Color::fromHex("#7089b4fa");
            border = theme::color::primary.withAlpha(0.6);
            fg = Color::rgba(0.08, 0.08, 0.14, 1.0);
        } else {
            bg = hovered ? theme::color::glassHover : theme::color::glass;
            border = theme::color::glassBorder;
            fg = theme::color::textSubtle;
        }

        p.fillRoundedRect(sr, theme::radius::medium, bg);
        p.strokeRoundedRect(sr, theme::radius::medium, border, 1.0);

        TextStyle ls{theme::font::family, 12.0, PANGO_WEIGHT_SEMIBOLD, fg};
        Size lsz = p.measureText(label, ls);
        p.drawText(sr.cx() - lsz.w / 2.0, sr.cy() - lsz.h / 2.0, label, ls, HAlign::Left);
    };

    drawBtn(cancelR, cancelHovered_, clamp01(cancelScale_.value(now)), "Cancel", false);
    drawBtn(confirmR, confirmHovered_, clamp01(confirmScale_.value(now)), confirmLabel_, true);

    p.popGroupWithAlpha(alpha);
}

}  // namespace qypr
