// PowerMenuIndicator.hpp - Power trigger for the unlocked bar.
//
// Activating the indicator opens Quick Settings, whose power tile runs
// `waylaunch --power` (the desktop's only power UI). Session-sensitive: the
// lock screen must never offer these — it has its own ConfirmPopover behind the
// reveal, and a shutdown button on a locked machine is a footgun. The
// indicator hides itself entirely when no SystemActions is supplied
// (qypr-lock supplies none).
//
// hasDetailedView() stays true so the open-indicator-detail path still routes
// through StatusBar's "power" diversion into Quick Settings; the indicator
// owns no popover of its own (createDetailedView returns null).

#pragma once

#include <string>

#include "ui/statusbar/StatusIndicator.hpp"

namespace qypr {

class SystemActions;

class PowerMenuIndicator : public StatusIndicator {
public:
    explicit PowerMenuIndicator(const SystemBackends& backends);

    std::string icon() const override { return ""; }
    std::string themedIcon() const override { return "system-shutdown-symbolic"; }
    std::string tooltip() const override { return "Control Center"; }
    Color iconColor() const override;

    bool onClick(double x, double y) override;
    bool hasDetailedView() const override { return true; }
    std::unique_ptr<DetailedPopover> createDetailedView() override;

    bool sensitive() const override { return true; }

private:
    SystemActions* power_ = nullptr;
};

}  // namespace qypr
