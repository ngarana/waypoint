// AdditionalIndicators.cpp - The whatsapp and email app modules.
#include "ui/indicators/AdditionalIndicators.hpp"

#include <algorithm>
#include <cctype>
#include <utility>

#include "system/SNIBackend.hpp"
#include "ui/statusbar/IndicatorRegistry.hpp"
#include "ui/statusbar/StatusArea.hpp"

namespace qypr {

AppModuleIndicator::AppModuleIndicator(const SystemBackends& backends, const std::string& id,
                                       int priority, std::string label, std::string glyph,
                                       std::string fallback)
    : AppStatusIndicator(backends, id, priority),
      label_(std::move(label)),
      glyph_(std::move(glyph)),
      fallback_(std::move(fallback)),
      tray_(backends, id) {}

std::vector<std::string> AppModuleIndicator::patterns() const {
    // In a bar the status area adds the [tray] claims; a free-standing
    // module uses the defaults.
    return backends_.statusArea != nullptr ? backends_.statusArea->claimPatterns(id())
                                           : StatusArea::defaultClaims(id());
}

std::vector<AppStatus> AppModuleIndicator::select(std::vector<AppStatus> all) const {
    const auto claims = patterns();
    std::erase_if(all, [&](const AppStatus& app) {
        return !app.matchesAny(claims) || (!app.hasNews() && app.windows.empty());
    });
    // Nothing on screen, but the app sits in the tray (its icon is claimed
    // by this module, so this is the only place it shows).
    if (all.empty()) {
        if (const SNIItem* item = tray_.item()) {
            AppStatus app;
            app.key = item->id;
            std::ranges::transform(app.key, app.key.begin(),
                                   [](unsigned char c) { return std::tolower(c); });
            app.name = !item->title.empty() ? item->title : label_;
            app.icon = !item->iconName.empty() ? item->iconName : glyph_;
            all.push_back(std::move(app));
        }
    }
    return all;
}

WhatsappIndicator::WhatsappIndicator(const SystemBackends& backends)
    : AppModuleIndicator(backends, "whatsapp", 264, "WhatsApp", "\U000F05A3",
                         "xdg-open https://web.whatsapp.com") {}

EmailIndicator::EmailIndicator(const SystemBackends& backends)
    : AppModuleIndicator(backends, "email", 265, "Email", "\U000F01F0", "xdg-open mailto:") {}

REGISTER_SESSION_INDICATOR("whatsapp", Zone::Right, 264, WhatsappIndicator)
REGISTER_SESSION_INDICATOR("email", Zone::Right, 265, EmailIndicator)
REGISTER_SESSION_INDICATOR("mail", Zone::Right, 266, MailIndicator)

}  // namespace qypr
