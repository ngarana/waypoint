// AdditionalIndicators.cpp - Extra status bar icons (KDE Connect, WhatsApp, email).
#include "ui/indicators/AdditionalIndicators.hpp"

#include <csignal>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "notifications/NotificationActions.hpp"
#include "notifications/NotificationMonitor.hpp"
#include "system/DesktopIndex.hpp"
#include "system/ToplevelBackend.hpp"
#include "ui/statusbar/IndicatorRegistry.hpp"

namespace qypr {

namespace {

std::string lower(std::string s) {
    std::ranges::transform(s, s.begin(), [](unsigned char c) { return std::tolower(c); });
    return s;
}

std::vector<const Notification*> getMatchingNotifications(const SystemBackends& b,
                                                          const std::vector<std::string>& needles) {
    std::vector<const Notification*> matched;
    if (b.notifications == nullptr) return matched;
    for (const auto& note : b.notifications->notifications()) {
        const std::string app = lower(note.app);
        const std::string de = lower(note.desktopEntry);
        for (const auto& needle : needles) {
            if (app.find(needle) != std::string::npos ||
                (!de.empty() && de.find(needle) != std::string::npos)) {
                matched.push_back(&note);
                break;
            }
        }
    }
    return matched;
}

int countMatchingNotifications(const SystemBackends& b, const std::vector<std::string>& needles) {
    return static_cast<int>(getMatchingNotifications(b, needles).size());
}

void dismissMatchingNotifications(const SystemBackends& b,
                                  const std::vector<std::string>& needles) {
    if (b.notificationActions == nullptr) return;
    const auto matched = getMatchingNotifications(b, needles);
    for (const auto* n : matched) {
        if (n->daemonId != 0) { b.notificationActions->close(n->daemonId); }
    }
}

bool activateOrLaunchApp(const SystemBackends& b, const std::vector<std::string>& appKeywords,
                         const std::string& fallbackCmd) {
    if (b.toplevel != nullptr) {
        const auto snap = b.toplevel->snapshot();
        for (const auto& w : snap.windows) {
            const std::string wId = lower(w.appId);
            const std::string wTitle = lower(w.title);
            for (const auto& kw : appKeywords) {
                if (wId.find(kw) != std::string::npos || wTitle.find(kw) != std::string::npos) {
                    b.toplevel->activate(w.id);
                    return true;
                }
            }
        }
    }

    if (b.desktopIndex != nullptr && b.loop != nullptr) {
        if (!b.desktopIndex->loaded()) { b.desktopIndex->load(); }
        for (const auto& kw : appKeywords) {
            const DesktopEntry* entry = b.desktopIndex->resolve(kw);
            if (entry != nullptr) {
                launchDetached(*b.loop, entry->exec, entry->terminal);
                return true;
            }
        }
    }

    if (b.loop != nullptr && !fallbackCmd.empty()) {
        launchDetached(*b.loop, fallbackCmd, /*terminal=*/false);
        return true;
    }

    return false;
}

bool isProcessRunning(const std::vector<std::string>& procNames, pid_t& cachedPid) {
    if (cachedPid > 0 && kill(cachedPid, 0) == 0) {
        std::ifstream comm("/proc/" + std::to_string(cachedPid) + "/comm");
        std::string name;
        if (comm >> name) {
            for (const auto& target : procNames) {
                if (name == target) return true;
            }
        }
    }
    cachedPid = 0;
    std::error_code ec;
    if (!std::filesystem::exists("/proc", ec)) return false;
    for (const auto& entry : std::filesystem::directory_iterator("/proc", ec)) {
        const std::string fn = entry.path().filename().string();
        if (fn.empty() || !std::isdigit(static_cast<unsigned char>(fn[0]))) continue;
        std::ifstream comm(entry.path() / "comm");
        std::string name;
        if (comm >> name) {
            for (const auto& target : procNames) {
                if (name == target) {
                    cachedPid = static_cast<pid_t>(std::strtol(fn.c_str(), nullptr, 10));
                    return true;
                }
            }
        }
    }
    return false;
}

}  // namespace

// --- KdeConnectIndicator ---

KdeConnectIndicator::KdeConnectIndicator(const SystemBackends& backends)
    : SimpleIconIndicator(backends, "kde-connect", Zone::Right, 262, "󰄜", "KDE Connect",
                          "smartphone-symbolic") {
    updateRunningState();
}

void KdeConnectIndicator::poll(int64_t now) {
    if (now - lastCheckMs_ >= 1000) {
        lastCheckMs_ = now;
        updateRunningState();
    }
}

void KdeConnectIndicator::onBackendUpdate() {
    updateRunningState();
}

void KdeConnectIndicator::updateRunningState() {
    const int notifCount = countMatchingNotifications(backends_, {"kdeconnect", "kde-connect"});
    bool windowOpen = false;
    if (backends_.toplevel != nullptr) {
        for (const auto& w : backends_.toplevel->snapshot().windows) {
            const std::string wId = lower(w.appId);
            if (wId.find("kdeconnect") != std::string::npos) {
                windowOpen = true;
                break;
            }
        }
    }
    const bool procActive =
        isProcessRunning({"kdeconnectd", "kdeconnect-app", "kdeconnect-indicator"}, cachedPid_);
    running_ = (notifCount > 0) || windowOpen || procActive;
    visible = running_;
    loaded_ = true;

    if (notifCount > 0) {
        tooltipText_ = "KDE Connect (" + std::to_string(notifCount) + " notification" +
                       (notifCount > 1 ? "s" : "") + ")";
    } else {
        tooltipText_ = "KDE Connect (Connected)";
    }
}

std::string KdeConnectIndicator::label() const {
    const int n = countMatchingNotifications(backends_, {"kdeconnect", "kde-connect"});
    return n > 0 ? std::to_string(n) : std::string{};
}

bool KdeConnectIndicator::onClick(double, double) {
    activateOrLaunchApp(backends_, {"kdeconnect", "kde-connect"}, "kdeconnect-app");
    dismissMatchingNotifications(backends_, {"kdeconnect", "kde-connect"});
    return true;
}

bool KdeConnectIndicator::onSecondaryClick(double, double) {
    dismissMatchingNotifications(backends_, {"kdeconnect", "kde-connect"});
    return true;
}

// --- WhatsappIndicator ---

WhatsappIndicator::WhatsappIndicator(const SystemBackends& backends)
    : SimpleIconIndicator(backends, "whatsapp", Zone::Right, 264, "󰖣", "WhatsApp",
                          "whatsapp-symbolic") {
    updateRunningState();
}

void WhatsappIndicator::poll(int64_t) {
    updateRunningState();
}

void WhatsappIndicator::onBackendUpdate() {
    updateRunningState();
}

void WhatsappIndicator::updateRunningState() {
    const int notifCount = countMatchingNotifications(backends_, {"whatsapp", "whats"});
    bool windowOpen = false;
    if (backends_.toplevel != nullptr) {
        for (const auto& w : backends_.toplevel->snapshot().windows) {
            const std::string wId = lower(w.appId);
            if (wId.find("whatsapp") != std::string::npos) {
                windowOpen = true;
                break;
            }
        }
    }
    running_ = (notifCount > 0) || windowOpen;
    visible = running_;
    loaded_ = true;
}

std::string WhatsappIndicator::icon() const {
    return countMatchingNotifications(backends_, {"whatsapp", "whats"}) > 0 ? "󰖣" : "󰖥";
}

std::string WhatsappIndicator::label() const {
    const int n = countMatchingNotifications(backends_, {"whatsapp", "whats"});
    return n > 0 ? std::to_string(n) : std::string{};
}

bool WhatsappIndicator::onClick(double, double) {
    activateOrLaunchApp(backends_, {"whatsapp", "whatsapp-for-linux"},
                        "xdg-open https://web.whatsapp.com");
    dismissMatchingNotifications(backends_, {"whatsapp", "whats"});
    return true;
}

bool WhatsappIndicator::onSecondaryClick(double, double) {
    dismissMatchingNotifications(backends_, {"whatsapp", "whats"});
    return true;
}

// --- EmailIndicator ---

EmailIndicator::EmailIndicator(const SystemBackends& backends)
    : SimpleIconIndicator(backends, "email", Zone::Right, 265, "󰇰", "Email",
                          "mail-unread-symbolic") {
    updateRunningState();
}

void EmailIndicator::poll(int64_t) {
    updateRunningState();
}

void EmailIndicator::onBackendUpdate() {
    updateRunningState();
}

void EmailIndicator::updateRunningState() {
    const int notifCount = countMatchingNotifications(
        backends_, {"mail", "thunderbird", "evolution", "geary", "kmail"});
    bool windowOpen = false;
    if (backends_.toplevel != nullptr) {
        for (const auto& w : backends_.toplevel->snapshot().windows) {
            const std::string wId = lower(w.appId);
            if (wId.find("thunderbird") != std::string::npos ||
                wId.find("evolution") != std::string::npos ||
                wId.find("geary") != std::string::npos || wId.find("mail") != std::string::npos) {
                windowOpen = true;
                break;
            }
        }
    }
    running_ = (notifCount > 0) || windowOpen;
    visible = running_;
    loaded_ = true;
}

std::string EmailIndicator::icon() const {
    return countMatchingNotifications(backends_,
                                      {"mail", "thunderbird", "evolution", "geary", "kmail"}) > 0
               ? "󰇰"
               : "󰇮";
}

std::string EmailIndicator::label() const {
    const int n = countMatchingNotifications(
        backends_, {"mail", "thunderbird", "evolution", "geary", "kmail"});
    return n > 0 ? std::to_string(n) : std::string{};
}

bool EmailIndicator::onClick(double, double) {
    activateOrLaunchApp(backends_, {"thunderbird", "evolution", "geary", "kmail", "mail"},
                        "xdg-open mailto:");
    dismissMatchingNotifications(backends_, {"mail", "thunderbird", "evolution", "geary", "kmail"});
    return true;
}

bool EmailIndicator::onSecondaryClick(double, double) {
    dismissMatchingNotifications(backends_, {"mail", "thunderbird", "evolution", "geary", "kmail"});
    return true;
}

REGISTER_INDICATOR("kde-connect", Zone::Right, 262, KdeConnectIndicator)
REGISTER_INDICATOR("phone-connect", Zone::Right, 263, PhoneConnectIndicator)
REGISTER_INDICATOR("whatsapp", Zone::Right, 264, WhatsappIndicator)
REGISTER_INDICATOR("email", Zone::Right, 265, EmailIndicator)
REGISTER_INDICATOR("mail", Zone::Right, 266, MailIndicator)

}  // namespace qypr
