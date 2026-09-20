// IdleInhibitor.cpp - idle-inhibit controller implementation.
#include "system/IdleInhibitor.hpp"

#include <wayland-client.h>

#include "idle-inhibit-unstable-v1-client-protocol.h"

namespace qypr {

IdleInhibitor::~IdleInhibitor() {
    if (inhibitor_) zwp_idle_inhibitor_v1_destroy(inhibitor_);
}

void IdleInhibitor::init(zwp_idle_inhibit_manager_v1* mgr, wl_surface* surface,
                         wl_display* display) {
    mgr_ = mgr;
    surface_ = surface;
    display_ = display;
    if (onChange_) onChange_();  // availability may have just flipped on
}

void IdleInhibitor::setActive(bool on) {
    if (!available()) return;
    if (on == active()) return;

    if (on) {
        inhibitor_ = zwp_idle_inhibit_manager_v1_create_inhibitor(mgr_, surface_);
    } else {
        zwp_idle_inhibitor_v1_destroy(inhibitor_);
        inhibitor_ = nullptr;
    }
    if (display_) wl_display_flush(display_);
    if (onChange_) onChange_();
}

}  // namespace qypr
