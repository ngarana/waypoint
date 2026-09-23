// NotificationStore.hpp - Notification card storage (bus-free).
//
// Owns the card list, local identity keys, and daemon-id correlation. Pure:
// callers drive it with parsed events and read back the list. Accents are
// applied at insert from the owner's theme (never globals).

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "notifications/NotificationParser.hpp"
#include "ui/Notification.hpp"
#include "ui/Theme.hpp"

namespace qypr {

class NotificationStore {
public:
    static constexpr size_t kMaxHeld = 8;      // buffer beyond what the view shows
    static constexpr size_t kMaxPending = 32;  // unanswered Notify calls to remember

    // Queue a parsed event. Transient events and empty title+body cards never
    // queue (returns 0); replacesId updates the card in place keeping its
    // local id; otherwise appends with a fresh id and trims to kMaxHeld.
    // `sensitive` is the facade-OR of the policy verdict and the hint fold;
    // accents come from the passed theme (never globals).
    uint64_t add(NotifyEvent event, bool sensitive, const theme::State& theme);
    // Attach a daemon id to the card whose call produced `replyCookie` from
    // `destination`. Wrong cookie/sender: ignored. False when nothing matched.
    bool attachDaemonId(const ReturnEvent& r);
    // Remove the card with `daemonId`, but only for explicit dismissal (2) or
    // app close calls (3); expired popups stay queued. Unknown id: no-op.
    bool close(uint32_t daemonId, uint32_t reason);
    // Seed pre-lock backlog records, oldest first: fresh local ids, accents
    // from the passed theme, trimmed to capacity. True when cards were added.
    bool seed(std::vector<Notification> backlog, const theme::State& theme);
    const std::vector<Notification>& notes() const { return notes_; }
    // Test seam for indicator tests: direct card injection bypassing id
    // assignment (mirrors the old monitor notes_ reach-in).
    std::vector<Notification>& testNotes() { return notes_; }
    bool hasPending() const { return !pending_.empty(); }

    // Accent palette cycled per card (Catppuccin Mocha); critical is always
    // red. Reads the owner's live theme (never globals).
    static Color accentFor(const theme::State& theme, uint64_t key, uint8_t urgency);

private:
    struct PendingCall {
        std::string sender;
        uint64_t cookie = 0;
        uint64_t id = 0;
    };

    std::vector<Notification> notes_;
    uint64_t nextKey_ = 1;  // local card identity (view reconciliation)
    std::vector<PendingCall> pending_;
};

}  // namespace qypr
