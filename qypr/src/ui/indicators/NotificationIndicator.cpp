// NotificationIndicator.cpp - Notification centre implementation.
//
// A bell-with-count in the bar; clicking opens a Windows 11-style notification
// centre: a scroll of app-grouped cards. Each card shows the app, title, body,
// age and (on hover) a dismiss ×; multi-notification apps collapse under a
// count with an expand chevron. Clicking a card activates it — the app's own
// "default" action when it registered one, otherwise we launch/focus the app
// via its .desktop entry. All send-side actions go through NotificationActions
// (the monitor connection is receive-only); with no actions backend the panel
// is read-only.
#include "ui/indicators/NotificationIndicator.hpp"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include "notifications/NotificationActions.hpp"
#include "notifications/NotificationMonitor.hpp"
#include "render/Painter.hpp"
#include "system/DesktopIndex.hpp"
#include "system/DndState.hpp"
#include "ui/Notification.hpp"
#include "ui/Theme.hpp"
#include "ui/statusbar/IndicatorRegistry.hpp"

namespace qypr {

class EventLoop;

namespace {

constexpr const char* kBell = "󰂚";      // nf-md-bell
constexpr const char* kBellOff = "󰂛";   // nf-md-bell_off (DND)
constexpr const char* kBellRing = "󰂞";  // nf-md-bell_ring (empty-state)
constexpr const char* kClose = "󰅖";     // nf-md-close
constexpr const char* kChevronDown = "󰅀";
constexpr const char* kChevronRight = "󰅂";

constexpr double kPad = 14.0;
constexpr double kMenuW = 390.0;
constexpr double kHeaderH = 30.0;       // "Notifications" / "Clear all" row
constexpr double kGroupHeaderH = 26.0;  // per-app header (only when count > 1)
constexpr double kCardH = 64.0;         // base card (icon + title + body)
constexpr double kActionRowH = 30.0;    // extra height for a custom-action row
constexpr double kGap = 8.0;
constexpr double kIconTile = 38.0;
constexpr double kAccentBarW = 3.0;

std::string ageLabel(int64_t postedAt, int64_t now) {
    if (postedAt <= 0) { return ""; }
    const int64_t secs = (now - postedAt) / 1000;
    if (secs < 10) { return "now"; }
    if (secs < 60) { return std::to_string(secs) + "s"; }
    if (secs < 3600) { return std::to_string(secs / 60) + "m"; }
    if (secs < 86400) { return std::to_string(secs / 3600) + "h"; }
    return std::to_string(secs / 86400) + "d";
}

// The tile letter: first alphanumeric of the app name, uppercased.
std::string initialOf(const std::string& app) {
    for (const unsigned char c : app) {
        if (std::isalnum(c) != 0) { return std::string{1, static_cast<char>(std::toupper(c))}; }
    }
    return std::string{"!"};
}

// Custom actions are everything except the implicit "default" (which is the
// whole-card click, not a button).
bool hasCustomActions(const Notification& n) {
    return std::ranges::any_of(n.actions, [](const auto& a) { return a.first != "default"; });
}

// ── Group: notifications from the same app, newest-first ────────────────────
struct NotifyGroup {
    std::string app;
    Color accent = theme::color::primary;
    uint8_t urgency = 1;
    std::vector<const Notification*> notes;  // newest-first
};

std::vector<NotifyGroup> buildGroups(const std::vector<Notification>& notes) {
    std::vector<NotifyGroup> groups;
    std::unordered_map<std::string, size_t> appIndex;
    for (int i = static_cast<int>(notes.size()) - 1; i >= 0; --i) {
        const Notification& n = notes.at(static_cast<size_t>(i));
        const std::string key = n.app.empty() ? "System" : n.app;
        auto it = appIndex.find(key);
        if (it == appIndex.end()) {
            appIndex[key] = groups.size();
            groups.push_back(
                NotifyGroup{.app = key, .accent = n.accent, .urgency = n.urgency, .notes = {&n}});
        } else {
            auto& g = groups.at(it->second);
            g.notes.push_back(&n);
            g.urgency = std::max(g.urgency, n.urgency);
        }
    }
    return groups;
}

// One hit-testable row on screen. It carries everything an action needs, so
// input handling never dereferences a Notification* that may have changed
// between the draw that laid it out and the click that follows.
struct Item {
    enum class Kind : std::uint8_t { Header, Card };
    Kind kind;
    Rect bounds;
    Rect closeBtn;  // card dismiss / group clear-all (drawn on hover)
    Rect chevron;   // header expand/collapse toggle
    std::vector<Rect> actionBtns;
    std::vector<int> actionIdx;  // index into the note's full actions array
    // Card payload.
    uint32_t daemonId = 0;
    bool isNewest = false;  // only the newest note's actions are invokable
    std::string desktopEntry;
    std::string app;
    // Header payload.
    std::string groupApp;
    std::vector<uint32_t> groupIds;
};

class NotificationPopover : public DetailedPopover {
public:
    NotificationPopover(const NotificationMonitor* mon, NotificationActions* actions,
                        DesktopIndex* apps, EventLoop* loop)
        : mon_(mon),
          actions_(actions),
          apps_(apps),
          loop_(loop) {}

