// NotificationAppsIndicator.hpp - Dynamic per-app notification icons for the status bar.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "ui/statusbar/StatusIndicator.hpp"

namespace qypr {

class NotificationAppsIndicator : public StatusIndicator {
public:
    explicit NotificationAppsIndicator(const SystemBackends& backends);

    std::string icon() const override { return ""; }
    bool sensitive() const override { return true; }
    std::string tooltip() const override;
    double measureWidth(Painter& p) override;
    void draw(Painter& p, int64_t now) override;
    void onBackendUpdate() override;

    bool onClick(double x, double y) override;
    bool onSecondaryClick(double x, double y) override;
    bool onMiddleClick(double x, double y) override;
    void onHoverMotion(double x, double y) override;
    void onHoverLeave() override;

    struct AppEntry {
        std::string name;
        std::string desktopEntry;
        std::string icon;
        int count = 0;
        int64_t latestTime = 0;
        std::string latestTitle;
        std::string latestBody;
        std::vector<uint32_t> daemonIds;
        bool hasCritical = false;
    };

    const std::vector<AppEntry>& apps() const { return apps_; }

private:
    int hitTest(double x) const;

    SystemBackends backends_{};
    std::vector<AppEntry> apps_;
    double hoverX_ = -1.0;
    double hoverY_ = -1.0;
};

}  // namespace qypr
