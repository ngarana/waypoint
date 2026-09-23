// NotificationParser.hpp - D-Bus message walking only.
//
// Converts monitored messages into typed events: no policy, no storage, no
// publication. Anything the parser cannot walk yields nullopt and the caller
// drops the message, exactly as the monitor facade did inline.

#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

struct sd_bus_message;

#include "notifications/NotificationPolicy.hpp"

namespace qypr {

struct NotifyEvent {
    std::string app;
    std::string icon;
    std::string summary;
    std::string body;
    uint32_t replacesId = 0;
    std::vector<std::pair<std::string, std::string>> actions;
    NotifyHints hints;     // incl. desktop-entry for click-to-launch
    int64_t postedAt = 0;  // capture time (ms)
    uint64_t cookie = 0;   // method-call cookie, for daemon-id correlation
    bool haveCookie = false;
    std::string sender;
};

struct ClosedEvent {
    uint32_t id = 0;
    uint32_t reason = 0;
};

struct ReturnEvent {
    uint64_t replyCookie = 0;
    std::string destination;
    uint32_t daemonId = 0;
};

// Notify(s app, u replaces_id, s icon, s summary, s body, as actions,
// a{sv} hints, i expire) — nullopt when the fixed head does not walk.
std::optional<NotifyEvent> parseNotify(sd_bus_message* m);
// NotificationClosed(u id, u reason) — nullopt when id is 0/unreadable.
std::optional<ClosedEvent> parseClosed(sd_bus_message* m);
// A method return carrying the daemon id — nullopt unless the signature is
// exactly "u" with a nonzero id (mirrors the facade's exact-type check).
std::optional<ReturnEvent> parseReturn(sd_bus_message* m);

}  // namespace qypr