    // Offline preview: render from a fixed set instead of a live monitor.
    explicit NotificationPopover(std::vector<Notification> demo) : previewNotes_(std::move(demo)) {}

    double contentWidth() const override { return kMenuW; }

    // Transient like a real notification panel: fade away after a spell of no
    // interaction. Any hover/click/scroll over it resets the host's timer.
    int autoDismissMs() const override { return 8000; }

    double contentHeight() const override {
        const auto& notes = list();
        if (notes.empty()) { return kPad + kHeaderH + 96.0 + kPad; }
        const uint64_t newestId = notes.back().id;
        double h = kPad + kHeaderH;
        for (const auto& g : buildGroups(notes)) {
            const bool multi = g.notes.size() > 1;
            const bool expanded = multi && isExpanded(g.app);
            if (multi) { h += kGroupHeaderH + kGap; }
            const size_t shown = (!multi || expanded) ? g.notes.size() : 1;
            for (size_t i = 0; i < shown; ++i) {
                h += cardHeight(*g.notes.at(i), g.notes.at(i)->id == newestId) + kGap;
            }
        }
        return h + kPad - kGap;  // last card's trailing gap folds into bottom pad
    }

    void draw(Painter& p, int64_t now) override {
        auto b = getBounds();
        b.y += (growUp ? 1.0 : -1.0) * (1.0 - openProgress_.value(now)) * 6.0;

        // The standalone bar uses the same backdrop as the strip; the lock
        // screen keeps the notification panel's original glass card.
        if (!drawSharedBackdrop(p, b, theme::statusbar::popoverRadius)) {
            p.fillGlass(b, theme::statusbar::popoverRadius, theme::color::background,
                        theme::statusbar::panelSurfaceHover().withAlpha(0.6),
                        theme::style::mode == "solid");
        }

        items_.clear();
        clearAll_ = Rect{};  // zero geometry (NSDMIs are all 0)
        const auto& notes = list();

        // ── Header ─────────────────────────────────────────────────────────
        double y = b.y + kPad;
        const TextStyle head{.family = theme::font::family,
                             .size = 13.0,
                             .weight = PANGO_WEIGHT_BOLD,
                             .color = theme::color::text};
        p.drawText(b.x + kPad, y, "Notifications", head);
        if (!notes.empty() && actions_ != nullptr) {
            const bool hot = clearAll_.contains(hoverX_, hoverY_);
            const TextStyle ca{.family = theme::font::family,
                               .size = 11.0,
                               .weight = PANGO_WEIGHT_NORMAL,
                               .color = hot ? theme::color::text : theme::color::textSubtle};
            const Size sz = p.measureText("Clear all", ca);
            const double cx = b.x + b.w - kPad - sz.w;
            const Rect updated{.x = cx - 8.0, .y = y - 4.0, .w = sz.w + 16.0, .h = 22.0};
            clearAll_ = updated;
            if (clearAll_.contains(hoverX_, hoverY_)) {
                p.fillRoundedRectSource(clearAll_, 6.0,
                                        theme::statusbar::panelSurfaceHover().withAlpha(0.5));
            }
            p.drawText(cx, y, "Clear all", ca);
        }
        y += kHeaderH;

        if (notes.empty()) {
            drawEmptyState(p, b, y);
            return;
        }

        // The single newest notification is the only one whose actions can be
        // fired (swaync's LatestInvokeAction targets the most-recent one).
        const uint64_t newestId = notes.back().id;

        // ── Groups ─────────────────────────────────────────────────────────
        for (auto& g : buildGroups(notes)) {
            const bool multi = g.notes.size() > 1;
            const bool expanded = multi && isExpanded(g.app);
            const Color accent = g.urgency >= 2 ? theme::color::error : g.accent;

            if (multi) {
                drawGroupHeader(p, b, y, g, accent, expanded);
                y += kGroupHeaderH + kGap;
            }

            const size_t shown = (!multi || expanded) ? g.notes.size() : 1;
            for (size_t i = 0; i < shown; ++i) {
                const Notification& n = *g.notes.at(i);
                const bool isNewest = n.id == newestId;
                const double ch = cardHeight(n, isNewest);
                const Rect r{.x = b.x + kPad, .y = y, .w = b.w - (kPad * 2.0), .h = ch};
                drawCard(p, r, n, accent, now, isNewest);
                y += ch + kGap;
            }
        }
    }

