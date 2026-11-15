// AppStatus.hpp - One status record per application.
//
// An app reports its state through several channels that know nothing of each
// other: notifications (the daemon), a Unity LauncherEntry (unread count,
// progress, urgency, quicklist), and its windows (wlr-foreign-toplevel). Each
// names the app differently — a desktop-entry hint, an application:// URI, a
// Wayland app id or StartupWMClass — so buildAppStatus() resolves every name
// through the desktop index and merges what refers to the same .desktop file
// into one AppStatus. Without an index (or for an app it doesn't know) the
// lowercased raw name is the key, so the merge degrades to exact matches.
//
// notification-apps shows the apps with something to say; whatsapp and email
// are the same record filtered by their claims (StatusArea), so an app shown
// by one of them is never shown twice (docs/APPLICATION_STATUS.md).
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace qypr {

class DesktopIndex;
struct LauncherEntry;
struct Notification;
struct ToplevelWindow;

struct AppStatus {
    std::string key;         // lowercase identity: the desktop id when known
    std::string desktopId;   // "" when no .desktop entry matched
    std::string name;        // display name
    std::string icon;        // the entry's Icon, else the first notification icon
    std::string categories;  // the entry's Categories ("Network;Email;")
    // Every lowercased name the channels used for this app (desktop-entry
    // hints, notification app names, window app ids, launcher ids): what
    // claim patterns are matched against.
    std::vector<std::string> aliases;

    // Notifications.
    int notifications = 0;
    bool critical = false;
    int64_t latestTime = 0;
    std::string latestTitle;
    std::string latestBody;
    std::vector<uint32_t> daemonIds;

    // LauncherEntry.
    int64_t badge = 0;     // the app's own count (unread messages)
    double progress = -1;  // 0..1, -1 for none
    bool urgent = false;
    std::string menuService;  // quicklist (dbusmenu) on this bus name …
    std::string menuPath;     // … at this path

    // Open windows (toplevel ids, arrival order).
    std::vector<uint64_t> windows;

    // Notifications, a badge, urgency or progress: something to say.
    [[nodiscard]] bool hasNews() const;
    // The number on the badge: the app's own count when it sends one,
    // otherwise its notifications.
    [[nodiscard]] int64_t count() const { return badge > 0 ? badge : notifications; }
    // A claim pattern (StatusArea::patternMatches syntax, or
    // "category:<Name>" for a desktop-entry category).
    [[nodiscard]] bool matches(const std::string& pattern) const;
    [[nodiscard]] bool matchesAny(const std::vector<std::string>& patterns) const;
};

struct AppStatusInputs {
    const std::vector<Notification>* notifications = nullptr;
    const std::vector<LauncherEntry>* launcher = nullptr;
    const std::vector<ToplevelWindow>* windows = nullptr;
    const DesktopIndex* index = nullptr;  // null or unloaded: raw names only
};

// Apps with news first, newest notification first; apps that only have
// windows after them, in window order.
std::vector<AppStatus> buildAppStatus(const AppStatusInputs& in);

}  // namespace qypr
