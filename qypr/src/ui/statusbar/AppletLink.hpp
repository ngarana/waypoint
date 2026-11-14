// AppletLink.hpp - A merged flyout's way into its tray applet's own menu.
//
// wifi and bluetooth fold the applet they claim (nm-applet, blueman) into
// their flyout rather than keeping its menu on right click, so one click
// reaches everything the module offers: the flyout ends with "More from
// nm-applet ›", which swaps the flyout for the applet's dbusmenu
// (TrayAppBinding::appletMenu). That menu's first row leads back.
//
// The row exists only while the applet runs (label() resolves it on every
// call), so it never appears on the lock screen, which gets no tray backend.
#pragma once

#include <functional>
#include <string>

#include "core/Types.hpp"

namespace qypr {

class Painter;
namespace theme {
struct State;
}

struct AppletLink {
    std::function<std::string()> label;  // "More from nm-applet"; "" = no row
    std::function<void()> open;          // ask the bar for the applet's menu

    static constexpr double kHeight = 42.0;  // divider + row

    [[nodiscard]] std::string text() const { return label ? label() : std::string(); }
    [[nodiscard]] double height() const { return text().empty() ? 0.0 : kHeight; }
    // Width the row needs (0 without a row).
    [[nodiscard]] double width() const;
    // Draw the row along the bottom of `card`, `pad` inside its edges; the
    // clickable rect, or an empty one when there is no row.
    Rect draw(Painter& p, const theme::State& t, const Rect& card, double pad, double hoverX,
              double hoverY) const;
};

}  // namespace qypr