    // ── Input ───────────────────────────────────────────────────────────────
    bool handleMotion(double x, double y) override {
        hoverX_ = x;
        hoverY_ = y;
        return contains(x, y);
    }
    bool handleDrag(double x, double y) override { return handleMotion(x, y); }

    bool consumeCloseRequest() override {
        const bool c = closeRequested_;
        closeRequested_ = false;
        return c;
    }

    bool handleClick(double x, double y) override {
        if (actions_ == nullptr) { return false; }

        if (clearAll_.contains(x, y)) {
            for (const auto& n : list()) {
                if (n.daemonId != 0) { actions_->close(n.daemonId); }
            }
            return true;
        }

        // Newest rows are drawn first; iterate forward (topmost wins on overlap
        // is irrelevant — rows never overlap).
        for (const auto& it : items_) {
            if (!it.bounds.contains(x, y)) { continue; }

            if (it.kind == Item::Kind::Header) {
                if (it.closeBtn.valid() && it.closeBtn.contains(x, y)) {
                    for (const uint32_t id : it.groupIds) {
                        if (id != 0) { actions_->close(id); }
                    }
                    groupExpanded_.erase(it.groupApp);
                    return true;
                }
                toggleExpanded(it.groupApp);  // chevron or anywhere on the header
                return true;
            }

            // Card.
            if (it.closeBtn.valid() && it.closeBtn.contains(x, y)) {
                if (it.daemonId != 0) { actions_->close(it.daemonId); }
                return true;
            }
            for (size_t k = 0; k < it.actionBtns.size(); ++k) {
                if (it.actionBtns.at(k).contains(x, y)) {
                    // Buttons are only laid out for the newest card (it.isNewest),
                    // the one swaync's LatestInvokeAction can target.
                    actions_->invokeLatestAction(static_cast<uint32_t>(it.actionIdx.at(k)));
                    requestClose();
                    return true;
                }
            }
            activate(it);  // click the card body
            return true;
        }
        return false;
    }

