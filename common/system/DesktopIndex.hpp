// DesktopIndex.hpp - Freedesktop .desktop application index for the launcher.
//
// Scans the XDG application directories once, parses each entry, and answers
// case-insensitive search queries. Parsing is in pure static helpers so it
// unit-tests without a real filesystem. Launching a result is a user-initiated
// one-shot spawn (decision D1) — see launchDetached().
#pragma once

#include <string>
#include <vector>

namespace qypr {

class EventLoop;

// One `[Desktop Action <id>]` group listed in the entry's `Actions=` key: a
// verb the app offers besides its main command ("New Window", "Compose").
struct DesktopAction {
    std::string id;    // the group's <id> (what ActivateAction takes)
    std::string name;  // display label
    std::string exec;  // field codes stripped; may be "" for D-Bus-only actions
    std::string icon;
    bool operator==(const DesktopAction&) const = default;
};

struct DesktopEntry {
    std::string id;    // desktop-file id (basename without ".desktop")
    std::string name;  // display name
    std::string exec;  // command, field codes (%f/%U/…) stripped
    std::string icon;  // freedesktop icon name ("" if none)
    bool terminal = false;
    // Extra searchable metadata (waylaunch searches these too).
    std::string categories;
    std::string comment;
    std::string genericName;
    std::string desktopPath;  // source .desktop file (for "reveal in files")
    // Lowercased "name genericName comment categories", built once at parse
    // time so search() is a single find() per entry instead of re-lowercasing
    // four fields on every keystroke (waylaunch's optimisation).
    std::string searchKey;
    // The app's own verbs, in `Actions=` order (groups not listed there, or
    // without a Name, are ignored as the spec says).
    std::vector<DesktopAction> actions;
    // `DBusActivatable=true`: the app implements org.freedesktop.Application
    // on the bus name equal to its id, so it can be activated without a spawn.
    bool dbusActivatable = false;
    // `StartupWMClass`: the window class / Wayland app id its windows carry
    // when that differs from the id (browser web apps: crx_<app-id>).
    std::string startupWMClass;
};

class DesktopIndex {
public:
    // Scan $XDG_DATA_HOME + $XDG_DATA_DIRS "/applications" (with sensible
    // fallbacks). Later duplicates by desktop-file id are ignored (earlier dirs
    // win, per the spec). Idempotent — clears and rebuilds.
    //
    // Call this at the point the user asks for the index (opening the launcher,
    // clicking a notification), not at startup: the scan reads every .desktop
    // file in the XDG data dirs, which on a cold boot is real disk I/O, and it
    // used to sit in front of the bar's first frame for a feature nobody had
    // requested yet. loaded() lets a caller skip a redundant rescan.
    void load();

    // True once the directory scan has run.
    [[nodiscard]] bool loaded() const { return loaded_; }

    [[nodiscard]] const std::vector<DesktopEntry>& entries() const { return entries_; }

    // Override the scan roots (empty = XDG defaults). waylaunch passes its
    // full list here (custom paths plus the GNOME/MATE extras).
    void setSearchPaths(std::vector<std::string> paths) { searchPaths_ = std::move(paths); }

    // Entries matching `query` (case-insensitive) over the precomputed
    // searchKey: prefix matches first, then substring, each alphabetical
    // (entries stay name-sorted). Empty query → all. One find() per entry.
    [[nodiscard]] std::vector<const DesktopEntry*> search(const std::string& query) const;

    // Best-effort resolve an app to a launchable entry, for click-to-launch from
    // a notification. `key` is the notification's `desktop-entry` hint or its
    // app-name. Matched (case-insensitively) as: exact desktop-file id, then the
    // id's trailing component (org.mozilla.firefox → firefox), then exact display
    // name, then name prefix. Null when nothing matches.
    [[nodiscard]] const DesktopEntry* resolve(const std::string& key) const;

    // The entry a window belongs to: exact id, then StartupWMClass, then
    // resolve(). Windows report their Wayland app id / X11 class, which is
    // the desktop id for well-behaved apps and the StartupWMClass otherwise.
    [[nodiscard]] const DesktopEntry* resolveWindow(const std::string& appId) const;

    // The entry with exactly this desktop-file id (case-insensitive).
    [[nodiscard]] const DesktopEntry* byId(const std::string& id) const;

    // --- pure helpers (static; unit-tested without the filesystem) ---
    // Parse one .desktop file body. False (skip) when it is NoDisplay/Hidden,
    // not Type=Application, or has no Name/Exec.
    static bool parseEntry(const std::string& body, DesktopEntry& out);
    // Strip Exec field codes (%f %F %u %U %i %c %k %v %m) and surrounding space.
    static std::string cleanExec(const std::string& exec);

private:
    std::vector<DesktopEntry> entries_;
    std::vector<std::string> searchPaths_;  // empty = XDG defaults
    bool loaded_ = false;
};

// Launch `cmd` via `sh -c` in a new session (no controlling terminal),
// reaped through `loop` (no fork, no zombie — fork()+double-fork is unsound
// in multithreaded processes, QL-6). `terminal` wraps the command in the
// user's $TERMINAL when set. User-initiated only (D1); false when there is
// no loop to reap through or the spawn failed.
bool launchDetached(EventLoop& loop, const std::string& cmd, bool terminal);

}  // namespace qypr
