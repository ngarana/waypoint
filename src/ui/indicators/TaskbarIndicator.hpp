// TaskbarIndicator.hpp - Icons-only window list (task manager) applet.
//
// The signature panel feature: one button per open toplevel, drawn as its app
// icon, with the focused window highlighted and minimized ones dimmed. A left
// click focuses/raises the window — or, if it is already focused, minimizes it
// (the familiar taskbar toggle). A middle click closes it.
//
// Session-sensitive: the open-window list reveals what you are doing, so it is
// hidden while locked (like ActiveWindowIndicator) and only appears on the
// unlocked qypr-bar.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "system/ToplevelBackend.hpp"  // ToplevelSnapshot
#include "ui/statusbar/StatusIndicator.hpp"

namespace qypr {

class ToplevelBackend;

class TaskbarIndicator : public StatusIndicator {
public:
    explicit TaskbarIndicator(const SystemBackends& backends);

    std::string icon() const override { return ""; }  // custom multi-icon draw
    std::string tooltip() const override;
    bool sensitive() const override { return true; }

    double measureWidth(Painter& p) override;
    void draw(Painter& p, int64_t now) override;

    void onBackendUpdate() override;
    bool onClick(double x, double y) override;
    bool onMiddleClick(double x, double y) override;

private:
    // Map an x within bounds to a window index, or -1 if outside any button.
    int hitTest(double x) const;

    ToplevelBackend* backend_ = nullptr;
    ToplevelSnapshot snap_;
};

}  // namespace qypr