    // Right-click dismisses without launching: a card closes just that
    // notification, a group header closes the whole group, "Clear all" clears
    // everything — the quick "get this out of here" gesture.
    bool handleSecondaryClick(double x, double y) override {
        if (actions_ == nullptr) { return false; }
        if (clearAll_.contains(x, y)) {
            for (const auto& n : list()) {
                if (n.daemonId != 0) { actions_->close(n.daemonId); }
            }
            return true;
        }
        for (const auto& it : items_) {
            if (!it.bounds.contains(x, y)) { continue; }
            if (it.kind == Item::Kind::Header) {
                for (const uint32_t id : it.groupIds) {
                    if (id != 0) { actions_->close(id); }
                }
                groupExpanded_.erase(it.groupApp);
                return true;
            }
            if (it.daemonId != 0) { actions_->close(it.daemonId); }
            return true;
        }
        return true;  // swallow right-clicks inside the centre (never dismiss)
    }

private:
    static double cardHeight(const Notification& n, bool isNewest) {
        return kCardH + (isNewest && hasCustomActions(n) ? kActionRowH : 0.0);
    }

    void requestClose() { closeRequested_ = true; }

    // Click a card body: launch/focus the sending app, then clear the card and
    // close the centre (Windows behaviour). We launch by desktop entry rather
    // than firing the app's "default" action because launching works for every
    // card, whereas swaync can only fire an action on the newest notification.
    void activate(const Item& it) {
        const DesktopEntry* e = nullptr;
        if (apps_ != nullptr) {
            // The index is built on demand (it is a full .desktop directory
            // scan, kept off the startup path); this click is a demand for it.
            if (!apps_->loaded()) { apps_->load(); }
            if (!it.desktopEntry.empty()) { e = apps_->resolve(it.desktopEntry); }
            if (e == nullptr && !it.app.empty()) { e = apps_->resolve(it.app); }
        }
        if (e == nullptr) { return; }      // can't resolve the app — leave the card up
        if (loop_ == nullptr) { return; }  // lock screen: never launch
        launchDetached(*loop_, e->exec, e->terminal);
        if (it.daemonId != 0 && actions_ != nullptr) { actions_->close(it.daemonId); }
        requestClose();
    }

    static void drawEmptyState(Painter& p, const Rect& b, double y) {
        const TextStyle glyph{.family = theme::font::iconFamily,
                              .size = 34.0,
                              .weight = PANGO_WEIGHT_NORMAL,
                              .color = theme::color::textMuted};
        const Size gs = p.measureText(kBellRing, glyph);
        p.drawText(b.x + ((b.w - gs.w) / 2.0), y + 14.0, kBellRing, glyph);
        const TextStyle t{.family = theme::font::family,
                          .size = 12.0,
                          .weight = PANGO_WEIGHT_NORMAL,
                          .color = theme::color::textSubtle};
        const char* msg = "You're all caught up";
        const Size ts = p.measureText(msg, t);
        p.drawText(b.x + ((b.w - ts.w) / 2.0), y + 14.0 + gs.h + 8.0, msg, t);
    }

