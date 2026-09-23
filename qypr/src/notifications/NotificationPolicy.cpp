// NotificationPolicy.cpp - See the header for the design.

#include "notifications/NotificationPolicy.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <ranges>

namespace qypr {

NotifyHints applyHint(const std::string& key, bool numeric, uint64_t num, const std::string& str,
                      NotifyHints h) {
    if (numeric) {
        if (key == "urgency") {
            h.urgency = static_cast<uint8_t>(num);
        } else if (key == "transient") {
            h.transient = num != 0;
        } else if (key == "sensitive" || key == "x-kde-privacy") {
            h.sensitive = h.sensitive || (num != 0);
        } else if (key == "visibility") {
            h.sensitive = h.sensitive || (num < 2);
        }
    } else {
        if (key == "visibility") {
            if (str == "private" || str == "secret") { h.sensitive = true; }
        } else if (key == "desktop-entry" || key == "desktop_entry") {
            // The .desktop id of the sending app — used to launch/focus
            // it when the user clicks the card.
            h.desktopEntry = str;
        }
    }
    return h;
}

std::vector<std::string> NotificationPolicy::defaultSensitiveApps() {
    return {"signal",      "telegram",  "whatsapp",  "discord",
            "thunderbird", "evolution", "messenger", "org.telegram.desktop",
            "slack",       "element"};
}

void NotificationPolicy::load(const std::string& path) {
    if (loaded_) { return; }
    loaded_ = true;
    std::ifstream f(path);
    if (!f.is_open()) {
        sensitiveApps_ = defaultSensitiveApps();
        return;
    }
    std::string line;
    while (std::getline(f, line)) {
        line.erase(line.begin(),
                   std::ranges::find_if(line, [](unsigned char ch) { return !std::isspace(ch); }));
        line.erase(std::ranges::find_if(std::views::reverse(line),
                                        [](unsigned char ch) { return !std::isspace(ch); })
                       .base(),
                   line.end());
        if (!line.empty() && line[0] != '#') {
            std::ranges::transform(line, line.begin(), ::tolower);
            sensitiveApps_.push_back(line);
        }
    }
}

bool NotificationPolicy::isSensitiveApp(const std::string& appName) const {
    std::string app = appName;
    std::ranges::transform(app, app.begin(), ::tolower);
    return std::ranges::any_of(sensitiveApps_, [&](const std::string& sensitive) {
        return app.find(sensitive) != std::string::npos;
    });
}

}  // namespace qypr
