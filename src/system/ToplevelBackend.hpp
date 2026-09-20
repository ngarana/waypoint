// ToplevelBackend.hpp - The focused window via wlr-foreign-toplevel-management.
//
// Tracks open toplevels and exposes the *activated* (focused) one's app id and
// title for an "active window" bar widget. Push only: the compositor streams
// title/app_id/state/closed events on the existing display fd.
//
// Protocol note: this is wlr-foreign-toplevel-management (widely supported —
// Hyprland, Sway, all wlroots compositors), used because it is the only broadly
// available protocol that carries per-window *focus* state. The standard
// ext-foreign-toplevel-list-v1 lists windows but has no activated/focus state,
// so it cannot answer "which window is focused". Still no per-WM IPC.
//
// Like WorkspaceBackend, it binds its own registry on the host wl_display, so
// the same class serves the lockscreen and the future standalone qypr-bar.
#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

struct wl_display;
struct wl_registry;
struct wl_seat;
struct wl_callback;
struct zwlr_foreign_toplevel_manager_v1;
struct zwlr_foreign_toplevel_handle_v1;

namespace qypr {

// One open toplevel, in stable arrival order. `id` is a small monotonic key the
// UI hit-tests against (the wl handle pointer is not exposed).
struct ToplevelWindow {
    uint64_t id = 0;
    std::string appId;
    std::string title;
    bool active = false;
    bool minimized = false;

    bool operator==(const ToplevelWindow&) const = default;
};

struct ToplevelSnapshot {
    bool available = false;  // the compositor exposes the protocol
    bool hasActive = false;  // some toplevel is currently activated
    std::string appId;       // the *active* window's app id / title (kept for the
    std::string title;       // ActiveWindowIndicator, which only wants the focused one)
    std::vector<ToplevelWindow> windows;  // every toplevel (taskbar), arrival order

    bool operator==(const ToplevelSnapshot&) const = default;
};

// Per-handle state (public for the C listener trampolines).
struct TlHandle {
    class ToplevelBackend* backend = nullptr;
    zwlr_foreign_toplevel_handle_v1* handle = nullptr;
    uint64_t id = 0;
    std::string appId;
    std::string title;
    bool active = false;
    bool minimized = false;
};

class ToplevelBackend {
public:
    ToplevelBackend() = default;
    ~ToplevelBackend();

    ToplevelBackend(const ToplevelBackend&) = delete;
    ToplevelBackend& operator=(const ToplevelBackend&) = delete;

    // Bind the protocol on the given display. Non-blocking: the manager (if
    // the compositor offers it) is bound from the registry callback and the
    // initial toplevel set arrives as push events; a wl_display sync reports a
    // missing protocol for diagnostics.
    void start(wl_display* display);

    const ToplevelSnapshot& snapshot() const { return snap_; }
    void setOnChange(std::function<void()> cb) { onChange_ = std::move(cb); }

    // --- Actions (taskbar). No-op when the id is unknown, or (for activate)
    // when the compositor exposes no seat. Each flushes the display. ---
    void activate(uint64_t id);        // focus/raise the window
    void close(uint64_t id);           // ask the app to close it
    void toggleMinimize(uint64_t id);  // (un)minimize

    // --- C listener trampolines (public; not for external use) ---
    void onRegistryGlobal(wl_registry*, uint32_t name, const char* iface, uint32_t version);
    void onManagerToplevel(zwlr_foreign_toplevel_handle_v1* h);
    static void onHandleTitle(TlHandle*, const char* title);
    static void onHandleAppId(TlHandle*, const char* appId);
    static void onHandleState(TlHandle*, const uint32_t* states, size_t n);
    void onHandleDone(TlHandle*);
    void onHandleClosed(TlHandle*);
    void onSyncDone(wl_callback* cb, uint32_t time);

private:
    void rebuildAndNotify();
    TlHandle* find(uint64_t id) const;

    wl_display* display_ = nullptr;
    wl_registry* registry_ = nullptr;
    wl_callback* syncCallback_ = nullptr;
    zwlr_foreign_toplevel_manager_v1* manager_ = nullptr;
    wl_seat* seat_ = nullptr;  // for activate(); bound from the registry
    std::vector<std::unique_ptr<TlHandle>> handles_;
    uint64_t nextId_ = 1;
    ToplevelSnapshot snap_;
    std::function<void()> onChange_;
};

}  // namespace qypr
