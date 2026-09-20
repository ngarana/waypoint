// ToplevelBackend.cpp - wlr-foreign-toplevel-management client (push-driven).
#include "system/ToplevelBackend.hpp"

#include <wayland-client.h>

#include <algorithm>
#include <cstdio>
#include <cstring>

#include "wlr-foreign-toplevel-management-unstable-v1-client-protocol.h"

namespace qypr {

namespace {

// --- zwlr_foreign_toplevel_handle_v1 listener trampolines ---
void tlTitle(void* data, zwlr_foreign_toplevel_handle_v1* /*unused*/, const char* title) {
    auto* h = static_cast<TlHandle*>(data);
    h->backend->onHandleTitle(h, title);
}
void tlAppId(void* data, zwlr_foreign_toplevel_handle_v1* /*unused*/, const char* appId) {
    auto* h = static_cast<TlHandle*>(data);
    h->backend->onHandleAppId(h, appId);
}
void tlOutputEnter(void* /*unused*/, zwlr_foreign_toplevel_handle_v1* /*unused*/,
                   wl_output* /*unused*/) {}
void tlOutputLeave(void* /*unused*/, zwlr_foreign_toplevel_handle_v1* /*unused*/,
                   wl_output* /*unused*/) {}
void tlState(void* data, zwlr_foreign_toplevel_handle_v1* /*unused*/, wl_array* states) {
    auto* h = static_cast<TlHandle*>(data);
    h->backend->onHandleState(h, static_cast<const uint32_t*>(states->data),
                              states->size / sizeof(uint32_t));
}
void tlDone(void* data, zwlr_foreign_toplevel_handle_v1* /*unused*/) {
    auto* h = static_cast<TlHandle*>(data);
    h->backend->onHandleDone(h);
}
void tlClosed(void* data, zwlr_foreign_toplevel_handle_v1* /*unused*/) {
    auto* h = static_cast<TlHandle*>(data);
    h->backend->onHandleClosed(h);
}
void tlParent(void* /*unused*/, zwlr_foreign_toplevel_handle_v1* /*unused*/,
              zwlr_foreign_toplevel_handle_v1* /*unused*/) {}
const zwlr_foreign_toplevel_handle_v1_listener kHandleListener = {.title = tlTitle,
                                                                  .app_id = tlAppId,
                                                                  .output_enter = tlOutputEnter,
                                                                  .output_leave = tlOutputLeave,
                                                                  .state = tlState,
                                                                  .done = tlDone,
                                                                  .closed = tlClosed,
                                                                  .parent = tlParent};

// --- zwlr_foreign_toplevel_manager_v1 listener trampolines ---
void managerToplevel(void* data, zwlr_foreign_toplevel_manager_v1* /*unused*/,
                     zwlr_foreign_toplevel_handle_v1* h) {
    static_cast<ToplevelBackend*>(data)->onManagerToplevel(h);
}
void managerFinished(void* /*unused*/, zwlr_foreign_toplevel_manager_v1* /*unused*/) {}
const zwlr_foreign_toplevel_manager_v1_listener kManagerListener = {.toplevel = managerToplevel,
                                                                    .finished = managerFinished};

// --- registry ---
void registryGlobal(void* data, wl_registry* reg, uint32_t name, const char* iface,
                    uint32_t version) {
    static_cast<ToplevelBackend*>(data)->onRegistryGlobal(reg, name, iface, version);
}
void registryGlobalRemove(void* /*unused*/, wl_registry* /*unused*/, uint32_t /*unused*/) {}
const wl_registry_listener kRegistryListener = {.global = registryGlobal,
                                                .global_remove = registryGlobalRemove};

// --- display sync (missing-global diagnostic, non-blocking) ---
void syncDone(void* data, wl_callback* cb, uint32_t time) {
    static_cast<ToplevelBackend*>(data)->onSyncDone(cb, time);
}

}  // namespace

// Bind the manager and (for activate) our own seat. `this` is the listener data
// so late-appearing globals stay safe — the registry listener outlives start().
void ToplevelBackend::onRegistryGlobal(wl_registry* reg, uint32_t name, const char* iface,
                                       uint32_t version) {
    constexpr uint32_t kWantVersion = 3;
    if (std::strcmp(iface, zwlr_foreign_toplevel_manager_v1_interface.name) == 0 &&
        (manager_ == nullptr)) {
        uint32_t const bind = version < kWantVersion ? version : kWantVersion;
        manager_ = static_cast<zwlr_foreign_toplevel_manager_v1*>(
            wl_registry_bind(reg, name, &zwlr_foreign_toplevel_manager_v1_interface, bind));
        // The initial toplevel set arrives right after binding (toplevel +
        // title/app_id/state + done), pushed on the host's normal dispatch.
        zwlr_foreign_toplevel_manager_v1_add_listener(manager_, &kManagerListener, this);
        snap_.available = true;
    } else if (std::strcmp(iface, wl_seat_interface.name) == 0 && (seat_ == nullptr)) {
        // Seat v1 is enough: activate() only needs the object, not input events.
        seat_ = static_cast<wl_seat*>(wl_registry_bind(reg, name, &wl_seat_interface, 1));
    }
}

ToplevelBackend::~ToplevelBackend() {
    if (syncCallback_ != nullptr) { wl_callback_destroy(syncCallback_); }
    for (auto& h : handles_) {
        if (h->handle != nullptr) { zwlr_foreign_toplevel_handle_v1_destroy(h->handle); }
    }
    if (seat_ != nullptr) { wl_seat_destroy(seat_); }
    if (manager_ != nullptr) { zwlr_foreign_toplevel_manager_v1_destroy(manager_); }
    if (registry_ != nullptr) { wl_registry_destroy(registry_); }
}

void ToplevelBackend::start(wl_display* display) {
    if ((display == nullptr) || (display_ != nullptr)) { return; }
    display_ = display;

    // Non-blocking: no wl_display_roundtrip on the startup path — the first
    // frame never waits on the compositor. The manager is bound from the
    // registry callback; the initial toplevel set follows as push events.
    registry_ = wl_display_get_registry(display);
    wl_registry_add_listener(registry_, &kRegistryListener, this);

    // One async sync: when it completes every advertised global has been
    // delivered — if the manager never appeared, the protocol is missing.
    syncCallback_ = wl_display_sync(display);
    static const wl_callback_listener kSyncListener = {.done = syncDone};
    wl_callback_add_listener(syncCallback_, &kSyncListener, this);
}

void ToplevelBackend::onSyncDone(wl_callback* cb, uint32_t /*time*/) {
    wl_callback_destroy(cb);
    syncCallback_ = nullptr;
    if (manager_ == nullptr) {
        std::fprintf(
            stderr, "qypr: no wlr-foreign-toplevel-management; active-window indicator disabled\n");
    }
}

void ToplevelBackend::onManagerToplevel(zwlr_foreign_toplevel_handle_v1* h) {
    auto t = std::make_unique<TlHandle>();
    t->backend = this;
    t->handle = h;
    t->id = nextId_++;
    zwlr_foreign_toplevel_handle_v1_add_listener(h, &kHandleListener, t.get());
    handles_.push_back(std::move(t));
}

void ToplevelBackend::onHandleTitle(TlHandle* h, const char* title) {
    h->title = (title != nullptr) ? title : "";
}

void ToplevelBackend::onHandleAppId(TlHandle* h, const char* appId) {
    h->appId = (appId != nullptr) ? appId : "";
}

void ToplevelBackend::onHandleState(TlHandle* h, const uint32_t* states, size_t n) {
    h->active = false;
    h->minimized = false;
    for (size_t i = 0; (states != nullptr) && i < n; ++i) {
        if (states[i] == ZWLR_FOREIGN_TOPLEVEL_HANDLE_V1_STATE_ACTIVATED) { h->active = true; }
        if (states[i] == ZWLR_FOREIGN_TOPLEVEL_HANDLE_V1_STATE_MINIMIZED) { h->minimized = true; }
    }
}

void ToplevelBackend::onHandleDone(TlHandle* /*unused*/) {
    // A handle's properties are consistent after 'done'; recompute the focused
    // window (only notifies when the active app id / title actually changed).
    rebuildAndNotify();
}

void ToplevelBackend::onHandleClosed(TlHandle* h) {
    if (h->handle != nullptr) {
        zwlr_foreign_toplevel_handle_v1_destroy(h->handle);
        h->handle = nullptr;
    }
    std::erase_if(handles_, [&](const std::unique_ptr<TlHandle>& p) { return p.get() == h; });
    rebuildAndNotify();
}

void ToplevelBackend::rebuildAndNotify() {
    ToplevelSnapshot next;
    next.available = snap_.available;
    for (const auto& h : handles_) {
        // Skip handles the compositor has announced but not yet described — they
        // would otherwise flash as a blank taskbar button for one frame.
        if (h->appId.empty() && h->title.empty()) { continue; }
        next.windows.push_back({.id = h->id,
                                .appId = h->appId,
                                .title = h->title,
                                .active = h->active,
                                .minimized = h->minimized});
        // The most recently focused toplevel wins if several report activated
        // (during a focus handoff both may briefly carry the bit).
        if (h->active) {
            next.hasActive = true;
            next.appId = h->appId;
            next.title = h->title;
        }
    }

    if (next == snap_) { return; }
    snap_ = std::move(next);
    if (onChange_) { onChange_(); }
}

TlHandle* ToplevelBackend::find(uint64_t id) const {
    for (const auto& h : handles_) {
        if (h->id == id) { return h.get(); }
    }
    return nullptr;
}

void ToplevelBackend::activate(uint64_t id) {
    TlHandle const* h = find(id);
    if ((h == nullptr) || (h->handle == nullptr) || (seat_ == nullptr)) { return; }
    zwlr_foreign_toplevel_handle_v1_activate(h->handle, seat_);
    if (display_ != nullptr) { wl_display_flush(display_); }
}

void ToplevelBackend::close(uint64_t id) {
    TlHandle const* h = find(id);
    if ((h == nullptr) || (h->handle == nullptr)) { return; }
    zwlr_foreign_toplevel_handle_v1_close(h->handle);
    if (display_ != nullptr) { wl_display_flush(display_); }
}

void ToplevelBackend::toggleMinimize(uint64_t id) {
    TlHandle const* h = find(id);
    if ((h == nullptr) || (h->handle == nullptr)) { return; }
    if (h->minimized) {
        zwlr_foreign_toplevel_handle_v1_unset_minimized(h->handle);
    } else {
        zwlr_foreign_toplevel_handle_v1_set_minimized(h->handle);
    }
    if (display_ != nullptr) { wl_display_flush(display_); }
}

}  // namespace qypr
