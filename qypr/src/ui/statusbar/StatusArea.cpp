// StatusArea.cpp - Tray claims, background-app placement, overflow requests.
#include "ui/statusbar/StatusArea.hpp"

#include <algorithm>
#include <cctype>

#include "core/Config.hpp"
#include "system/SNIBackend.hpp"

namespace qypr {

namespace {

std::string lower(std::string s) {
    std::ranges::transform(s, s.begin(), [](unsigned char c) { return std::tolower(c); });
    return s;
}

std::vector<std::string> lowerAll(std::vector<std::string> v) {
    for (auto& s : v) s = lower(std::move(s));
    std::erase_if(v, [](const std::string& s) { return s.empty(); });
    return v;
}

}  // namespace

std::vector<std::string> StatusArea::defaultClaims(const std::string& moduleId) {
    // Ids verified against the running applets where available (nm-applet,
    // blueman); the rest are the ids those projects register.
    if (moduleId == "wifi") {
        return {"nm-applet", "nm-tray", "network-manager-applet",
                "org.freedesktop.network-manager-applet"};
    }
    if (moduleId == "bluetooth") return {"blueman", "blueman-*", "blueberry*"};
    if (moduleId == "volume") return {"pasystray"};
    if (moduleId == "removable-devices") return {"udiskie"};
    if (moduleId == "phone-link") return {"kdeconnect*", "org.kde.kdeconnect*", "kde connect"};
    // App modules: matched against an app's desktop id, name and the names
    // its notifications and windows use (AppStatus::matches).
    if (moduleId == "whatsapp") return {"*whatsapp*"};
    if (moduleId == "email") {
        return {"category:email",      "*mail*",      "thunderbird*",   "org.mozilla.thunderbird*",
                "net.thunderbird*",    "betterbird*", "eu.betterbird*", "evolution",
                "org.gnome.evolution", "geary",       "org.gnome.geary"};
    }
    return {};
}

const std::vector<std::string>& StatusArea::appModules() {
    static const std::vector<std::string> kModules = {"whatsapp", "email"};
    return kModules;
}

std::vector<std::string> StatusArea::claimPatterns(const std::string& moduleId) const {
    return inLayout(moduleId) ? claimsFor(moduleId) : std::vector<std::string>{};
}

bool StatusArea::patternMatches(const std::string& pattern, const std::string& value) {
    if (pattern.empty() || value.empty()) return false;
    const std::string v = lower(value);
    const bool head = pattern.front() == '*';
    const bool tail = pattern.size() > 1 && pattern.back() == '*';
    const std::string_view core(pattern.data() + (head ? 1 : 0),
                                pattern.size() - (head ? 1 : 0) - (tail ? 1 : 0));
    if (head && tail) return v.find(core) != std::string::npos;
    if (head) return v.ends_with(core);
    if (tail) return v.starts_with(core);
    return v == pattern;
}

void StatusArea::configure(const Config* config) {
    extraClaims_.clear();
    pinned_.clear();
    hidden_.clear();
    if (config == nullptr) return;
    for (const char* module :
         {"wifi", "bluetooth", "volume", "removable-devices", "phone-link", "whatsapp", "email"}) {
        auto extra = lowerAll(config->getList("tray", std::string("claims-") + module));
        if (!extra.empty()) extraClaims_.emplace_back(module, std::move(extra));
    }
    pinned_ = lowerAll(config->getList("tray", "pinned"));
    hidden_ = lowerAll(config->getList("tray", "hidden"));
}

bool StatusArea::inLayout(const std::string& moduleId) const {
    return std::ranges::find(layout_, moduleId) != layout_.end();
}

std::vector<std::string> StatusArea::claimsFor(const std::string& moduleId) const {
    auto claims = defaultClaims(moduleId);
    for (const auto& [module, extra] : extraClaims_) {
        if (module == moduleId) claims.insert(claims.end(), extra.begin(), extra.end());
    }
    return claims;
}

bool StatusArea::claimedBy(const std::string& moduleId, const std::string& key) const {
    if (key.empty() || !inLayout(moduleId)) return false;
    return std::ranges::any_of(claimsFor(moduleId),
                               [&](const std::string& p) { return patternMatches(p, key); });
}

std::string StatusArea::claimant(const std::string& key) const {
    for (const auto& module : layout_) {
        if (claimedBy(module, key)) return module;
    }
    return {};
}

int StatusArea::claimedItem(const std::string& moduleId, const SNIBackend* sni) const {
    if (sni == nullptr) return -1;
    const auto& items = sni->items();
    for (size_t i = 0; i < items.size(); ++i) {
        if (claimedBy(moduleId, items[i].id)) return static_cast<int>(i);
    }
    return -1;
}

bool StatusArea::itemClaimed(const SNIItem& item) const {
    return !claimant(item.id).empty();
}

bool StatusArea::pinned(const std::string& key) const {
    return !key.empty() && std::ranges::find(pinned_, lower(key)) != pinned_.end();
}

bool StatusArea::hidden(const std::string& key) const {
    return !key.empty() && std::ranges::find(hidden_, lower(key)) != hidden_.end();
}

}  // namespace qypr
