// DndState.hpp - qypr-local Do Not Disturb state.
//
// The Desktop Notifications spec defines no DND API and every daemon-side
// interface is proprietary (native-only principle), so DND is a plain local
// flag: owned by App, toggled by the status bar, read by Shell to suppress
// the lockscreen notification stack. No daemon round-trips, no persistence —
// a fresh lock starts with DND off.

#pragma once

#include <functional>
#include <vector>

namespace qypr {

class DndState {
public:
    bool enabled() const { return enabled_; }

    void setEnabled(bool on) {
        if (on == enabled_) return;
        enabled_ = on;
        for (auto& cb : listeners_) {
            if (cb) cb();
        }
    }

    void toggle() { setEnabled(!enabled_); }

    // Multiple consumers listen: Shell (notification suppression) and
    // StatusBar (indicator refresh). Listeners must outlive this object.
    void addListener(std::function<void()> cb) { listeners_.push_back(std::move(cb)); }

private:
    bool enabled_ = false;
    std::vector<std::function<void()>> listeners_;
};

}  // namespace qypr
