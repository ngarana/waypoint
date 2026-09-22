// LauncherIndicator.cpp - Application launcher trigger implementation.
#include "ui/indicators/LauncherIndicator.hpp"

#include "core/Config.hpp"
#include "system/DesktopIndex.hpp"
#include "ui/statusbar/IndicatorRegistry.hpp"

namespace qypr {

namespace {
constexpr const char* kAppsGlyph = "󰀻";  // nf-md-apps (grid)
constexpr const char* kConfigSection = "quick-settings";
constexpr const char* kDefaultCommand = "waylaunch";
}  // namespace

LauncherIndicator::LauncherIndicator(const SystemBackends& backends)
    : StatusIndicator("launcher", Zone::Left, 5),
      backend_(backends.desktopIndex),
      config_(backends.config),
      loop_(backends.loop) {
    // Present only when a DesktopIndex is supplied — i.e. on the unlocked bar.
    // The lock screen leaves it null, so the launcher never appears while locked.
    visible = backend_ != nullptr;
}

std::string LauncherIndicator::icon() const {
    return kAppsGlyph;
}

std::string LauncherIndicator::tooltip() const {
    return "Applications";
}

bool LauncherIndicator::onClick(double x, double y) {
    (void)x;
    (void)y;
    launch();
    return true;  // consumed: no fallthrough to activateIndicator
}

void LauncherIndicator::onActivate() {
    launch();
}

void LauncherIndicator::launch() {
    // The desktop's launcher is `waylaunch` (Super+D); the former in-bar
    // LauncherPopover over DesktopIndex was deleted as duplicate UI. Spawn
    // reaped through the loop (no fork, no zombie), mirroring the Quick
    // Settings power tile's command override convention. No loop (lock
    // screen) means no launch — but the indicator is hidden there anyway.
    if (loop_ == nullptr) { return; }
    std::string cmd = kDefaultCommand;
    if (config_ != nullptr) { cmd = config_->getString(kConfigSection, "launcher-command", cmd); }
    launchDetached(*loop_, cmd, /*terminal=*/false);
}

REGISTER_SESSION_INDICATOR("launcher", Zone::Left, 5, LauncherIndicator)

}  // namespace qypr
