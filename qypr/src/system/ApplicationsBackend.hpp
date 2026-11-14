// ApplicationsBackend.hpp - Applications on the session bus.
//
// Two app-level protocols that don't involve a tray icon:
//
//   - com.canonical.Unity.LauncherEntry: apps broadcast an `Update` signal
//     carrying their unread count, a progress fraction, an urgency flag and a
//     quicklist (a dbusmenu on the sender). Telegram, Thunderbird, Element,
//     Signal, Electron and Chromium apps, Firefox downloads and many more send
//     it; Plasma's task manager and GNOME's docks read it. Some apps only send
//     it while `com.canonical.Unity` is owned, so the backend asks for that
//     name (without replacing or queueing behind an existing owner, and
//     letting a shell that starts later take it over).
//   - org.freedesktop.Application: a DBusActivatable app is started or raised
//     with Activate / ActivateAction on its own bus name instead of a spawn.
//
// The entries are pushed: nothing is polled, and an entry goes away when the
// app that sent it leaves the bus.
#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

struct sd_bus_message;
struct sd_bus_slot;
#include <systemd/sd-bus.h>  // sd_bus_error is a typedef here, not a struct

namespace qypr {

class EventLoop;
class SystemBus;
struct DesktopEntry;
struct DesktopAction;

// One app's launcher state, keyed by the desktop id it named.
struct LauncherEntry {
    std::string desktopId;  // "org.telegram.desktop" (from application://….desktop)
    std::string sender;     // unique name that sent it; the quicklist lives there
    int64_t count = 0;
    bool countVisible = false;
    double progress = 0.0;  // 0..1
    bool progressVisible = false;
    bool urgent = false;
    std::string quicklist;  // dbusmenu object path on `sender`, "" for none

    [[nodiscard]] int64_t badge() const { return countVisible && count > 0 ? count : 0; }
    // -1 when no progress is shown.
    [[nodiscard]] double shownProgress() const;
    bool operator==(const LauncherEntry&) const = default;
};

// The properties one Update signal carried; absent ones keep their value.
struct LauncherUpdate {
    std::optional<int64_t> count;
    std::optional<bool> countVisible;
    std::optional<double> progress;
    std::optional<bool> progressVisible;
    std::optional<bool> urgent;
    std::optional<std::string> quicklist;
};

class ApplicationsBackend {
public:
    ApplicationsBackend(EventLoop& loop, SystemBus& sessionBus);
    ~ApplicationsBackend();

    ApplicationsBackend(const ApplicationsBackend&) = delete;
    ApplicationsBackend& operator=(const ApplicationsBackend&) = delete;
    ApplicationsBackend(ApplicationsBackend&&) = delete;
    ApplicationsBackend& operator=(ApplicationsBackend&&) = delete;

    bool start();
    [[nodiscard]] const std::vector<LauncherEntry>& launcherEntries() const { return entries_; }
    void setOnChange(std::function<void()> cb) { onChange_ = std::move(cb); }

    // Start or raise the app: Activate on its bus name when it is
    // DBusActivatable (falling back to its Exec if that call fails), its Exec
    // otherwise. False when there is nothing to run.
    bool activate(const DesktopEntry& app);
    // One of the app's desktop actions, the same way.
    bool activateAction(const DesktopEntry& app, const DesktopAction& action);

    // --- pure helpers (static; unit-tested without a bus) ---
    // "application://org.telegram.desktop.desktop" → "org.telegram.desktop";
    // "" for anything else.
    static std::string desktopIdFromUri(const std::string& uri);
    // org.freedesktop.Application object path for a desktop id: '/' + id with
    // '.' → '/' and '-' → '_' ("org.gnome.Maps" → "/org/gnome/Maps").
    static std::string objectPathFor(const std::string& desktopId);
    // Apply one Update from `sender`. An entry left with nothing to show (no
    // badge, progress, urgency or quicklist) is removed.
    static void applyUpdate(std::vector<LauncherEntry>& entries, const std::string& sender,
                            const std::string& desktopId, const LauncherUpdate& update);
    // Forget every entry `sender` published (it left the bus).
    static bool dropSender(std::vector<LauncherEntry>& entries, const std::string& sender);

private:
    static int onUpdate(sd_bus_message* m, void* userdata, sd_bus_error* err);
    static int onNameOwnerChanged(sd_bus_message* m, void* userdata, sd_bus_error* err);
    static int onActivateReply(sd_bus_message* m, void* userdata, sd_bus_error* err);
    void watchSender(const std::string& sender);
    void changed();
    bool callActivate(const DesktopEntry& app, const DesktopAction* action);

    EventLoop& loop_;
    SystemBus& bus_;
    sd_bus_slot* updateSlot_ = nullptr;
    sd_bus_slot* nameSlot_ = nullptr;
    // NameOwnerChanged for each sender that has entries, so its entries go
    // when it exits.
    std::vector<std::pair<std::string, sd_bus_slot*>> senderSlots_;
    std::vector<LauncherEntry> entries_;
    std::function<void()> onChange_;
};

}  // namespace qypr
