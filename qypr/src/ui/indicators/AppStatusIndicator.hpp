// AppStatusIndicator.hpp - A row of app buttons built from AppStatus.
//
// The base of notification-apps (every app with something to say) and of the
// whatsapp / email modules (the apps their claims match). Each app is one
// 28 px button: its icon, a badge with its count, a progress bar, a dot for
// critical notifications or urgency.
//
//   left click    focus the app's window (the next one when one is already
//                 focused), else activate it (D-Bus) or launch it; clears its
//                 notifications.
//   right click   the app's menu: its desktop actions, its LauncherEntry
//   / Menu key    quicklist, its tray menu (for a module that claims the
//                 app's tray icon), and "Clear notifications".
//   middle click  clears its notifications.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "ui/indicators/AppStatus.hpp"
#include "ui/indicators/TrayAppBinding.hpp"
#include "ui/statusbar/StatusIndicator.hpp"

namespace qypr {

class AppStatusIndicator : public StatusIndicator {
public:
    AppStatusIndicator(const SystemBackends& backends, const std::string& id, int priority);

    [[nodiscard]] std::string icon() const override { return ""; }
    [[nodiscard]] bool sensitive() const override { return true; }
    [[nodiscard]] std::string tooltip() const override;
    double measureWidth(Painter& p) override;
    void draw(Painter& p, int64_t now) override;
    void onBackendUpdate() override;

    bool onClick(double x, double y) override;
    bool onSecondaryClick(double /*x*/, double /*y*/) override { return false; }
    bool onMiddleClick(double x, double y) override;
    void onHoverMotion(double x, double y) override;
    void onHoverLeave() override;
    [[nodiscard]] bool hasContextMenu() const override { return !apps_.empty(); }
    std::unique_ptr<DetailedPopover> createContextMenu(double x, double y) override;

    [[nodiscard]] const std::vector<AppStatus>& apps() const { return apps_; }

    // The tooltip line for one app (count, progress, latest notification).
    static std::string describe(const AppStatus& app);

protected:
    // Which of the session's apps this module shows, in order.
    [[nodiscard]] virtual std::vector<AppStatus> select(std::vector<AppStatus> all) const = 0;
    // Whether the module needs windows resolved even when no app has news
    // (a module that shows a running app).
    [[nodiscard]] virtual bool tracksWindows() const { return false; }
    // Run when an app can be neither focused nor launched.
    [[nodiscard]] virtual std::string fallbackCommand() const { return {}; }
    [[nodiscard]] virtual std::string emptyTooltip() const = 0;
    // The tray app this module claims, for its menu (null: none).
    [[nodiscard]] virtual const TrayAppBinding* tray() const { return nullptr; }

    [[nodiscard]] int hitTest(double x) const;
    void open(const AppStatus& app) const;
    void dismiss(const AppStatus& app) const;

    SystemBackends backends_{};
    std::vector<AppStatus> apps_;
    double hoverX_ = -1.0;
    double hoverY_ = -1.0;
};

}  // namespace qypr
