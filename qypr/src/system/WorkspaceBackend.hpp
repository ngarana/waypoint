// WorkspaceBackend.hpp - Compositor workspaces via the ext-workspace-v1 protocol.
//
// General-purpose, compositor-agnostic: ext-workspace-v1 is a standard Wayland
// protocol (Hyprland, Sway, river, …) — never a per-WM IPC (no hyprctl). Push
// only: the compositor streams workspace create/name/state/remove events on the
// existing display fd; there is no polling. `activate()` switches workspace
// (transactional: activate the handle, then commit the manager).
//
// This is a *bar* widget backend. It binds its own registry on the host's
// wl_display, so the same class serves both the lockscreen and the future
// standalone qypr-bar (each just passes its own display).
#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

struct wl_display;
struct wl_registry;
struct wl_callback;
struct ext_workspace_manager_v1;
struct ext_workspace_handle_v1;

namespace qypr {

struct WorkspaceInfo {
    std::string name;
    bool active = false;
    bool urgent = false;

    bool operator==(const WorkspaceInfo&) const = default;
};

struct WorkspaceSnapshot {
    bool available = false;                 // the compositor exposes ext-workspace-v1
    std::vector<WorkspaceInfo> workspaces;  // display order, hidden ones filtered out

    bool operator==(const WorkspaceSnapshot&) const = default;
};

// Per-handle state (public only so the C listener trampolines can reach it).
struct WsHandle {
    class WorkspaceBackend* backend = nullptr;
    ext_workspace_handle_v1* handle = nullptr;
    std::string name;
    uint32_t coord = 0;
    bool hasCoord = false;
    bool active = false;
    bool urgent = false;
    bool hidden = false;
};

class WorkspaceBackend {
public:
    WorkspaceBackend() = default;
    ~WorkspaceBackend();

    WorkspaceBackend(const WorkspaceBackend&) = delete;
    WorkspaceBackend& operator=(const WorkspaceBackend&) = delete;

    // Bind ext-workspace-v1 on the given display. Non-blocking: the manager
    // (if the compositor offers it) is bound from the registry callback and the
    // initial workspace set arrives as push events on the host's normal
    // dispatch; a wl_display sync reports a missing protocol for diagnostics.
    void start(wl_display* display);

    const WorkspaceSnapshot& snapshot() const { return snap_; }
    void setOnChange(std::function<void()> cb) { onChange_ = std::move(cb); }

    // Switch to the named workspace (no-op if unknown or unsupported).
    void activate(const std::string& name);

    // --- C listener trampolines (public; not for external use) ---
    void onRegistryGlobal(wl_registry*, uint32_t name, const char* iface, uint32_t version);
    void onManagerWorkspace(ext_workspace_handle_v1* h);
    void onManagerDone();
    static void onHandleName(WsHandle*, const char* name);
    static void onHandleCoordinates(WsHandle*, const uint32_t* coords, size_t n);
    static void onHandleState(WsHandle*, uint32_t state);
    void onHandleRemoved(WsHandle*);
    void onSyncDone(wl_callback* cb, uint32_t time);

private:
    void rebuildAndNotify();

    wl_display* display_ = nullptr;
    wl_registry* registry_ = nullptr;
    wl_callback* syncCallback_ = nullptr;
    ext_workspace_manager_v1* manager_ = nullptr;
    std::vector<std::unique_ptr<WsHandle>> handles_;
    WorkspaceSnapshot snap_;
    std::function<void()> onChange_;
};

}  // namespace qypr
