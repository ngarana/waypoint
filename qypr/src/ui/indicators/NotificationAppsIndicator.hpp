// NotificationAppsIndicator.hpp - Dynamic per-app notification icons.
#pragma once

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

private:
    struct AppEntry {
        std::string name;
        std::string glyph;
        int count = 0;
    };
    std::vector<AppEntry> apps_;
    NotificationMonitor* monitor_ = nullptr;
};

}  // namespace qypr
