// BackgroundAppsIndicator.hpp - The status area's overflow (`background-apps`).
//
// Aliases: `sni`, `tray`.
//
// One chevron for everything that is running or relevant but doesn't earn a
// permanent place on the bar (see docs/APPLICATION_STATUS.md):
//
//   STATUS           native modules that are passive right now: a drive
//                    that lives in fstab, a paired phone that is away. The
//                    row opens that module's own flyout, anchored here.
//   BACKGROUND APPS  StatusNotifierItem apps no native module has claimed
//                    (Telegram, Steam, a password manager). The row activates
//                    the app; right click / Menu opens the app's own menu.
//
// Tray apps are the last resort, so none of them takes bar space by default.
// Two exceptions put an app's icon next to the chevron: `[tray] pinned`, and
// an app asking for attention (StatusNotifierItem NeedsAttention), for as
// long as it asks. `[tray] hidden` keeps an app out of sight entirely.
#pragma once

#include <string>
#include <vector>

#include "system/SNIBackend.hpp"
#include "ui/statusbar/StatusIndicator.hpp"

namespace qypr {

class DbusMenuBackend;

class BackgroundAppsIndicator : public StatusIndicator {
public:
    explicit BackgroundAppsIndicator(const SystemBackends& backends);

    [[nodiscard]] std::string icon() const override { return ""; }
    [[nodiscard]] bool sensitive() const override { return true; }
    [[nodiscard]] bool listsOverflow() const override { return true; }
    [[nodiscard]] std::string tooltip() const override;
    double measureWidth(Painter& p) override;
    void draw(Painter& p, int64_t now) override;
    void onBackendUpdate() override;
    bool onClick(double x, double y) override;
    bool onSecondaryClick(double x, double y) override;
    bool onMiddleClick(double x, double y) override;
    bool onScroll(double dx, double dy, double x, double y) override;
    void onHoverMotion(double x, double y) override;
    void onHoverLeave() override { hoverX_ = -1; }
    [[nodiscard]] bool hasDetailedView() const override { return pendingMenu_ != kNoMenu; }
    std::unique_ptr<DetailedPopover> createDetailedView() override;

    // Tray items next to the chevron: pinned, or asking for attention.
    static std::vector<size_t> stripItems(const SNIBackend* sni, const StatusArea* area);
    // Every other unclaimed, unhidden tray item.
    static std::vector<size_t> overflowItems(const SNIBackend* sni, const StatusArea* area);

    static constexpr size_t kStripLimit = 6;

private:
    static constexpr int kNoMenu = -1;
    static constexpr int kOverflow = -2;
    [[nodiscard]] bool hasOverflow() const;
    [[nodiscard]] int iconIndexAt(double x) const;
    [[nodiscard]] bool overChevron(double x) const;
    void refreshVisibility();

    SNIBackend* backend_ = nullptr;
    DbusMenuBackend* dbusMenu_ = nullptr;
    StatusArea* area_ = nullptr;
    int pendingMenu_ = kNoMenu;
    double hoverX_ = -1;
};

}  // namespace qypr
