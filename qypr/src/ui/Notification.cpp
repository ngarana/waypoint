// Notification.cpp - The lock screen's notification chips.
//
// The chips follow the redesign mock (docs/mock/code.html): a footer band of
// small telemetry chips along the bottom-left, each one a status dot, an
// identity label and a value. They replace the old stack of glass cards —
// the row costs one line of height instead of four, and the chips read as
// part of the instrument panel instead of as a column of stickers.

#include "ui/Notification.hpp"

#include <algorithm>
#include <cctype>
#include <numeric>

#include "render/IconResolver.hpp"
#include "render/Painter.hpp"
#include "ui/Theme.hpp"

namespace qypr {

namespace {

// The chip's metrics, in the mock's units: a 6px dot, 12px of horizontal
// padding, 6px of vertical padding, 12px between chips.
constexpr double kDotRadius = 3.0;
constexpr double kDotGap = 8.0;
constexpr double kChipPadX = 12.0;
constexpr double kChipPadY = 6.0;
constexpr double kChipGap = 12.0;
constexpr double kChipRadius = 4.0;
constexpr double kDismissSize = 16.0;

constexpr double kLabelSize = 11.0;
constexpr double kValueSize = 10.0;
constexpr double kBodySize = 10.0;

// The chip band is two rows deep; the rest of the notifications become the
// overflow count.
constexpr size_t kMaxRows = 2;

constexpr double kLabelTracking = 1.4;
constexpr double kValueTracking = 0.8;

TextStyle labelStyle(const theme::State& theme) {
    return {theme.font.family, kLabelSize, PANGO_WEIGHT_MEDIUM, theme.colors.text, kLabelTracking};
}

TextStyle valueStyle(const theme::State& theme, const Color& accent) {
    return {theme.font.monoFamily, kValueSize, PANGO_WEIGHT_NORMAL, accent, kValueTracking};
}

TextStyle bodyStyle(const theme::State& theme) {
    return {theme.font.monoFamily, kBodySize, PANGO_WEIGHT_NORMAL, theme.colors.textMuted, 0.6};
}

TextStyle dismissStyle(const theme::State& theme, bool hovered) {
    return {theme.font.family, 13.0, PANGO_WEIGHT_NORMAL,
            hovered ? theme.colors.text : theme.colors.textMuted.withAlpha(0.7)};
}

std::string uppercase(std::string text) {
    for (char& c : text) {
        if (c >= 'a' && c <= 'z') { c = static_cast<char>(c - 'a' + 'A'); }
    }
    return text;
}

// The label is an identifier, not a sentence: spaces become underscores so
// "Claude Code" reads as CLAUDE_CODE, the way the mock names its daemons.
std::string identifier(const std::string& text) {
    std::string out;
    out.reserve(text.size());
    for (char c : text) {
        if (c == ' ') {
            out.push_back('_');
        } else if (static_cast<unsigned char>(c) < 0x20) {
            continue;
        } else {
            out.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(c))));
        }
    }
    return out;
}

}  // namespace

std::string NotificationView::chipLabel(const Notification& note) {
    if (note.sensitive) { return "NOTIFICATION:"; }
    const std::string id = identifier(note.app.empty() ? std::string("ALERT") : note.app);
    return id + ":";
}

std::string NotificationView::chipValue(const Notification& note) {
    if (note.sensitive) { return "MASKED"; }
    return uppercase(note.title.empty() ? std::string("NO SUBJECT") : note.title);
}

std::string NotificationView::chipBody(const Notification& note) {
    if (note.sensitive) { return "CONTENTS HIDDEN"; }
    return note.body;
}

