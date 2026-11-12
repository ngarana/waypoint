// SNITrayHost.cpp - Persistent application status and native app actions.
#include "ui/indicators/SNITrayHost.hpp"
#include <algorithm>
#include <utility>
#include <xkbcommon/xkbcommon-keysyms.h>
#include "render/IconResolver.hpp"
#include "render/Painter.hpp"
#include "system/DbusMenuBackend.hpp"
#include "ui/Theme.hpp"
#include "ui/indicators/AppTile.hpp"
#include "ui/statusbar/IndicatorRegistry.hpp"
#include "ui/statusbar/MenuPopover.hpp"

namespace qypr {
namespace {
constexpr double kCell = 28;
constexpr double kPad = 4;
constexpr size_t kVisibleLimit = 6;

// Preserve registration order. Passive and excess items remain accessible in
// overflow. Missing icon themes never remove the app's hit target.
std::vector<size_t> stripItems(const SNIBackend* backend) {
    std::vector<size_t> result;
    if (!backend) return result;
    const auto& items = backend->items();
    for (size_t i = 0; i < items.size(); ++i) {
        if (items[i].status != "Passive" && result.size() < kVisibleLimit) result.push_back(i);
    }
    return result;
}
std::vector<size_t> hiddenItems(const SNIBackend* backend) {
    const auto strip = stripItems(backend);
    std::vector<size_t> result;
    if (!backend) return result;
    for (size_t i = 0; i < backend->items().size(); ++i) {
        if (std::ranges::find(strip, i) == strip.end()) result.push_back(i);
    }
    return result;
}
std::string title(const SNIItem& item) {
    return item.title.empty() ? item.service : item.title;
}
void drawIcon(Painter& p, const SNIItem& item, const Rect& box, const theme::State& th) {
    auto* surface = item.iconName.empty() ? nullptr : IconResolver::instance().get(item.iconName);
    if (!surface) surface = item.pixmap;
    const Rect icon{box.x + (box.w - 18) / 2, box.y + (box.h - 18) / 2, 18, 18};
    if (surface) {
        if (apptile::surfaceIsMonochrome(surface))
            p.drawSurfaceTinted(surface, icon, th.colors.text);
        else
            p.drawSurface(surface, icon);
    } else {
        p.fillRoundedRect(icon, 5, apptile::fallbackColor(title(item), th));
        const TextStyle ts{th.font.family, 11, PANGO_WEIGHT_BOLD, th.colors.background};
        const auto initial = apptile::initialFor(title(item));
        const auto size = p.measureText(initial, ts);
        p.drawText(icon.x + (18 - size.w) / 2, icon.y + (18 - size.h) / 2, initial, ts);
    }
    if (item.status == "NeedsAttention")
        p.fillCircle(box.x + box.w - 4, box.y + 5, 3, th.colors.primary);
}

class OverflowPopover : public DetailedPopover {
public:
    OverflowPopover(SNIBackend* backend, DbusMenuBackend* menus)
        : backend_(backend),
          menus_(menus) {}
    [[nodiscard]] double contentWidth() const override {
        return menu_ ? menu_->contentWidth() : 300;
    }
    [[nodiscard]] double contentHeight() const override {
        return menu_ ? menu_->contentHeight()
                     : 48 + 40 * static_cast<double>(
                                     std::max(size_t{1}, hiddenItems(backend_).size()));
    }
    [[nodiscard]] bool wantsKeyboard() const override { return true; }
    void draw(Painter& p, int64_t now) override {
        const auto b = getBounds();
        if (menu_) {
            menu_->anchorX = b.x + b.w;
            menu_->anchorY = b.y;
            menu_->draw(p, now);
            return;
        }
        if (!drawSharedBackdrop(p, b, theme().statusbar.popoverRadius))
            p.fillRoundedRectSource(b, theme().statusbar.popoverRadius, theme().panelSurface());
        const TextStyle hdr{theme().font.family, 12, PANGO_WEIGHT_BOLD, theme().colors.textSubtle};
        p.drawText(b.x + 14, b.y + 14, "Background applications", hdr);
        rows_.clear();
        double y = b.y + 40;
        for (const auto index : hiddenItems(backend_)) {
            const auto& item = backend_->items()[index];
            const Rect r{b.x + 8, y, b.w - 16, 40};
            // Store identities, never pointers into the backend's replaceable vector.
            rows_.push_back({r, item.service, item.path});
            if (selected_ == static_cast<int>(rows_.size()) - 1)
                p.fillRoundedRect(r, 6, theme().colors.glassHover);
            drawIcon(p, item, {r.x + 4, y + 6, 28, 28}, theme());
            const TextStyle label{theme().font.family, 13, PANGO_WEIGHT_NORMAL,
                                  theme().colors.text};
            p.drawText(r.x + 40, y + 10, title(item), label, HAlign::Left, r.w - 48);
            y += 40;
        }
        if (rows_.empty()) p.drawText(b.x + 14, y + 8, "No hidden applications", hdr);
    }
    bool handleClick(double x, double y) override {
        if (menu_) return menu_->handleClick(x, y);
        return activate(x, y, false);
    }
    bool handleSecondaryClick(double x, double y) override { return activate(x, y, true); }
    bool handleMotion(double x, double y) override {
        if (menu_) return menu_->handleMotion(x, y);
        selected_ = -1;
        for (size_t i = 0; i < rows_.size(); ++i)
            if (rows_[i].rect.contains(x, y)) selected_ = static_cast<int>(i);
        return contains(x, y);
    }
    bool handleKey(uint32_t key) override {
        if (menu_) return menu_->handleKey(key);
        if (rows_.empty()) return false;
        const auto count = static_cast<int>(rows_.size());
        if (key == XKB_KEY_Down || key == XKB_KEY_Tab) {
            selected_ = (selected_ + 1) % count;
        } else if (key == XKB_KEY_Up || key == XKB_KEY_ISO_Left_Tab) {
            selected_ = (selected_ + count - 1) % count;
        } else if (selected_ >= 0 && selected_ < count &&
                   (key == XKB_KEY_Return || key == XKB_KEY_space || key == XKB_KEY_Menu)) {
            const auto r = rows_[selected_].rect;
            return activate(r.x + 1, r.y + 1, key == XKB_KEY_Menu);
        } else {
            return false;
        }
        return true;
    }
    bool consumeCloseRequest() override {
        if (menu_ && menu_->consumeCloseRequest()) return true;
        return std::exchange(close_, false);
    }

private:
    bool activate(double x, double y, bool context) {
        if (menu_) return false;
        for (const auto& row : rows_) {
            if (!row.rect.contains(x, y)) continue;
            const auto& items = backend_->items();
            for (size_t i = 0; i < items.size(); ++i) {
                const auto& item = items[i];
                if (item.service != row.service || item.path != row.path) continue;
                if (context || item.itemIsMenu) {
                    if (menus_ && !item.menuPath.empty()) {
                        auto root = menus_->fetch(item.service, item.menuPath, 0);
                        menu_ = std::make_unique<MenuPopover>(menus_, item.service, item.menuPath,
                                                              std::move(root), title(item));
                        menu_->setTheme(theme());
                        menu_->open();
                    } else {
                        backend_->contextMenu(i, static_cast<int>(x), static_cast<int>(y));
                        close_ = true;
                    }
                } else {
                    backend_->activate(i, static_cast<int>(x), static_cast<int>(y));
                    close_ = true;
                }
                return true;
            }
            return true;  // Removed app: never activate the replacement at its old index.
        }
        return false;
    }
    struct Row {
        Rect rect;
        std::string service, path;
    };
    SNIBackend* backend_;
    DbusMenuBackend* menus_;
    std::vector<Row> rows_;
    std::unique_ptr<MenuPopover> menu_;
    int selected_ = -1;
    bool close_ = false;
};
}  // namespace

SNITrayHost::SNITrayHost(const SystemBackends& b)
    : StatusIndicator("sni", Zone::Right, 600),
      backend_(b.sni),
      dbusMenu_(b.dbusMenu) {
    visible = false;
}
void SNITrayHost::onBackendUpdate() {
    visible = backend_ && !backend_->items().empty();
}
double SNITrayHost::measureWidth(Painter& p) {
    (void)p;
    if (!backend_ || backend_->items().empty()) return 0;
    return 2 * kPad + kCell * static_cast<double>(stripItems(backend_).size() +
                                                  (!hiddenItems(backend_).empty() ? 1 : 0));
}
int SNITrayHost::iconIndexAt(double x) const {
    const double offset = x - bounds.x - kPad;
    const auto strip = stripItems(backend_);
    if (offset < 0 || offset >= kCell * static_cast<double>(strip.size())) return -1;
    return static_cast<int>(strip[static_cast<size_t>(offset / kCell)]);
}
bool SNITrayHost::overOverflow(double x) const {
    const double start = bounds.x + kPad + kCell * static_cast<double>(stripItems(backend_).size());
    return !hiddenItems(backend_).empty() && x >= start && x < start + kCell;
}
void SNITrayHost::onHoverMotion(double x, double y) {
    (void)y;
    hoverX_ = x;
}
std::string SNITrayHost::tooltip() const {
    if (overOverflow(hoverX_)) return "Show hidden applications";
    const int index = iconIndexAt(hoverX_);
    if (index >= 0) {
        const auto& item = backend_->items()[index];
        return title(item) + (item.status == "NeedsAttention" ? " — Needs attention" : "");
    }
    if (!backend_ || backend_->items().empty()) return "System tray";
    return std::to_string(backend_->items().size()) + " tray items";
}
void SNITrayHost::draw(Painter& p, int64_t now) {
    (void)now;
    if (!visible || !backend_) return;
    double x = bounds.x + kPad;
    for (const auto index : stripItems(backend_)) {
        const Rect r{x, bounds.y + 2, kCell, bounds.h - 4};
        if (hoverX_ >= x && hoverX_ < x + kCell) p.fillRoundedRect(r, 6, theme().colors.glassHover);
        drawIcon(p, backend_->items()[index], r, theme());
        x += kCell;
    }
    if (!hiddenItems(backend_).empty()) {
        const Rect r{x, bounds.y + 2, kCell, bounds.h - 4};
        if (overOverflow(hoverX_)) p.fillRoundedRect(r, 6, theme().colors.glassHover);
        const TextStyle ts{theme().font.family, 16, PANGO_WEIGHT_BOLD, theme().colors.text};
        p.drawText(x + 8, bounds.y + (bounds.h - 18) / 2, "⌄", ts);
    }
}
bool SNITrayHost::onClick(double x, double y) {
    pendingMenu_ = -1;
    if (overOverflow(x)) {
        pendingMenu_ = -2;
        return false;
    }
    const int i = iconIndexAt(x);
    if (i < 0) return false;
    if (backend_->items()[i].itemIsMenu) {
        onSecondaryClick(x, y);
        return pendingMenu_ == -1;
    }
    backend_->activate(i, static_cast<int>(x), static_cast<int>(y));
    return true;
}
bool SNITrayHost::onSecondaryClick(double x, double y) {
    pendingMenu_ = -1;
    if (overOverflow(x)) {
        pendingMenu_ = -2;
        return true;
    }
    const int i = iconIndexAt(x);
    if (i < 0) return false;
    if (dbusMenu_ && !backend_->items()[i].menuPath.empty())
        pendingMenu_ = i;
    else
        backend_->contextMenu(i, static_cast<int>(x), static_cast<int>(y));
    return true;
}
bool SNITrayHost::onMiddleClick(double x, double y) {
    const int i = iconIndexAt(x);
    if (i < 0) return false;
    backend_->secondaryActivate(i, static_cast<int>(x), static_cast<int>(y));
    return true;
}
bool SNITrayHost::onScroll(double dx, double dy, double x, double y) {
    (void)y;
    const int i = iconIndexAt(x);
    if (i < 0) return false;
    backend_->scroll(i, static_cast<int>(dx), static_cast<int>(dy));
    return true;
}
std::unique_ptr<DetailedPopover> SNITrayHost::createDetailedView() {
    const int i = std::exchange(pendingMenu_, -1);
    if (i == -2) return std::make_unique<OverflowPopover>(backend_, dbusMenu_);
    if (!dbusMenu_ || !backend_ || i < 0 || static_cast<size_t>(i) >= backend_->items().size())
        return nullptr;
    const auto& item = backend_->items()[i];
    auto root = dbusMenu_->fetch(item.service, item.menuPath, 0);
    return std::make_unique<MenuPopover>(dbusMenu_, item.service, item.menuPath, std::move(root),
                                         title(item));
}
REGISTER_SESSION_INDICATOR("sni", Zone::Right, 600, SNITrayHost)
}  // namespace qypr
