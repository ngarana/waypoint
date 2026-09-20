// MenuPopover.cpp - Generic dbusmenu renderer implementation.
#include "ui/statusbar/MenuPopover.hpp"

#include "render/Painter.hpp"
#include "system/DbusMenuBackend.hpp"
#include "ui/Theme.hpp"

namespace qypr {

namespace {
constexpr double kMenuW = 300.0;
constexpr double kRowH = 30.0;
constexpr double kSepH = 9.0;
constexpr double kHeaderH = 28.0;
constexpr double kPad = 6.0;
constexpr double kLeftCol = 24.0;  // check/radio gutter
constexpr const char* kCheck = "✓";
constexpr const char* kRadioOn = "●";
constexpr const char* kSubmenu = "›";
}  // namespace

MenuPopover::MenuPopover(DbusMenuBackend* backend, std::string service, std::string menuPath,
                         std::vector<MenuNode> root, std::string title)
    : backend_(backend),
      service_(std::move(service)),
      menuPath_(std::move(menuPath)) {
    stack_.push_back({0, std::move(title), std::move(root)});
}

double MenuPopover::contentWidth() const {
    return kMenuW;
}

double MenuPopover::rowsHeight() const {
    double h = 0;
    for (const auto& it : cur().items) {
        if (!it.visible) continue;
        h += it.separator ? kSepH : kRowH;
    }
    return h;
}

double MenuPopover::contentHeight() const {
    double h = kPad * 2 + rowsHeight();
    if (stack_.size() > 1) h += kHeaderH;
    if (rowsHeight() <= 0) h += kRowH;  // "(empty)" line
    return h;
}

void MenuPopover::draw(Painter& p, int64_t now) {
    Rect b = getBounds();
    b.y += (growUp ? 1.0 : -1.0) * (1.0 - openProgress_.value(now)) * 6.0;
    if (!drawSharedBackdrop(p, b, theme::statusbar::popoverRadius))
        p.fillRoundedRectSource(b, theme::statusbar::popoverRadius,
                                theme::statusbar::panelSurface());

    hits_.clear();
    double y = b.y + kPad;

    // Back header when drilled into a submenu.
    if (stack_.size() > 1) {
        const Rect hr{b.x + kPad, y, b.w - kPad * 2, kHeaderH};
        if (hr.contains(hoverX_, hoverY_)) p.fillRoundedRect(hr, 6.0, theme::color::glassHover);
        TextStyle cs{theme::font::family, 15.0, PANGO_WEIGHT_NORMAL, theme::color::primary};
        p.drawText(hr.x + 6.0, y + (kHeaderH - 17.0) / 2.0, "‹", cs);  // back chevron
        TextStyle bs{theme::font::family, 12.0, PANGO_WEIGHT_BOLD, theme::color::text};
        p.drawText(hr.x + 22.0, y + (kHeaderH - 14.0) / 2.0, cur().title, bs);
        hits_.push_back({hr, Hit::Back, 0, ""});
        // Divider under the header.
        p.fillRect({b.x + kPad, y + kHeaderH - 1.0, b.w - kPad * 2, 1.0},
                   theme::color::glassBorder);
        y += kHeaderH;
    }

    if (rowsHeight() <= 0) {
        TextStyle es{theme::font::family, 12.0, PANGO_WEIGHT_NORMAL, theme::color::textSubtle};
        p.drawText(b.x + kPad + kLeftCol, y + (kRowH - 14.0) / 2.0, "(empty)", es);
        return;
    }

    for (const auto& it : cur().items) {
        if (!it.visible) continue;
        if (it.separator) {
            p.fillRect({b.x + kPad + 4.0, y + kSepH / 2.0, b.w - kPad * 2 - 8.0, 1.0},
                       theme::color::glassBorder);
            y += kSepH;
            continue;
        }

        const Rect row{b.x + kPad, y, b.w - kPad * 2, kRowH};
        const bool hot = it.enabled && row.contains(hoverX_, hoverY_);
        if (hot) p.fillRoundedRect(row, 6.0, theme::color::glassHover);

        const Color fg = it.enabled ? theme::color::text : theme::color::textSubtle.withAlpha(0.5);

        // Toggle marker in the left gutter.
        if (it.toggleType == "checkmark" && it.toggleState == 1) {
            TextStyle ts{theme::font::family, 12.0, PANGO_WEIGHT_BOLD, theme::color::primary};
            p.drawText(row.x + 6.0, y + (kRowH - 14.0) / 2.0, kCheck, ts);
        } else if (it.toggleType == "radio" && it.toggleState == 1) {
            TextStyle ts{theme::font::family, 12.0, PANGO_WEIGHT_NORMAL, theme::color::primary};
            p.drawText(row.x + 6.0, y + (kRowH - 14.0) / 2.0, kRadioOn, ts);
        }

        TextStyle ls{theme::font::family, 12.0, PANGO_WEIGHT_NORMAL, fg};
        const double labelMax = row.w - kLeftCol - (it.hasSubmenu ? 20.0 : 8.0);
        p.drawText(row.x + kLeftCol, y + (kRowH - 14.0) / 2.0, it.label, ls, HAlign::Left,
                   labelMax);

        if (it.hasSubmenu) {
            TextStyle ss{theme::font::family, 15.0, PANGO_WEIGHT_NORMAL, theme::color::textSubtle};
            p.drawText(row.x + row.w - 16.0, y + (kRowH - 17.0) / 2.0, kSubmenu, ss);
        }

        if (it.enabled) {
            hits_.push_back({row, it.hasSubmenu ? Hit::Submenu : Hit::Leaf, it.id, it.label});
        }
        y += kRowH;
    }
}

bool MenuPopover::handleClick(double x, double y) {
    for (const auto& h : hits_) {
        if (!h.r.contains(x, y)) continue;
        switch (h.kind) {
            case Hit::Back:
                if (stack_.size() > 1) stack_.pop_back();
                return true;
            case Hit::Submenu:
                stack_.push_back({h.id, h.label, backend_->fetch(service_, menuPath_, h.id)});
                return true;
            case Hit::Leaf:
                backend_->clicked(service_, menuPath_, h.id);
                closeRequested_ = true;
                return true;
        }
    }
    return false;
}

bool MenuPopover::handleDrag(double x, double y) {
    hoverX_ = x;
    hoverY_ = y;
    return false;
}

bool MenuPopover::consumeCloseRequest() {
    const bool c = closeRequested_;
    closeRequested_ = false;
    return c;
}

}  // namespace qypr
