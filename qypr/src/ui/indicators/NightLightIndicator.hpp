// NightLightIndicator.hpp - Night Light bar applet + compact slider popover.
#pragma once

#include "ui/statusbar/StatusIndicator.hpp"

namespace qypr {

class NightLightBackend;

class NightLightIndicator : public StatusIndicator {
public:
    explicit NightLightIndicator(const SystemBackends& backends);

    std::string icon() const override;
    std::string themedIcon() const override;
    std::string tooltip() const override;
    Color iconColor() const override;
    void onBackendUpdate() override;
    bool onScroll(double dx, double dy, double x, double y) override;

    std::unique_ptr<QSTile> createTile() override;
    bool hasDetailedView() const override { return backend_ != nullptr; }
    std::unique_ptr<DetailedPopover> createDetailedView() override;

private:
    NightLightBackend* backend_ = nullptr;
};

}  // namespace qypr
