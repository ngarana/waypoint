// TrayAppBinding.cpp - Forward module gestures to the claimed tray app.
#include "ui/indicators/TrayAppBinding.hpp"

#include <utility>

#include "system/SNIBackend.hpp"
#include "ui/statusbar/MenuPopover.hpp"
#include "ui/statusbar/MenuSource.hpp"
#include "ui/statusbar/StatusArea.hpp"

namespace qypr {

TrayAppBinding::TrayAppBinding(const SystemBackends& backends, std::string moduleId)
    : sni_(backends.sni),
      menus_(backends.dbusMenu),
      area_(backends.statusArea),
      moduleId_(std::move(moduleId)) {}

const SNIItem* TrayAppBinding::item() const {
    if (sni_ == nullptr || area_ == nullptr) return nullptr;
    const int i = area_->claimedItem(moduleId_, sni_);
    return i < 0 ? nullptr : &sni_->items()[static_cast<size_t>(i)];
}

std::unique_ptr<MenuSource> TrayAppBinding::menuSection() const {
    const SNIItem* it = item();
    if (it == nullptr || it->menuPath.empty() || menus_ == nullptr) return nullptr;
    return std::make_unique<DbusMenuSource>(menus_, it->service, it->menuPath);
}

std::unique_ptr<DetailedPopover> TrayAppBinding::contextMenu(double x, double y,
                                                             std::unique_ptr<MenuSource> native,
                                                             const std::string& title) const {
    auto menu = std::make_unique<CompositeMenu>();
    if (native) menu->addSection(std::move(native));
    if (auto app = menuSection()) menu->addSection(std::move(app));
    auto popover = std::make_unique<MenuPopover>(std::move(menu), title);
    if (popover->hasItems()) return popover;
    // Nothing to draw: a menu-less applet may still pop its own menu.
    if (const SNIItem* it = item()) {
        sni_->contextMenu(static_cast<size_t>(sni_->indexOf(it->service, it->path)),
                          static_cast<int>(x), static_cast<int>(y));
    }
    return nullptr;
}

bool TrayAppBinding::secondaryActivate(double x, double y) const {
    const SNIItem* it = item();
    if (it == nullptr) return false;
    sni_->secondaryActivate(static_cast<size_t>(sni_->indexOf(it->service, it->path)),
                            static_cast<int>(x), static_cast<int>(y));
    return true;
}

bool TrayAppBinding::activate(double x, double y) const {
    const SNIItem* it = item();
    if (it == nullptr) return false;
    sni_->activate(static_cast<size_t>(sni_->indexOf(it->service, it->path)), static_cast<int>(x),
                   static_cast<int>(y));
    return true;
}

}  // namespace qypr
