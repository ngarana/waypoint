// AdditionalIndicators.cpp - Extra status bar icons (USB, KDE Connect, WhatsApp, email).
#include "ui/indicators/SimpleIconIndicator.hpp"
#include "ui/indicators/UsbIndicator.hpp"
#include "ui/indicators/UsbEjectIndicator.hpp"
#include "ui/indicators/KdeConnectIndicator.hpp"
#include "ui/indicators/PhoneConnectIndicator.hpp"
#include "ui/indicators/WhatsappIndicator.hpp"
#include "ui/indicators/EmailIndicator.hpp"
#include "ui/indicators/MailIndicator.hpp"

#include "system/DesktopIndex.hpp"
#include "notifications/NotificationMonitor.hpp"
#include "ui/statusbar/IndicatorRegistry.hpp"

#include <algorithm>
#include <cctype>
#include <string>

namespace qypr {

namespace {
std::string lower(std::string s) {
    std::ranges::transform(s, s.begin(), [](unsigned char c) { return std::tolower(c); });
    return s;
}

int countMatchingNotifications(const SystemBackends& b, const std::vector<std::string>& needles) {
    if (b.notifications == nullptr) return 0;
    int n = 0;
    for (const auto& note : b.notifications->notifications()) {
        const std::string app = lower(note.app);
        for (const auto& needle : needles) {
            if (app.find(needle) != std::string::npos) {
                ++n;
                break;
            }
        }
    }
    return n;
}

void launchIfPossible(const SystemBackends& b, const std::string& cmd) {
    if (b.loop != nullptr) { launchDetached(*b.loop, cmd, /*terminal=*/false); }
}
}  // namespace

UsbIndicator::UsbIndicator(const SystemBackends& backends)
    : SimpleIconIndicator(backends, "usb", Zone::Right, 260, "󰇄", "USB devices",
                          "drive-removable-media-symbolic") {}

bool UsbIndicator::onClick(double, double) {
    launchIfPossible(backends_, "xdg-open /run/media");
    return true;
}

UsbEjectIndicator::UsbEjectIndicator(const SystemBackends& backends)
    : SimpleIconIndicator(backends, "usb-eject", Zone::Right, 261, "󰇄", "Eject USB device",
                          "media-eject-symbolic") {}

bool UsbEjectIndicator::onClick(double, double) {
    launchIfPossible(backends_, "udisksctl unmount -a");
    return true;
}

KdeConnectIndicator::KdeConnectIndicator(const SystemBackends& backends)
    : SimpleIconIndicator(backends, "kde-connect", Zone::Right, 262, "󰄜", "KDE Connect",
                          "smartphone-symbolic") {}

bool KdeConnectIndicator::onClick(double, double) {
    launchIfPossible(backends_, "kdeconnect-app");
    return true;
}

PhoneConnectIndicator::PhoneConnectIndicator(const SystemBackends& backends)
    : SimpleIconIndicator(backends, "phone-connect", Zone::Right, 263, "󰄜", "Phone connect",
                          "smartphone-symbolic") {}

bool PhoneConnectIndicator::onClick(double, double) {
    launchIfPossible(backends_, "kdeconnect-app");
    return true;
}

WhatsappIndicator::WhatsappIndicator(const SystemBackends& backends)
    : SimpleIconIndicator(backends, "whatsapp", Zone::Right, 264, "󰖣", "WhatsApp",
                          "whatsapp-symbolic") {}

std::string WhatsappIndicator::label() const {
    const int n = countMatchingNotifications(backends_, {"whatsapp", "whats"});
    return n > 0 ? std::to_string(n) : std::string{};
}

bool WhatsappIndicator::onClick(double, double) {
    launchIfPossible(backends_, "xdg-open https://web.whatsapp.com");
    return true;
}

EmailIndicator::EmailIndicator(const SystemBackends& backends)
    : SimpleIconIndicator(backends, "email", Zone::Right, 265, "󰇰", "Email",
                          "mail-unread-symbolic") {}

std::string EmailIndicator::label() const {
    const int n =
        countMatchingNotifications(backends_, {"mail", "thunderbird", "evolution", "geary"});
    return n > 0 ? std::to_string(n) : std::string{};
}

bool EmailIndicator::onClick(double, double) {
    launchIfPossible(backends_, "xdg-open mailto:");
    return true;
}

MailIndicator::MailIndicator(const SystemBackends& backends)
    : SimpleIconIndicator(backends, "mail", Zone::Right, 266, "󰇰", "Mail",
                          "mail-unread-symbolic") {}

std::string MailIndicator::label() const {
    const int n =
        countMatchingNotifications(backends_, {"mail", "thunderbird", "evolution", "geary"});
    return n > 0 ? std::to_string(n) : std::string{};
}

bool MailIndicator::onClick(double, double) {
    launchIfPossible(backends_, "xdg-open mailto:");
    return true;
}

REGISTER_INDICATOR("usb", Zone::Right, 260, UsbIndicator)
REGISTER_INDICATOR("usb-eject", Zone::Right, 261, UsbEjectIndicator)
REGISTER_INDICATOR("kde-connect", Zone::Right, 262, KdeConnectIndicator)
REGISTER_INDICATOR("phone-connect", Zone::Right, 263, PhoneConnectIndicator)
REGISTER_INDICATOR("whatsapp", Zone::Right, 264, WhatsappIndicator)
REGISTER_INDICATOR("email", Zone::Right, 265, EmailIndicator)
REGISTER_INDICATOR("mail", Zone::Right, 266, MailIndicator)

}  // namespace qypr
