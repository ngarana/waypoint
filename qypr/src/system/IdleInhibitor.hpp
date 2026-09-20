// IdleInhibitor.hpp - "Keep awake" toggle via zwp_idle_inhibit_manager_v1.
//
// Holds (or drops) a zwp_idle_inhibitor_v1 on the bar's own surface. While the
// inhibitor exists, an idle daemon (swayidle/hypridle) that honours the standard
// protocol will not blank/lock the screen. Bar-only by nature — the manager and
// surface come from BarDisplay, which the lock screen does not run.
//
// init() is called once the display globals are bound; before that (or when the
// compositor lacks the protocol) available() is false and the indicator hides.
#pragma once

#include <functional>

struct wl_display;
struct wl_surface;
struct zwp_idle_inhibit_manager_v1;
struct zwp_idle_inhibitor_v1;

namespace qypr {

class IdleInhibitor {
public:
    IdleInhibitor() = default;
    ~IdleInhibitor();

    IdleInhibitor(const IdleInhibitor&) = delete;
    IdleInhibitor& operator=(const IdleInhibitor&) = delete;

    // Supplied by BarApp after BarDisplay::connect(). Any null → unavailable.
    void init(zwp_idle_inhibit_manager_v1* mgr, wl_surface* surface, wl_display* display);

    bool available() const { return mgr_ != nullptr && surface_ != nullptr; }
    bool active() const { return inhibitor_ != nullptr; }

    void setActive(bool on);
    void toggle() { setActive(!active()); }

    void setOnChange(std::function<void()> cb) { onChange_ = std::move(cb); }

private:
    zwp_idle_inhibit_manager_v1* mgr_ = nullptr;
    wl_surface* surface_ = nullptr;
    wl_display* display_ = nullptr;
    zwp_idle_inhibitor_v1* inhibitor_ = nullptr;
    std::function<void()> onChange_;
};

}  // namespace qypr
