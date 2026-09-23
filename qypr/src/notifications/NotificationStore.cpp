// NotificationStore.cpp - See the header for the design.

#include "notifications/NotificationStore.hpp"

#include <algorithm>
#include <array>

namespace qypr {

Color NotificationStore::accentFor(const theme::State& theme, uint64_t key, uint8_t urgency) {
    // Desktop Notifications spec: urgency hint levels (2 = critical).
    if (urgency >= 2) { return theme.colors.red; }
    const std::array<Color, 8> accents = {
        theme.colors.blue, theme.colors.green, theme.colors.mauve, theme.colors.peach,
        theme.colors.teal, theme.colors.sky,   theme.colors.pink,  theme.colors.yellow,
    };
    return accents.at(key % accents.size());
}

uint64_t NotificationStore::add(NotifyEvent event, bool sensitive, const theme::State& theme) {
    if (event.hints.transient) {
        return 0;  // volume OSDs and the like: never queued
    }
    if (event.summary.empty() && event.body.empty()) { return 0; }

    Notification n;
    n.daemonId = event.replacesId;
    n.postedAt = event.postedAt;
    n.app = std::move(event.app);
    n.title = std::move(event.summary);
    n.body = std::move(event.body);
    n.icon = std::move(event.icon);
    n.desktopEntry = std::move(event.hints.desktopEntry);
    n.urgency = event.hints.urgency;
    n.sensitive = sensitive;
    n.actions = std::move(event.actions);

    // replaces_id: update the existing card in place (same key, so the view
    // reconciles without re-animating).
    if (event.replacesId != 0) {
        for (auto& existing : notes_) {
            if (existing.daemonId == event.replacesId) {
                n.id = existing.id;
                n.accent = accentFor(theme, n.id, n.urgency);
                existing = std::move(n);
                return existing.id;
            }
        }
    }

    n.id = nextKey_++;
    n.accent = accentFor(theme, n.id, n.urgency);

    // The daemon's reply to this call carries the assigned notification id;
    // remember the call so attachDaemonId() can correlate it.
    if (event.haveCookie) {
        if (pending_.size() >= kMaxPending) {
            pending_.clear();  // stale, unanswered
        }
        pending_.push_back(
            PendingCall{.sender = std::move(event.sender), .cookie = event.cookie, .id = n.id});
    }

    notes_.push_back(std::move(n));
    while (notes_.size() > kMaxHeld) { notes_.erase(notes_.begin()); }
    return notes_.back().id;
}

bool NotificationStore::attachDaemonId(const ReturnEvent& r) {
    if (pending_.empty()) { return false; }
    auto it = std::ranges::find_if(pending_, [&](const PendingCall& pc) {
        return pc.cookie == r.replyCookie && pc.sender == r.destination;
    });
    if (it == pending_.end()) { return false; }
    const uint64_t key = it->id;
    pending_.erase(it);
    for (auto& n : notes_) {
        if (n.id == key) {
            n.daemonId = r.daemonId;
            break;
        }
    }
    return true;
}

bool NotificationStore::close(uint32_t daemonId, uint32_t reason) {
    // Expired popups (reason 1) stay — while locked, this stack is the user's
    // queue. Explicit dismissal or an app's CloseNotification removes the card.
    // Desktop Notifications spec: 2 = dismissed by the user, 3 = close call.
    if (reason != 2 && reason != 3) { return false; }
    const size_t before = notes_.size();
    std::erase_if(notes_, [daemonId](const Notification& n) { return n.daemonId == daemonId; });
    return notes_.size() != before;
}

bool NotificationStore::seed(std::vector<Notification> backlog, const theme::State& theme) {
    for (auto& n : backlog) {
        n.id = nextKey_++;
        n.accent = accentFor(theme, n.id, n.urgency);
        notes_.push_back(std::move(n));
    }
    while (notes_.size() > kMaxHeld) { notes_.erase(notes_.begin()); }
    return !backlog.empty();
}

}  // namespace qypr
