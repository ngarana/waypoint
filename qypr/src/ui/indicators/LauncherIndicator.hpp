// LauncherIndicator.hpp - Application launcher trigger.
//
// A single bar button that launches the desktop application launcher
// (`waylaunch`, Super+D) through the loopless spawn path. Bar-only: the lock
// screen supplies no DesktopIndex, so the indicator hides itself and can never
// spawn an application while locked. The former in-bar LauncherPopover
// (keyboard-driven .desktop search) was deleted as duplicate UI — waylaunch
// is the launcher.
#pragma once

#include <string>

#include "ui/statusbar/StatusIndicator.hpp"

namespace qypr {

class Config;
class DesktopIndex;
class EventLoop;

class LauncherIndicator : public StatusIndicator {
public:
    explicit LauncherIndicator(const SystemBackends& backends);

    std::string icon() const override;
    std::string tooltip() const override;

    bool onClick(double x, double y) override;
    void onActivate() override;

    bool hasDetailedView() const override { return false; }
    std::unique_ptr<DetailedPopover> createDetailedView() override { return nullptr; }

private:
    void launch();

    DesktopIndex* backend_ = nullptr;  // presence gate only (bar vs lock)
    const Config* config_ = nullptr;   // borrowed; null on qypr-lock is valid
    EventLoop* loop_ = nullptr;        // pidfd reaping (I3); null on qypr-lock
};

}  // namespace qypr
