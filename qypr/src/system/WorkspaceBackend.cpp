// WorkspaceBackend.cpp - ext-workspace-v1 client (push-driven).
#include "system/WorkspaceBackend.hpp"

#include <wayland-client.h>

#include <algorithm>
#include <cstdio>
#include <cstring>

#include "ext-workspace-v1-client-protocol.h"

namespace qypr {

namespace {

// ext_workspace_handle_v1 state bits.
constexpr uint32_t kStateActive = 1;
constexpr uint32_t kStateUrgent = 2;
constexpr uint32_t kStateHidden = 4;

// --- ext_workspace_handle_v1 listener trampolines ---
void handleId(void* /*unused*/, ext_workspace_handle_v1* /*unused*/, const char* /*unused*/) {}
void handleName(void* data, ext_workspace_handle_v1* /*unused*/, const char* name) {
    auto* h = static_cast<WsHandle*>(data);
    h->backend->onHandleName(h, name);
}
void handleCoordinates(void* data, ext_workspace_handle_v1* /*unused*/, wl_array* coords) {
    auto* h = static_cast<WsHandle*>(data);
    h->backend->onHandleCoordinates(h, static_cast<const uint32_t*>(coords->data),
                                    coords->size / sizeof(uint32_t));
}
void handleState(void* data, ext_workspace_handle_v1* /*unused*/, uint32_t state) {
    auto* h = static_cast<WsHandle*>(data);
    h->backend->onHandleState(h, state);
}
void handleCapabilities(void* /*unused*/, ext_workspace_handle_v1* /*unused*/,
                        uint32_t /*unused*/) {}
void handleRemoved(void* data, ext_workspace_handle_v1* /*unused*/) {
    auto* h = static_cast<WsHandle*>(data);
    h->backend->onHandleRemoved(h);
}
const ext_workspace_handle_v1_listener kHandleListener = {.id = handleId,
                                                          .name = handleName,
                                                          .coordinates = handleCoordinates,
                                                          .state = handleState,
                                                          .capabilities = handleCapabilities,
                                                          .removed = handleRemoved};

// --- ext_workspace_manager_v1 listener trampolines ---
void managerGroup(void* /*unused*/, ext_workspace_manager_v1* /*unused*/,
                  ext_workspace_group_handle_v1* /*unused*/) {}
void managerWorkspace(void* data, ext_workspace_manager_v1* /*unused*/,
                      ext_workspace_handle_v1* ws) {
    static_cast<WorkspaceBackend*>(data)->onManagerWorkspace(ws);
}
void managerDone(void* data, ext_workspace_manager_v1* /*unused*/) {
    static_cast<WorkspaceBackend*>(data)->onManagerDone();
}
void managerFinished(void* /*unused*/, ext_workspace_manager_v1* /*unused*/) {}
const ext_workspace_manager_v1_listener kManagerListener = {.workspace_group = managerGroup,
                                                            .workspace = managerWorkspace,
                                                            .done = managerDone,
                                                            .finished = managerFinished};

// --- registry ---
void registryGlobal(void* data, wl_registry* reg, uint32_t name, const char* iface,
                    uint32_t version) {
    static_cast<WorkspaceBackend*>(data)->onRegistryGlobal(reg, name, iface, version);
}
void registryGlobalRemove(void* /*unused*/, wl_registry* /*unused*/, uint32_t /*unused*/) {}
const wl_registry_listener kRegistryListener = {.global = registryGlobal,
                                                .global_remove = registryGlobalRemove};

// --- display sync (missing-global diagnostic, non-blocking) ---
void syncDone(void* data, wl_callback* cb, uint32_t time) {
    static_cast<WorkspaceBackend*>(data)->onSyncDone(cb, time);
}

}  // namespace

void WorkspaceBackend::onRegistryGlobal(wl_registry* reg, uint32_t name, const char* iface,
                                        uint32_t /*version*/) {
    if (std::strcmp(iface, ext_workspace_manager_v1_interface.name) == 0 && (manager_ == nullptr)) {
        manager_ = static_cast<ext_workspace_manager_v1*>(
            wl_registry_bind(reg, name, &ext_workspace_manager_v1_interface, 1));
        // The initial workspace set arrives right after binding (create +
        // name/state + done), pushed on the host's normal dispatch.
        ext_workspace_manager_v1_add_listener(manager_, &kManagerListener, this);
        snap_.available = true;
    }
}

WorkspaceBackend::~WorkspaceBackend() {
    if (syncCallback_ != nullptr) { wl_callback_destroy(syncCallback_); }
    for (auto& h : handles_) {
        if (h->handle != nullptr) { ext_workspace_handle_v1_destroy(h->handle); }
    }
    if (manager_ != nullptr) { ext_workspace_manager_v1_destroy(manager_); }
    if (registry_ != nullptr) { wl_registry_destroy(registry_); }
}

void WorkspaceBackend::start(wl_display* display) {
    if ((display == nullptr) || (display_ != nullptr)) { return; }
    display_ = display;

    // Non-blocking: no wl_display_roundtrip on the startup path — the first
    // frame never waits on the compositor. The manager is bound from the
    // registry callback; the initial workspace set follows as push events.
    registry_ = wl_display_get_registry(display);
    wl_registry_add_listener(registry_, &kRegistryListener, this);

    // One async sync: when it completes every advertised global has been
    // delivered — if the manager never appeared, the protocol is missing.
    syncCallback_ = wl_display_sync(display);
    static const wl_callback_listener kSyncListener = {.done = syncDone};
    wl_callback_add_listener(syncCallback_, &kSyncListener, this);
}

void WorkspaceBackend::onSyncDone(wl_callback* cb, uint32_t /*time*/) {
    wl_callback_destroy(cb);
    syncCallback_ = nullptr;
    if (manager_ == nullptr) {
        std::fprintf(stderr, "qypr: no ext-workspace-v1; workspaces indicator disabled\n");
    }
}

void WorkspaceBackend::onManagerWorkspace(ext_workspace_handle_v1* ws) {
    auto h = std::make_unique<WsHandle>();
    h->backend = this;
    h->handle = ws;
    ext_workspace_handle_v1_add_listener(ws, &kHandleListener, h.get());
    handles_.push_back(std::move(h));
}

void WorkspaceBackend::onHandleName(WsHandle* h, const char* name) {
    h->name = (name != nullptr) ? name : "";
}

void WorkspaceBackend::onHandleCoordinates(WsHandle* h, const uint32_t* coords, size_t n) {
    if (n > 0 && (coords != nullptr)) {
        h->coord = coords[0];
        h->hasCoord = true;
    } else {
        h->hasCoord = false;
    }
}

void WorkspaceBackend::onHandleState(WsHandle* h, uint32_t state) {
    h->active = ((state & kStateActive) != 0u);
    h->urgent = ((state & kStateUrgent) != 0u);
    h->hidden = ((state & kStateHidden) != 0u);
}

void WorkspaceBackend::onHandleRemoved(WsHandle* h) {
    if (h->handle != nullptr) {
        ext_workspace_handle_v1_destroy(h->handle);
        h->handle = nullptr;
    }
    std::erase_if(handles_, [&](const std::unique_ptr<WsHandle>& p) { return p.get() == h; });
    // 'removed' arrives outside a manager transaction; refresh immediately.
    rebuildAndNotify();
}

void WorkspaceBackend::onManagerDone() {
    rebuildAndNotify();
}

void WorkspaceBackend::rebuildAndNotify() {
    WorkspaceSnapshot next;
    next.available = snap_.available;

    std::vector<const WsHandle*> visible;
    for (const auto& h : handles_) {
        if (h->hidden) {
            continue;  // spec: hidden workspaces must not be displayed
        }
        visible.push_back(h.get());
    }
    // Order by coordinate when the compositor supplies one, else keep a stable,
    // numeric-name-aware order so "1 2 … 10" reads naturally.
    auto sortKey = [](const WsHandle* h) -> long {
        if (h->hasCoord) { return static_cast<long>(h->coord); }
        char* end = nullptr;
        long const n = std::strtol(h->name.c_str(), &end, 10);
        return (end && *end == '\0' && !h->name.empty()) ? n : 1'000'000L;
    };
    std::ranges::stable_sort(
        visible, [&](const WsHandle* a, const WsHandle* b) { return sortKey(a) < sortKey(b); });

    for (const WsHandle* h : visible) {
        next.workspaces.push_back({.name = h->name, .active = h->active, .urgent = h->urgent});
    }

    if (next == snap_) { return; }
    snap_ = std::move(next);
    if (onChange_) { onChange_(); }
}

void WorkspaceBackend::activate(const std::string& name) {
    if (manager_ == nullptr) { return; }
    for (const auto& h : handles_) {
        if ((h->handle != nullptr) && h->name == name) {
            ext_workspace_handle_v1_activate(h->handle);
            ext_workspace_manager_v1_commit(manager_);
            if (display_ != nullptr) { wl_display_flush(display_); }
            return;
        }
    }
}

}  // namespace qypr