    void drawGroupHeader(Painter& p, const Rect& b, double y, const NotifyGroup& g,
                         const Color& accent, bool expanded) {
        const Rect r{.x = b.x + kPad, .y = y, .w = b.w - (kPad * 2.0), .h = kGroupHeaderH};
        const bool hot = r.contains(hoverX_, hoverY_);
        if (hot) {
            p.fillRoundedRectSource(r, 8.0, theme::statusbar::panelSurface().withAlpha(0.5));
        }

        const TextStyle appStyle{.family = theme::font::family,
                                 .size = 11.0,
                                 .weight = PANGO_WEIGHT_BOLD,
                                 .color = accent};
        p.drawText(r.x + 8.0, r.y + 6.0, g.app, appStyle, HAlign::Left, r.w - 90.0);
        const Size appSz = p.measureText(g.app, appStyle);
        const TextStyle cnt{.family = theme::font::family,
                            .size = 10.0,
                            .weight = PANGO_WEIGHT_NORMAL,
                            .color = theme::color::textMuted};
        p.drawText(r.x + 8.0 + std::min(appSz.w, r.w - 90.0) + 6.0, r.y + 7.0,
                   "(" + std::to_string(g.notes.size()) + ")", cnt);

        Item item{.kind = Item::Kind::Header};
        item.bounds = r;
        item.groupApp = g.app;
        for (const auto* n : g.notes) { item.groupIds.push_back(n->daemonId); }

        // Clear-group × (on hover).
        if (actions_ != nullptr) {
            const Rect xb{.x = r.x + r.w - 24.0, .y = r.y + 3.0, .w = 20.0, .h = 20.0};
            const bool xhot = xb.contains(hoverX_, hoverY_);
            if (hot || xhot) {
                const TextStyle s{.family = theme::font::iconFamily,
                                  .size = 10.0,
                                  .weight = PANGO_WEIGHT_NORMAL,
                                  .color = xhot ? theme::color::error : theme::color::textMuted};
                const Size gs = p.measureText(kClose, s);
                p.drawText(xb.x + ((xb.w - gs.w) / 2.0), xb.y + ((xb.h - gs.h) / 2.0), kClose, s);
            }
            item.closeBtn = xb;
        }
        // Expand/collapse chevron.
        const Rect ch{.x = r.x + r.w - 48.0, .y = r.y + 3.0, .w = 20.0, .h = 20.0};
        const TextStyle cs{.family = theme::font::iconFamily,
                           .size = 11.0,
                           .weight = PANGO_WEIGHT_NORMAL,
                           .color = ch.contains(hoverX_, hoverY_) ? theme::color::text
                                                                  : theme::color::textSubtle};
        const char* glyph = expanded ? kChevronDown : kChevronRight;
        const Size cgs = p.measureText(glyph, cs);
        p.drawText(ch.x + ((ch.w - cgs.w) / 2.0), ch.y + ((ch.h - cgs.h) / 2.0), glyph, cs);
        item.chevron = ch;

        items_.push_back(std::move(item));
    }

