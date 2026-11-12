// SNITrayHost.hpp - Persistent application indicators and overflow.
#pragma once
#include <string>
#include <vector>
#include "system/SNIBackend.hpp"
#include "ui/statusbar/StatusIndicator.hpp"

namespace qypr {
class DbusMenuBackend;
class SNITrayHost : public StatusIndicator {
public:
    explicit SNITrayHost(const SystemBackends& backends);
    std::string icon() const override { return ""; }
    bool sensitive() const override { return true; }
    std::string tooltip() const override;
    double measureWidth(Painter& p) override;
    void draw(Painter& p, int64_t now) override;
    void onBackendUpdate() override;
    bool onClick(double x, double y) override;
    bool onSecondaryClick(double x, double y) override;
    bool onMiddleClick(double x, double y) override;
    bool onScroll(double dx, double dy, double x, double y) override;
    void onHoverMotion(double x, double y) override;
    void onHoverLeave() override { hoverX_ = -1; }
    bool hasDetailedView() const override { return pendingMenu_ != -1; }
    std::unique_ptr<DetailedPopover> createDetailedView() override;

private:
    int iconIndexAt(double x) const;
    bool overOverflow(double x) const;
    SNIBackend* backend_ = nullptr;
    DbusMenuBackend* dbusMenu_ = nullptr;
    int pendingMenu_ = -1;
    double hoverX_ = -1;
};
}  // namespace qypr
