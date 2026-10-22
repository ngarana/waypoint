// ConfirmPopover.cpp

#include "ui/ConfirmPopover.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "render/Painter.hpp"

namespace qypr {

// ---------------------------------------------------------------------------
// Geometry
// ---------------------------------------------------------------------------

// Card rect: right edge sits at anchoredX_, vertically centred on anchoredY_.
// We clamp so it never goes above the top of the screen.
namespace {

Rect makeCardRect(double anchoredX, double anchoredY, int /*w*/, int h, const theme::State& theme) {
    const auto& c = theme.lock.confirm;
    double x = anchoredX - c.cardW - c.anchorGap;
    double y = anchoredY - c.cardH / 2.0;
    // Keep card on screen vertically.
    y = std::max(y, c.edgeInset);
    if (y + c.cardH > h - c.edgeInset) y = h - c.edgeInset - c.cardH;
    return {x, y, c.cardW, c.cardH};
}

}  // namespace

Rect ConfirmPopover::cancelBtnRect(const Rect& card) const {
    const auto& c = theme().lock.confirm;
    const double btnW = (card.w - (c.pad * 2.0) - c.buttonGap) / 2.0;
    return {card.x + c.pad, card.y + card.h - c.pad - c.buttonHeight, btnW, c.buttonHeight};
}

Rect ConfirmPopover::confirmBtnRect(const Rect& card) const {
    const auto& c = theme().lock.confirm;
    Rect cancel = cancelBtnRect(card);
    return {cancel.x + cancel.w + c.buttonGap, cancel.y, cancel.w, c.buttonHeight};
}

Rect ConfirmPopover::barRect(const Rect& card) const {
    // Bar sits just above the button row.
    const auto& c = theme().lock.confirm;
    Rect cancel = cancelBtnRect(card);
    return {card.x + c.pad, cancel.y - c.barHeight - c.barGap, card.w - (c.pad * 2.0), c.barHeight};
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

void ConfirmPopover::show(const std::string& icon, const std::string& title,
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
    if (autoDismissMs > 0) barAnim_.animateTo(0.0, autoDismissMs, ease::linear);

    fadeAnim_.animateTo(1.0, theme().anim.fast, ease::inOutQuad);
}

void ConfirmPopover::dismiss() {
    visible_ = false;
    fadeAnim_.animateTo(0.0, theme().anim.fast, ease::inOutQuad);
}

void ConfirmPopover::confirm() {
    if (onConfirm_) onConfirm_();
    dismiss();
}

// ---------------------------------------------------------------------------
// Input
// ---------------------------------------------------------------------------

bool ConfirmPopover::handlePress(double x, double y, int64_t /*now*/) {
    if (!active()) return false;
    const Rect& card = cardBounds_;
    Rect cancel = cancelBtnRect(card);
    Rect confirm = confirmBtnRect(card);

    if (confirm.contains(x, y)) {
        confirmScale_.animateTo(0.92, theme().anim.fast, ease::inOutQuad);
        this->confirm();
        return true;
    }
    if (cancel.contains(x, y)) {
        cancelScale_.animateTo(0.92, theme().anim.fast, ease::inOutQuad);
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

void ConfirmPopover::updateHover(double x, double y, int64_t now) {
    if (!active()) return;
    const Rect& card = cardBounds_;
    Rect cancel = cancelBtnRect(card);
    Rect confirm = confirmBtnRect(card);

    bool nc = confirm.contains(x, y);
    bool nl = cancel.contains(x, y);

    if (nc != confirmHovered_) {
        confirmHovered_ = nc;
        confirmScale_.animateTo(nc ? 1.05 : 1.0, theme().anim.fast, ease::outBack);
    }
    if (nl != cancelHovered_) {
        cancelHovered_ = nl;
        cancelScale_.animateTo(nl ? 1.05 : 1.0, theme().anim.fast, ease::outBack);
    }
}

// ---------------------------------------------------------------------------
// Draw
// ---------------------------------------------------------------------------

void ConfirmPopover::draw(Painter& p, int w, int h, int64_t now) {
    if (!active()) return;

    const double alpha = clamp01(fadeAnim_.value(now));
    if (alpha < 0.001) return;

    // Auto-cancel: bar drained to zero and animation finished — dismiss without acting.
    if (autoDismissMs > 0 && !barAnim_.active(now) && barAnim_.target() < 0.001) {
        barAnim_.set(1.0);  // prevent re-trigger
        dismiss();
        return;
    }

    p.pushGroup();

    // ── Card ─────────────────────────────────────────────────────────────
    const auto& c = theme().lock.confirm;
    Rect card = makeCardRect(anchoredX_, anchoredY_, w, h, theme());
    cardBounds_ = card;

    // Glass card — no backdrop scrim, floats above the lock screen.
    p.fillRoundedRect(card, theme().radius.large, theme().colors.glass);
    p.strokeRoundedRect(card, theme().radius.large, theme().colors.glassBorder, 1.0);

    // ── Header: icon + title on one line ─────────────────────────────────
    {
        const double lineY = card.y + c.pad;
        const auto iconSz = static_cast<double>(theme().font.size);
        TextStyle is{theme().font.iconFamily, iconSz, PANGO_WEIGHT_NORMAL, theme().colors.primary};
        Size iconSzM = p.measureText(icon_, is);
        p.drawText(card.x + c.pad, lineY, icon_, is, HAlign::Left);

        TextStyle ts{theme().font.family, static_cast<double>(theme().font.sizeBody),
                     PANGO_WEIGHT_SEMIBOLD, theme().colors.text};
        double titleX = card.x + c.pad + iconSzM.w + c.titleGap;
        p.drawText(titleX, lineY, title_, ts, HAlign::Left,
                   card.w - (c.pad * 2.0) - iconSzM.w - c.titleGap);
    }

    // ── Countdown bar ────────────────────────────────────────────────────
    if (autoDismissMs > 0) {
        const double remaining = clamp01(barAnim_.value(now));
        const Rect bar = barRect(card);

        // Track (empty bar background).
        p.fillRoundedRect(bar, bar.h / 2.0, theme().colors.glass);
        // Fill (remaining time).
        if (remaining > 0.005) {
            Rect fill{bar.x, bar.y, bar.w * remaining, bar.h};
            p.fillRoundedRect(fill, bar.h / 2.0, theme().colors.primary);
        }
    }

    // ── Buttons ──────────────────────────────────────────────────────────
    const Rect cancelR = cancelBtnRect(card);
    const Rect confirmR = confirmBtnRect(card);

    auto drawBtn = [&](const Rect& r, bool hovered, double scale, const std::string& label,
                       bool isPrimary) {
        const double sx = r.cx();
        const double sy = r.cy();
        Rect sr{sx - r.w * scale / 2.0, sy - r.h * scale / 2.0, r.w * scale, r.h * scale};

        Color bg;
        Color border;
        Color fg;
        if (isPrimary) {
            bg = theme().colors.primary.withAlpha(hovered ? 0.80 : 0.44);
            border = theme().colors.primary.withAlpha(0.6);
            fg = theme().colors.background;
        } else {
            bg = hovered ? theme().colors.glassHover : theme().colors.glass;
            border = theme().colors.glassBorder;
            fg = theme().colors.textSubtle;
        }

        p.fillRoundedRect(sr, theme().radius.medium, bg);
        p.strokeRoundedRect(sr, theme().radius.medium, border, 1.0);

        TextStyle ls{theme().font.family, static_cast<double>(theme().font.sizeMeta),
                     PANGO_WEIGHT_SEMIBOLD, fg};
        Size lsz = p.measureText(label, ls);
        p.drawText(sr.cx() - lsz.w / 2.0, sr.cy() - lsz.h / 2.0, label, ls, HAlign::Left);
    };

    drawBtn(cancelR, cancelHovered_, clamp01(cancelScale_.value(now)), "Cancel", false);
    drawBtn(confirmR, confirmHovered_, clamp01(confirmScale_.value(now)), confirmLabel_, true);

    p.popGroupWithAlpha(alpha);
}

}  // namespace qypr