void NotificationView::update(std::vector<Notification> notes) {
    std::unordered_set<uint64_t> incomingIds;
    incomingIds.reserve(notes.size());
    for (const auto& n : notes) { incomingIds.insert(n.id); }
    std::erase_if(dismissedIds_, [&](uint64_t id) { return !incomingIds.contains(id); });

    std::vector<Card> next;
    next.reserve(notes.size());
    for (auto& n : notes) {
        if (dismissedIds_.contains(n.id)) { continue; }
        auto it = std::ranges::find_if(cards_, [&](const Card& c) { return c.note.id == n.id; });
        if (it != cards_.end()) {
            Card c = *it;           // keep its animation state (no re-fade)
            c.note = std::move(n);  // refresh text/colour
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
}

NotificationView::ChipText NotificationView::chipText(const Card& c, Painter& p) const {
    const theme::State& t = theme();
    ChipText text;
    text.label = chipLabel(c.note);
    text.value = chipValue(c.note);
    text.body = chipBody(c.note);
    text.labelSize = p.measureText(text.label, labelStyle(t));
    text.valueSize = p.measureText(text.value, valueStyle(t, c.note.accent));
    text.bodySize = p.measureText(text.body, bodyStyle(t));
    return text;
}

double NotificationView::chipWidth(const ChipText& text, double progress) const {
    const double lineW = (kDotRadius * 2) + kDotGap + text.labelSize.w + 6 + text.valueSize.w;
    const double bodyW = (kDotRadius * 2) + kDotGap + text.bodySize.w;
    const double contentW = std::max(lineW, bodyW * progress);
    return (kChipPadX * 2) + contentW;
}

double NotificationView::chipHeight(const ChipText& text, double progress) const {
    const double lineH = std::max(text.labelSize.h, text.valueSize.h);
    return (kChipPadY * 2) + lineH + (2.0 + text.bodySize.h) * progress;
}

void NotificationView::drawChip(const Card& c, const ChipText& text, Painter& p, int64_t now,
                                const Rect& r) {
    const theme::State& t = theme();
    const double progress = clamp01(c.expandProgress.value(now));

    // Surface: the chip's own hairline and a whisper of fill. Hovering lifts
    // the border to the notification's accent, the way the mock does.
    const Color border = c.hovered ? c.note.accent.withAlpha(0.45) : t.colors.glassBorder;
    const Color fill = c.hovered ? c.note.accent.withAlpha(0.06) : t.colors.surface.withAlpha(0.22);
    p.fillRoundedRect(r, kChipRadius, fill);
    p.strokeRoundedRect(r, kChipRadius, border, 1);

    const double contentX = r.x + kChipPadX;
    const double cy = r.y + kChipPadY + (std::max(text.labelSize.h, text.valueSize.h) / 2.0);

    // Status dot.
    p.fillCircle(contentX + kDotRadius, cy, kDotRadius, c.note.accent);

    // Identity line: label, then the value in the accent colour.
    const double labelX = contentX + (kDotRadius * 2) + kDotGap;
    p.drawText(labelX, cy - (text.labelSize.h / 2.0), text.label, labelStyle(t), HAlign::Left);
    p.drawText(labelX + text.labelSize.w + 6, cy - (text.valueSize.h / 2.0), text.value,
               valueStyle(t, c.note.accent), HAlign::Left);

    // Body line: only an expanded chip shows it, and it is what makes the
    // chip grow upwards out of the foot line.
    if (progress > 0.01) {
        const double bodyY = r.y + kChipPadY + std::max(text.labelSize.h, text.valueSize.h) + 2.0;
        p.pushGroup();
        p.drawText(labelX, bodyY, text.body, bodyStyle(t), HAlign::Left);
        p.popGroupWithAlpha(progress);
    }

    // Dismiss affordance: a small × at the chip's right edge, on hover only.
    if (c.hovered) {
        const TextStyle style = dismissStyle(t, c.closeHovered);
        const Size size = p.measureText("×", style);
        p.drawText(r.x + r.w - kChipPadX - size.w, cy - (size.h / 2.0), "×", style, HAlign::Left);
    }
}

void NotificationView::draw(Painter& p, int64_t now, double left, double bottom, double maxWidth) {
    layout_.clear();
    if (cards_.empty()) { return; }

    const auto maxVisible = static_cast<size_t>(theme().notification.maxVisible);
    const size_t start = cards_.size() > maxVisible ? cards_.size() - maxVisible : 0;
    const size_t count = cards_.size() - start;

    // Measure every chip, then pack them into rows that fit `maxWidth`.
    std::vector<ChipText> texts;
    std::vector<double> widths;
    std::vector<double> heights;
    texts.reserve(count);
    widths.reserve(count);
    heights.reserve(count);

    std::vector<std::vector<size_t>> rows;
    std::vector<double> rowHeights;
    double rowWidth = 0.0;
    size_t overflow = 0;

    for (size_t i = 0; i < count; ++i) {
        const Card& c = cards_.at(start + i);
        const ChipText text = chipText(c, p);
        const double progress = clamp01(c.expandProgress.value(now));
        const double w = chipWidth(text, progress);
        const double h = chipHeight(text, progress);

        // The band is two rows deep; anything that does not fit becomes the
        // overflow count rather than a third row.
        if (!rows.empty() && (rowWidth + w) > maxWidth) {
            if (rows.size() >= kMaxRows) {
                ++overflow;
                continue;
            }
            rows.emplace_back();
            rowHeights.push_back(0.0);
            rowWidth = 0.0;
        }
        if (rows.empty()) {
            rows.emplace_back();
            rowHeights.push_back(0.0);
        }
        rows.back().push_back(i);
        rowHeights.back() = std::max(rowHeights.back(), h);
        rowWidth += w + kChipGap;
        texts.push_back(text);
        widths.push_back(w);
        heights.push_back(h);
    }

    // The block is bottom-anchored: the last row sits on the foot line and
    // any wrapped row grows upwards from it.
    double total = std::accumulate(rowHeights.begin(), rowHeights.end(), 0.0);
    if (rows.size() > 1) { total += kChipGap * static_cast<double>(rows.size() - 1); }

    double rowTop = bottom - total;
    for (size_t r = 0; r < rows.size(); ++r) {
        const double rowH = rowHeights[r];
        const double rowBottom = rowTop + rowH;
        double x = left;

        for (const size_t i : rows[r]) {
            const size_t idx = start + i;
            const Card& c = cards_.at(idx);
            const double appear = clamp01(c.appear.value(now));

            // A new chip slides in from the left while it fades.
            const Rect chip{x - ((1.0 - appear) * 12.0), rowBottom - heights.at(i), widths.at(i),
                            heights.at(i)};

            if (appear <= 0.01) {
                x += widths.at(i) + kChipGap;
                continue;
            }

            p.pushGroup();
            drawChip(c, texts.at(i), p, now, chip);
            p.popGroupWithAlpha(appear);

            HitBox box;
            box.index = idx;
            box.chip = chip;
            const Rect dismiss{chip.x + chip.w - kChipPadX - kDismissSize, chip.y + kChipPadY,
                               kDismissSize, kDismissSize};
            box.dismiss = dismiss;
            layout_.push_back(box);

            x += widths.at(i) + kChipGap;
        }
        rowTop += rowH + kChipGap;
    }

    // Overflow: a trailing chip that counts what did not fit.
    if (overflow > 0) {
        const std::string label = "+" + std::to_string(overflow);
        const TextStyle style{theme().font.monoFamily, kValueSize, PANGO_WEIGHT_NORMAL,
                              theme().colors.textMuted, kValueTracking};
        const Size size = p.measureText(label, style);
        const double w = (kChipPadX * 2) + size.w;
        const double h = (kChipPadY * 2) + size.h;
        const Rect chip{left, bottom - h, w, h};
        p.fillRoundedRect(chip, kChipRadius, theme().colors.surface.withAlpha(0.22));
        p.strokeRoundedRect(chip, kChipRadius, theme().colors.glassBorder, 1);
        p.drawText(chip.cx() - (size.w / 2.0), chip.cy() - (size.h / 2.0), label, style,
                   HAlign::Left);
    }
}

bool NotificationView::handlePress(double x, double y, int64_t now) {
    for (const auto& box : layout_) {
        if (!box.chip.contains(x, y) || box.index >= cards_.size()) { continue; }

        if (box.dismiss.contains(x, y)) {
            const Notification dismissed = cards_.at(box.index).note;
            dismissedIds_.insert(dismissed.id);
            cards_.erase(cards_.begin() +
                         static_cast<std::vector<Card>::difference_type>(box.index));
            layout_.clear();
            if (onDismiss_) { onDismiss_(dismissed); }
            return true;
        }

        // Keep the row readable: only one chip may be expanded at a time.
        for (size_t i = 0; i < cards_.size(); ++i) {
            if (i != box.index && cards_.at(i).expanded) {
                cards_.at(i).expanded = false;
                cards_.at(i).expandProgress.animateTo(0.0, theme().anim.medium, ease::inOutQuad);
            }
        }
        Card& c = cards_.at(box.index);
        c.expanded = !c.expanded;
        const double target = c.expanded ? 1.0 : 0.0;
        c.expandProgress.animateTo(target, theme().anim.medium, ease::inOutQuad);
        return true;
    }
    return false;
}

void NotificationView::updateHover(double x, double y, int64_t now) {
    (void)now;
    for (const auto& box : layout_) {
        if (box.index >= cards_.size()) { continue; }
        cards_.at(box.index).hovered = box.chip.contains(x, y);
        cards_.at(box.index).closeHovered =
            cards_.at(box.index).hovered && box.dismiss.contains(x, y);
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
                     .app = "Claude Code",
                     .title = "Task active // running tests",
                     .body = "",
                     .icon = "",
                     .accent = theme::kDefaultState.colors.pink},
        Notification{.id = 2,
                     .postedAt = 0,
                     .app = "SSH Tokyo-01",
                     .title = "Connected [42ms]",
                     .body = "",
                     .icon = "",
                     .accent = theme::kDefaultState.colors.green},
        Notification{.id = 3,
                     .postedAt = 0,
                     .app = "Calendar",
                     .title = "Team standup 10:30",
                     .body = "",
                     .icon = "",
                     .accent = theme::kDefaultState.colors.blue},
        Notification{.id = 4,
                     .postedAt = 0,
                     .app = "Daemon",
                     .title = "PID 4910 idle",
                     .body = "",
                     .icon = "",
                     .accent = theme::kDefaultState.colors.textMuted},
    };
}

}  // namespace qypr