    void drawCard(Painter& p, const Rect& r, const Notification& n, const Color& accent,
                  int64_t now, bool isNewest) {
        const bool hot = r.contains(hoverX_, hoverY_);
        p.fillRoundedRectSource(r, 10.0,
                                hot ? theme::statusbar::panelSurfaceHover()
                                    : theme::statusbar::panelSurface());
        p.strokeRoundedRectSource(r, 10.0, theme::statusbar::panelSurfaceHover().withAlpha(0.5),
                                  1.0);
        // Accent spine.
        p.fillRoundedRect(Rect{.x = r.x, .y = r.y + 8.0, .w = kAccentBarW, .h = r.h - 16.0}, 1.5,
                          accent);

        // Icon tile with the app initial.
        const Rect tile{.x = r.x + 12.0, .y = r.y + 12.0, .w = kIconTile, .h = kIconTile};
        p.fillRoundedRect(tile, 9.0, accent);
        const TextStyle init{.family = theme::font::family,
                             .size = 17.0,
                             .weight = PANGO_WEIGHT_BOLD,
                             .color = theme::color::background};
        const std::string letter = initialOf(n.app.empty() ? n.title : n.app);
        const Size ls = p.measureText(letter, init);
        p.drawText(tile.x + ((tile.w - ls.w) / 2.0), tile.y + ((tile.h - ls.h) / 2.0), letter,
                   init);

        const double textX = tile.x + tile.w + 12.0;
        const double rightPad = 12.0;
        const double textW = r.x + r.w - rightPad - textX;

        Item item{.kind = Item::Kind::Card};
        item.bounds = r;
        item.daemonId = n.daemonId;
        item.isNewest = isNewest;
        item.desktopEntry = n.desktopEntry;
        item.app = n.app;

        // Age (top-right), swapped for a × on hover.
        double titleW = textW - 24.0;
        if (hot && actions_ != nullptr && n.daemonId != 0) {
            const Rect xb{.x = r.x + r.w - 30.0, .y = r.y + 10.0, .w = 20.0, .h = 20.0};
            const bool xhot = xb.contains(hoverX_, hoverY_);
            const TextStyle s{.family = theme::font::iconFamily,
                              .size = 11.0,
                              .weight = PANGO_WEIGHT_NORMAL,
                              .color = xhot ? theme::color::error : theme::color::textSubtle};
            const Size gs = p.measureText(kClose, s);
            p.drawText(xb.x + ((xb.w - gs.w) / 2.0), xb.y + ((xb.h - gs.h) / 2.0), kClose, s);
            item.closeBtn = xb;
        } else {
            const std::string age = ageLabel(n.postedAt, now);
            if (!age.empty()) {
                const TextStyle as{.family = theme::font::family,
                                   .size = 10.0,
                                   .weight = PANGO_WEIGHT_NORMAL,
                                   .color = theme::color::textMuted};
                const Size asz = p.measureText(age, as);
                p.drawText(r.x + r.w - rightPad - asz.w, r.y + 13.0, age, as);
                titleW = textW - asz.w - 8.0;
            }
        }

        // Title + body (single line each, ellipsised).
        std::string title = n.title;
        if (title.empty()) { title = n.app.empty() ? "Notification" : n.app; }
        const TextStyle ts{.family = theme::font::family,
                           .size = 12.0,
                           .weight = PANGO_WEIGHT_BOLD,
                           .color = theme::color::text};
        p.drawText(textX, r.y + 12.0, title, ts, HAlign::Left, titleW);
        if (!n.body.empty()) {
            const TextStyle bs{.family = theme::font::family,
                               .size = 11.0,
                               .weight = PANGO_WEIGHT_NORMAL,
                               .color = theme::color::textSubtle};
            p.drawText(textX, r.y + 32.0, n.body, bs, HAlign::Left, textW);
        }

        // Custom-action row — only on the newest card, since swaync can fire an
        // action on the latest notification alone. The action index passed to
        // swaync counts *non-default* actions only (verified: with actions
        // [default, reply, mute], LatestInvokeAction(1) fires "mute"), so it is
        // exactly this loop's position `k`.
        if (isNewest) {
            std::vector<std::string> labels;  // non-default action labels, in order
            for (const auto& a : n.actions) {
                if (a.first != "default") { labels.push_back(a.second); }
            }
            if (!labels.empty()) {
                const double bx0 = textX;
                const double rowW = r.x + r.w - rightPad - bx0;
                const double gap = 6.0;
                const auto count = static_cast<double>(labels.size());
                const double bw = (rowW - ((count - 1.0) * gap)) / count;
                const double by = r.y + kCardH - 4.0;
                double bx = bx0;
                for (size_t k = 0; k < labels.size(); ++k) {
                    const Rect btn{.x = bx, .y = by, .w = bw, .h = 22.0};
                    const bool bhot = btn.contains(hoverX_, hoverY_);
                    p.fillRoundedRect(btn, 6.0,
                                      bhot ? theme::statusbar::panelSurfaceHover()
                                           : theme::color::background);
                    p.strokeRoundedRectSource(
                        btn, 6.0, theme::statusbar::panelSurfaceHover().withAlpha(0.6), 1.0);
                    const TextStyle bt{.family = theme::font::family,
                                       .size = 10.0,
                                       .weight = PANGO_WEIGHT_BOLD,
                                       .color =
                                           bhot ? theme::color::text : theme::color::textSubtle};
                    const Size bsz = p.measureText(labels.at(k), bt);
                    p.drawText(btn.x + ((btn.w - bsz.w) / 2.0), btn.y + ((btn.h - bsz.h) / 2.0),
                               labels.at(k), bt, HAlign::Left, bw - 8.0);
                    item.actionBtns.push_back(btn);
                    item.actionIdx.push_back(static_cast<int>(k));
                    bx += bw + gap;
                }
            }
        }

        items_.push_back(std::move(item));
    }

    const std::vector<Notification>& list() const {
        if (!previewNotes_.empty()) { return previewNotes_; }
        static const std::vector<Notification> kNone;
        return mon_ != nullptr ? mon_->notifications() : kNone;
    }

    bool isExpanded(const std::string& app) const {
        auto it = groupExpanded_.find(app);
        return it != groupExpanded_.end() && it->second;
    }
    void toggleExpanded(const std::string& app) {
        auto it = groupExpanded_.find(app);
        if (it == groupExpanded_.end()) {
            groupExpanded_.emplace(app, true);
        } else {
            it->second = !it->second;
        }
    }

