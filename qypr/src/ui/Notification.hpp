// Notification.hpp - Windows 11-style lock-screen notification cards.
//
// Pure view + interaction: holds the current set of notifications and paints
// them as a stack of glass cards anchored bottom-left. The data is pushed in
// via update() (live: NotificationMonitor; previews: demoNotifications());
// this class never fetches it.

#pragma once

#include <cstdint>
#include <functional>
#include <unordered_set>
#include <utility>
#include <vector>

#include "core/Types.hpp"
#include "ui/Theme.hpp"

namespace qypr {

class Painter;

// One notification. `id` is a stable key (assigned by the source) used to
// reconcile cards across refreshes so they don't re-animate. `icon` is an
// optional glyph; when empty the app's first letter is drawn in the coloured
// tile (font-safe, no icon-pack dependency).
struct Notification {
    uint64_t id = 0;
    int64_t postedAt = 0;  // capture time (ms)
    std::string app;       // source name, e.g. "Calendar"
    std::string title;     // heading (falls back to app when empty)
    std::string body;      // detail text (single line, ellipsised)
    std::string icon;      // optional glyph for the tile
    Color accent = theme::kDefaultState.colors.primary;
    uint32_t daemonId = 0;   // id assigned by the notification daemon (0 = not yet known)
    uint8_t urgency = 1;     // freedesktop urgency hint: 0 low, 1 normal, 2 critical
    bool sensitive = false;  // true if the notification contains sensitive content
    std::vector<std::pair<std::string, std::string>> actions;
    // freedesktop `desktop-entry` hint: the .desktop id of the sending app — the
    // reliable key for launching/focusing it when a card is clicked ("" if the
    // app did not send it; we then fall back to matching by app name). Kept last
    // so the positional aggregate inits in demoNotifications() stay valid.
    std::string desktopEntry;
};

class NotificationView : public theme::ThemeAware {
public:
    using DismissHandler = std::function<void(const Notification&)>;

    // Reconcile the visible set with `notes` by id: keep existing cards (no
    // re-animation), fade in new ones, drop the rest.
    void update(std::vector<Notification> notes);
    bool active() const { return !cards_.empty(); }
    void setOnDismiss(DismissHandler handler) { onDismiss_ = std::move(handler); }

    // Paint the stack so its bottom edge sits at `bottom`, left edge at `left`.
    void draw(Painter& p, int64_t now, double left, double bottom, double maxWidth);

    // Click a card to expand/collapse it; the close affordance dismisses it.
    // Returns true if a card consumed the press.
    bool handlePress(double x, double y, int64_t now);
    void updateHover(double x, double y, int64_t now);
    void clearHover(int64_t now);
    bool animating(int64_t now) const;

private:
    struct Card {
        Notification note;
        Animated appear{0};
        Animated expandProgress{0.0};
        bool hovered = false;
        bool closeHovered = false;
        bool expanded = false;
    };

    // Per-card height at a given width (text may wrap to one line only).
    static double cardHeight(const theme::State& theme, const Card& c, Painter& p, double w,
                             int64_t now);
    static void drawCard(const theme::State& theme, Card& c, Painter& p, int64_t now,
                         const Rect& r);

    std::vector<Card> cards_;
    // A local dismissal is held until the source removes the id. This closes
    // the view immediately and prevents the monitor's next refresh from
    // resurrecting the card while CloseNotification is still in flight.
    std::unordered_set<uint64_t> dismissedIds_;
    DismissHandler onDismiss_;
    // Hit-test cache rebuilt each draw (pointer handlers have no Painter).
    std::vector<std::pair<size_t, Rect>> layout_;
};

// Sample cards for --preview renders only; the real lock screen shows live
// captures (or nothing) — never fake data.
std::vector<Notification> demoNotifications();

}  // namespace qypr
