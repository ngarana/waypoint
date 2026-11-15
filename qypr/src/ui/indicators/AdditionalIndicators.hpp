// AdditionalIndicators.hpp - App modules: one app's status, by name.
//
// whatsapp and email show the AppStatus records their claims match
// (StatusArea::defaultClaims, extended by [tray] claims-whatsapp /
// claims-email): a WhatsApp client, a mail client (any desktop entry in the
// Email category). The module is on the bar while such an app has something
// to say, has a window open, or sits in the tray; notification-apps then
// leaves the app to it. Gestures: AppStatusIndicator. Removable drives and
// phones have their own modules (RemovableDevicesIndicator,
// PhoneLinkIndicator).
#pragma once

#include <string>
#include <vector>

#include "ui/indicators/AppStatusIndicator.hpp"
#include "ui/indicators/TrayAppBinding.hpp"

namespace qypr {

class AppModuleIndicator : public AppStatusIndicator {
public:
    AppModuleIndicator(const SystemBackends& backends, const std::string& id, int priority,
                       std::string label, std::string glyph, std::string fallback);

    // The claims this module matches apps against.
    [[nodiscard]] std::vector<std::string> patterns() const;

protected:
    [[nodiscard]] std::vector<AppStatus> select(std::vector<AppStatus> all) const override;
    [[nodiscard]] bool tracksWindows() const override { return true; }
    [[nodiscard]] std::string fallbackCommand() const override { return fallback_; }
    [[nodiscard]] std::string emptyTooltip() const override { return label_ + " is not running"; }
    [[nodiscard]] const TrayAppBinding* tray() const override { return &tray_; }

private:
    std::string label_;
    std::string glyph_;  // drawn for a tray-only app without a themed icon
    std::string fallback_;
    TrayAppBinding tray_;
};

class WhatsappIndicator : public AppModuleIndicator {
public:
    explicit WhatsappIndicator(const SystemBackends& backends);
};

using WhatsAppIndicator = WhatsappIndicator;

class EmailIndicator : public AppModuleIndicator {
public:
    explicit EmailIndicator(const SystemBackends& backends);
};

using MailIndicator = EmailIndicator;
using MailboxIndicator = EmailIndicator;
using InboxIndicator = EmailIndicator;

}  // namespace qypr