    const NotificationMonitor* mon_ = nullptr;
    NotificationActions* actions_ = nullptr;
    DesktopIndex* apps_ = nullptr;
    EventLoop* loop_ = nullptr;  // pidfd reaping (I3); null in preview + lock

    std::vector<Item> items_;  // rebuilt every draw for hit-testing
    Rect clearAll_;
    double hoverX_ = -1, hoverY_ = -1;
    bool closeRequested_ = false;
    std::unordered_map<std::string, bool> groupExpanded_;
    std::vector<Notification> previewNotes_;  // non-empty only in --preview
};

}  // namespace

// Offline preview of the notification centre with demo data (see Notification.hpp
// demoNotifications()). Anchored top-right like the live popover.
void previewNotificationCentre(Painter& p, double anchorX, double anchorY, bool growUp,
                               bool backdropEnabled, double backdropAlpha) {
    NotificationPopover pop(demoNotifications());
    pop.setBackdrop(backdropEnabled, backdropAlpha);
    pop.anchorX = anchorX;
    pop.anchorY = anchorY;
    pop.growUp = growUp;
    pop.draw(p, nowMs());
}

NotificationIndicator::NotificationIndicator(const SystemBackends& backends)
    : StatusIndicator("notifications", Zone::Right, 650),
      monitor_(backends.notifications),
      actions_(backends.notificationActions),
      dnd_(backends.dnd),
      apps_(backends.desktopIndex),
      loop_(backends.loop) {
    // No monitor (qypr-lock) → the applet does not exist at all.
    visible = monitor_ != nullptr;
}

size_t NotificationIndicator::count() const {
    return monitor_ != nullptr ? monitor_->notifications().size() : 0;
}

std::string NotificationIndicator::icon() const {
    // While DND is on the bell reads as muted — the count still shows, because
    // suppressed notifications are still waiting for you.
    return (dnd_ != nullptr && dnd_->enabled()) ? kBellOff : kBell;
}

std::string NotificationIndicator::themedIcon() const {
    // notification-* symbolic from the active icon theme.
    return (dnd_ != nullptr && dnd_->enabled()) ? "notification-alert-symbolic"
                                                : "notification-new-symbolic";
}

std::string NotificationIndicator::label() const {
    const size_t n = count();
    return n == 0 ? "" : std::to_string(n);
}

std::string NotificationIndicator::tooltip() const {
    const size_t n = count();
    if (n == 0) { return "No notifications"; }
    return std::to_string(n) + (n == 1 ? " notification" : " notifications");
}

Color NotificationIndicator::iconColor() const {
    if (dnd_ != nullptr && dnd_->enabled()) { return theme::color::textSubtle; }
    return count() > 0 ? theme::color::primary : theme::color::textSubtle;
}

double NotificationIndicator::measureWidth(Painter& p) {
    // Measure the icon glyph.
    double w = 0;
    // drawSurfaceTinted takes a non-const surface; this use is read-only.
    // NOLINTNEXTLINE(misc-const-correctness)
    cairo_surface_t* themed = displayIconSurface();
    if (themed != nullptr) {
        w = theme::statusbar::symbolicIconSize;
    } else {
        const std::string ic = icon();
        if (!ic.empty()) {
            const TextStyle iconStyle{.family = theme::font::iconFamily,
                                      .size = theme::statusbar::iconSize,
                                      .weight = PANGO_WEIGHT_NORMAL,
                                      .color = iconColor()};
            w = p.measureText(ic, iconStyle).w;
        }
    }
    // Superscript count extends slightly past the icon's right edge.
    const size_t n = count();
    if (n > 0) {
        const TextStyle supSt{.family = theme::font::family,
                              .size = kSuperscriptSize,
                              .weight = PANGO_WEIGHT_NORMAL,
                              .color = theme::color::primary};
        const std::string countStr = std::to_string(n);
        const double supW = p.measureText(countStr, supSt).w;
        w += kSuperscriptOffsetX + supW;
    }
    return w + (2 * 8.0);  // kSidePad from base class
}

void NotificationIndicator::draw(Painter& p, int64_t now) {
    // Delegate hover background and focus ring to the base class, but we handle
    // the content layout ourselves (bell icon + superscript count badge).
    if (!visible) { return; }

    const double alpha = hoverAlpha_.value(now);
    const double scale = hoverScale_.value(now);

    // Hover background pill
    if (alpha > 0.01) {
        const Color bg = theme::color::surfaceHover.withAlpha(alpha * 0.5);
        Rect hoverRect = bounds;
        hoverRect.y += 2.0;
        hoverRect.h -= 4.0;
        p.fillRoundedRect(hoverRect, 8.0, bg);
    }

    // Focus ring
    if (focused) {
        Rect focusRect = bounds;
        focusRect.y += 1.0;
        focusRect.h -= 2.0;
        p.strokeRoundedRect(focusRect, 8.0, theme::color::primary, 1.5);
    }

    // Icon
    cairo_surface_t* themedSurf = displayIconSurface();
    const std::string ic = icon();
    const double iconPx = theme::statusbar::symbolicIconSize * (scale > 1.001 ? scale : 1.0);
    const double shadowA = theme::effects::shadowOpacity;
    const double shadowOff = theme::effects::shadowOffset;

    // Measure icon width for centering.
    double iconW = 0;
    if (themedSurf != nullptr) {
        iconW = iconPx;
    } else if (!ic.empty()) {
        const TextStyle iconStyle{.family = theme::font::iconFamily,
                                  .size = theme::statusbar::iconSize,
                                  .weight = PANGO_WEIGHT_NORMAL,
                                  .color = iconColor()};
        iconW = p.measureText(ic, iconStyle).w;
    }

    // Account for superscript width when centering.
    const size_t n = count();
    double extraW = 0;
    if (n > 0) {
        const TextStyle supSt{.family = theme::font::family,
                              .size = kSuperscriptSize,
                              .weight = PANGO_WEIGHT_NORMAL,
                              .color = theme::color::primary};
        const std::string countStr = std::to_string(n);
        extraW = kSuperscriptOffsetX + p.measureText(countStr, supSt).w;
    }

    const double x = bounds.x + ((bounds.w - iconW - extraW) / 2.0);
    const double iconY = bounds.y + ((bounds.h - iconPx) / 2.0);

    if (themedSurf != nullptr) {
        p.drawSurfaceTinted(themedSurf, Rect{.x = x, .y = iconY, .w = iconPx, .h = iconPx},
                            iconColor());
    } else if (!ic.empty()) {
        TextStyle iconStyle{.family = theme::font::iconFamily,
                            .size = theme::statusbar::iconSize,
                            .weight = PANGO_WEIGHT_NORMAL,
                            .color = iconColor()};
        if (scale > 1.001) { iconStyle.size *= scale; }
        p.drawTextShadowed(x, iconY, ic, iconStyle, HAlign::Left, shadowA, shadowOff);
    }

    // Superscript count badge (top-right of icon).
    if (n > 0) {
        const TextStyle supSt{.family = theme::font::family,
                              .size = kSuperscriptSize,
                              .weight = PANGO_WEIGHT_BOLD,
                              .color = theme::color::primary};
        const std::string countStr = std::to_string(n);
        const double sx = x + iconW + kSuperscriptOffsetX;
        const double sy = iconY + kSuperscriptOffsetY;
        p.drawText(sx, sy, countStr, supSt, HAlign::Left);
    }
}

void NotificationIndicator::onBackendUpdate() {
    if (monitor_ != nullptr) { visible = true; }  // count/label re-read on draw
}

std::unique_ptr<DetailedPopover> NotificationIndicator::createDetailedView() {
    return std::make_unique<NotificationPopover>(monitor_, actions_, apps_, loop_);
}

REGISTER_INDICATOR("notifications", Zone::Right, 650, NotificationIndicator)

}  // namespace qypr
