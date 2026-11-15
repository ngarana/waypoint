// BackgroundAppsIndicator.cpp - Overflow chevron: passive modules and tray apps.
#include "ui/indicators/BackgroundAppsIndicator.hpp"

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
#include "ui/statusbar/StatusArea.hpp"

namespace qypr {

namespace {

constexpr double kCell = 28;
constexpr double kPad = 4;
constexpr double kRowH = 42;
constexpr double kSectionH = 24;
constexpr double kPopPad = 8;
constexpr double kPopW = 320;
// Points the way the overflow opens.
constexpr const char* kChevronDown = "\U000F0140";  // nf-md-chevron_down
constexpr const char* kChevronUp = "\U000F0143";    // nf-md-chevron_up

std::string title(const SNIItem& item) {
    if (!item.title.empty()) { return item.title; }
    return item.id.empty() ? item.service : item.id;
}

// A key the user can write in [tray] pinned / hidden: the item's Id, or its
// title.
bool listed(const SNIItem& item, const StatusArea* area,
            bool (StatusArea::*test)(const std::string&) const) {
    return area != nullptr && ((area->*test)(item.id) || (area->*test)(item.title));
}

bool eligible(const SNIItem& item, const StatusArea* area) {
    if (area == nullptr) { return true; }
    return !area->itemClaimed(item) && !area->hidden(item.id) && !area->hidden(item.title);
}

void drawItemIcon(Painter& p, const SNIItem& item, const Rect& box, const theme::State& th) {
    auto* surface = item.iconName.empty() ? nullptr : IconResolver::instance().get(item.iconName);
    if (surface == nullptr) { surface = item.pixmap; }
    const Rect icon{box.x + ((box.w - 18) / 2), box.y + ((box.h - 18) / 2), 18, 18};
    if (surface != nullptr) {
        if (apptile::surfaceIsMonochrome(surface)) {
            p.drawSurfaceTinted(surface, icon, th.colors.text);
        } else {
            p.drawSurface(surface, icon);
        }
    } else {
        p.fillRoundedRect(icon, 5, apptile::fallbackColor(title(item), th));
        const TextStyle ts{th.font.family, 11, PANGO_WEIGHT_BOLD, th.colors.background};
        const auto initial = apptile::initialFor(title(item));
        const auto size = p.measureText(initial, ts);
        p.drawText(icon.x + ((18 - size.w) / 2), icon.y + ((18 - size.h) / 2), initial, ts);
    }
    if (item.status == "NeedsAttention") {
        p.fillCircle(box.x + box.w - 4, box.y + 5, 3, th.colors.primary);
    }
}

void drawEntryIcon(Painter& p, const OverflowEntry& e, const Rect& box, const theme::State& th) {
    auto* surface = e.iconName.empty() ? nullptr : IconResolver::instance().get(e.iconName);
    const Rect icon{box.x + ((box.w - 18) / 2), box.y + ((box.h - 18) / 2), 18, 18};
    if (surface != nullptr) {
        p.drawSurfaceTinted(surface, icon, th.colors.text);
    } else {
        const TextStyle g{th.font.family, 16, PANGO_WEIGHT_NORMAL, th.colors.primary};
        p.drawText(icon.x, icon.y - 1, e.glyph, g);
    }
    if (e.attention) { p.fillCircle(box.x + box.w - 4, box.y + 5, 3, th.colors.primary); }
}

// The overflow list. Rows store identities (module id; tray service + path),
// never indices into a list that can change under an open popover.
class OverflowPopover : public DetailedPopover {
public:
    OverflowPopover(SNIBackend* sni, DbusMenuBackend* menus, StatusArea* area, std::string hostId)
        : sni_(sni),
          menus_(menus),
          area_(area),
          hostId_(std::move(hostId)) {}

    [[nodiscard]] double contentWidth() const override {
        return menu_ ? menu_->contentWidth() : kPopW;
    }
    [[nodiscard]] double contentHeight() const override {
        if (menu_) { return menu_->contentHeight(); }
        const size_t passive = passiveEntries().size();
        const size_t apps = BackgroundAppsIndicator::overflowItems(sni_, area_).size();
        double h = 2 * kPopPad;
        if (passive > 0) { h += kSectionH + (kRowH * static_cast<double>(passive)); }
        if (apps > 0) { h += kSectionH + (kRowH * static_cast<double>(apps)); }
        return passive + apps == 0 ? h + kRowH : h;
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
        if (!drawSharedBackdrop(p, b, theme().statusbar.popoverRadius)) {
            p.fillRoundedRectSource(b, theme().statusbar.popoverRadius, theme().panelSurface());
        }
        rows_.clear();
        double y = b.y + kPopPad;
        const TextStyle section{.family = theme().font.family,
                                .size = 10.5,
                                .weight = PANGO_WEIGHT_BOLD,
                                .color = theme().colors.textMuted,
                                .letterSpacing = 0.6};
        const TextStyle label{theme().font.family, 13, PANGO_WEIGHT_NORMAL, theme().colors.text};
        const TextStyle sub{theme().font.family, 11, PANGO_WEIGHT_NORMAL,
                            theme().colors.textSubtle};

        auto rowFrame = [&](const Rect& r) {
            if (selected_ == static_cast<int>(rows_.size()) - 1) {
                p.fillRoundedRect(r, 6, theme().colors.glassHover);
            }
        };
        auto texts = [&](const Rect& r, const std::string& t, const std::string& s) {
            if (s.empty()) {
                p.drawText(r.x + 40, r.y + 12, t, label, HAlign::Left, r.w - 48);
            } else {
                p.drawText(r.x + 40, r.y + 4, t, label, HAlign::Left, r.w - 48);
                p.drawText(r.x + 40, r.y + 22, s, sub, HAlign::Left, r.w - 48);
            }
        };

        const auto passive = passiveEntries();
        if (!passive.empty()) {
            p.drawText(b.x + 14, y + 6, "STATUS", section);
            y += kSectionH;
            for (const auto& e : passive) {
                const Rect r{b.x + kPopPad, y, b.w - (2 * kPopPad), kRowH};
                rows_.push_back({.rect = r, .module = e.id});
                rowFrame(r);
                drawEntryIcon(p, e, {r.x + 4, y + 7, 28, 28}, theme());
                texts(r, e.title, e.subtitle);
                y += kRowH;
            }
        }
        const auto apps = BackgroundAppsIndicator::overflowItems(sni_, area_);
        if (!apps.empty()) {
            p.drawText(b.x + 14, y + 6, "BACKGROUND APPS", section);
            y += kSectionH;
            for (const auto index : apps) {
                const auto& item = sni_->items()[index];
                const Rect r{b.x + kPopPad, y, b.w - (2 * kPopPad), kRowH};
                rows_.push_back({.rect = r, .service = item.service, .path = item.path});
                rowFrame(r);
                drawItemIcon(p, item, {r.x + 4, y + 7, 28, 28}, theme());
                std::string detail;
                if (item.status == "NeedsAttention") {
                    detail = "Needs attention";
                } else if (!item.tooltipTitle.empty() && item.tooltipTitle != title(item)) {
                    detail = item.tooltipTitle;
                }
                texts(r, title(item), detail);
                y += kRowH;
            }
        }
        if (rows_.empty()) {
            p.drawText(b.x + 14, y + 12, "Nothing running in the background", sub);
        }
    }

    bool handleClick(double x, double y) override {
        if (menu_) { return menu_->handleClick(x, y); }
        return activate(x, y, false);
    }
    bool handleSecondaryClick(double x, double y) override {
        if (menu_) { return menu_->handleSecondaryClick(x, y); }
        return activate(x, y, true);
    }
    bool handleMotion(double x, double y) override {
        if (menu_) { return menu_->handleMotion(x, y); }
        selected_ = -1;
        for (size_t i = 0; i < rows_.size(); ++i) {
            if (rows_[i].rect.contains(x, y)) { selected_ = static_cast<int>(i); }
        }
        return contains(x, y);
    }
    bool handleKey(uint32_t key) override {
        if (menu_) { return menu_->handleKey(key); }
        if (rows_.empty()) { return false; }
        const auto count = static_cast<int>(rows_.size());
        if (key == XKB_KEY_Down || key == XKB_KEY_Tab) {
            selected_ = (selected_ + 1) % count;
        } else if (key == XKB_KEY_Up || key == XKB_KEY_ISO_Left_Tab) {
            selected_ = (selected_ + count - 1) % count;
        } else if (selected_ >= 0 && selected_ < count &&
                   (key == XKB_KEY_Return || key == XKB_KEY_space || key == XKB_KEY_Menu)) {
            const auto r = rows_[static_cast<size_t>(selected_)].rect;
            return activate(r.x + 1, r.y + 1, key == XKB_KEY_Menu);
        } else {
            return false;
        }
        return true;
    }
    bool consumeCloseRequest() override {
        if (menu_ && menu_->consumeCloseRequest()) { return true; }
        return std::exchange(close_, false);
    }

private:
    [[nodiscard]] std::vector<OverflowEntry> passiveEntries() const {
        return area_ != nullptr ? area_->passiveEntries() : std::vector<OverflowEntry>{};
    }

    bool activate(double x, double y, bool context) {
        const auto row =
            std::ranges::find_if(rows_, [&](const Row& r) { return r.rect.contains(x, y); });
        if (row == rows_.end()) { return false; }
        if (!row->module.empty()) {
            // Served by the StatusBar once this handler has returned: the
            // module's flyout replaces this popover, anchored to the chevron.
            if (area_ != nullptr) { area_->requestDetail(row->module, hostId_); }
            return true;
        }
        if (sni_ == nullptr) { return true; }
        const int i = sni_->indexOf(row->service, row->path);
        if (i < 0) { return true; }  // gone: never act on whatever took its place
        const auto& item = sni_->items()[static_cast<size_t>(i)];
        if (context || item.itemIsMenu) {
            if (menus_ != nullptr && !item.menuPath.empty()) {
                menu_ = std::make_unique<MenuPopover>(
                    std::make_unique<DbusMenuSource>(menus_, item.service, item.menuPath),
                    title(item));
                menu_->setTheme(theme());
                menu_->open();
            } else {
                sni_->contextMenu(static_cast<size_t>(i), static_cast<int>(x), static_cast<int>(y));
                close_ = true;
            }
        } else {
            sni_->activate(static_cast<size_t>(i), static_cast<int>(x), static_cast<int>(y));
            close_ = true;
        }
        return true;
    }

    struct Row {
        Rect rect;
        std::string module;         // passive module id, or
        std::string service, path;  // tray item identity
    };
    SNIBackend* sni_;
    DbusMenuBackend* menus_;
    StatusArea* area_;
    std::string hostId_;
    std::vector<Row> rows_;
    std::unique_ptr<MenuPopover> menu_;
    int selected_ = -1;
    bool close_ = false;
};

}  // namespace

std::vector<size_t> BackgroundAppsIndicator::stripItems(const SNIBackend* sni,
                                                        const StatusArea* area) {
    std::vector<size_t> result;
    if (sni == nullptr) { return result; }
    const auto& items = sni->items();
    for (size_t i = 0; i < items.size() && result.size() < kStripLimit; ++i) {
        const auto& item = items[i];
        if (!eligible(item, area)) { continue; }
        if (item.status == "NeedsAttention" || listed(item, area, &StatusArea::pinned)) {
            result.push_back(i);
        }
    }
    return result;
}

std::vector<size_t> BackgroundAppsIndicator::overflowItems(const SNIBackend* sni,
                                                           const StatusArea* area) {
    std::vector<size_t> result;
    if (sni == nullptr) { return result; }
    const auto strip = stripItems(sni, area);
    for (size_t i = 0; i < sni->items().size(); ++i) {
        if (eligible(sni->items()[i], area) && std::ranges::find(strip, i) == strip.end()) {
            result.push_back(i);
        }
    }
    return result;
}

BackgroundAppsIndicator::BackgroundAppsIndicator(const SystemBackends& b)
    : StatusIndicator("background-apps", Zone::Right, 600),
      backend_(b.sni),
      dbusMenu_(b.dbusMenu),
      area_(b.statusArea) {
    refreshVisibility();
}

bool BackgroundAppsIndicator::hasOverflow() const {
    if (!overflowItems(backend_, area_).empty()) { return true; }
    return area_ != nullptr && !area_->passiveEntries().empty();
}

void BackgroundAppsIndicator::onBackendUpdate() {
    refreshVisibility();
}

void BackgroundAppsIndicator::refreshVisibility() {
    visible = !stripItems(backend_, area_).empty() || hasOverflow();
}

double BackgroundAppsIndicator::measureWidth(Painter& p) {
    (void)p;
    if (!visible) { return 0; }
    const auto cells = stripItems(backend_, area_).size() + (hasOverflow() ? 1 : 0);
    if (cells == 0) { return 0; }
    return (2 * kPad) + (kCell * static_cast<double>(cells));
}

int BackgroundAppsIndicator::iconIndexAt(double x) const {
    const double offset = x - bounds.x - kPad;
    const auto strip = stripItems(backend_, area_);
    if (offset < 0 || offset >= kCell * static_cast<double>(strip.size())) { return -1; }
    return static_cast<int>(strip[static_cast<size_t>(offset / kCell)]);
}

bool BackgroundAppsIndicator::overChevron(double x) const {
    const double start =
        bounds.x + kPad + (kCell * static_cast<double>(stripItems(backend_, area_).size()));
    return hasOverflow() && x >= start && x < start + kCell;
}

void BackgroundAppsIndicator::onHoverMotion(double x, double y) {
    (void)y;
    hoverX_ = x;
}

std::string BackgroundAppsIndicator::tooltip() const {
    const int index = iconIndexAt(hoverX_);
    if (index >= 0) {
        const auto& item = backend_->items()[static_cast<size_t>(index)];
        return title(item) + (item.status == "NeedsAttention" ? " — needs attention" : "");
    }
    const size_t apps = overflowItems(backend_, area_).size();
    const size_t passive = area_ != nullptr ? area_->passiveEntries().size() : 0;
    if (apps + passive == 0) { return "Nothing running in the background"; }
    std::string out;
    if (passive > 0) { out = std::to_string(passive) + " more status"; }
    if (apps > 0) {
        if (!out.empty()) { out += ", "; }
        out += std::to_string(apps) + (apps == 1 ? " background app" : " background apps");
    }
    return out;
}

void BackgroundAppsIndicator::draw(Painter& p, int64_t now) {
    (void)now;
    if (!visible) { return; }
    double x = bounds.x + kPad;
    if (backend_ != nullptr) {
        for (const auto index : stripItems(backend_, area_)) {
            const Rect r{x, bounds.y + 2, kCell, bounds.h - 4};
            if (hoverX_ >= x && hoverX_ < x + kCell) {
                p.fillRoundedRect(r, 6, theme().colors.glassHover);
            }
            drawItemIcon(p, backend_->items()[index], r, theme());
            x += kCell;
        }
    }
    if (hasOverflow()) {
        const Rect r{x, bounds.y + 2, kCell, bounds.h - 4};
        if (overChevron(hoverX_)) { p.fillRoundedRect(r, 6, theme().colors.glassHover); }
        const TextStyle ts{theme().font.family, 15, PANGO_WEIGHT_NORMAL, theme().colors.text};
        const char* glyph = barBottom ? kChevronUp : kChevronDown;
        const auto size = p.measureText(glyph, ts);
        p.drawText(x + ((kCell - size.w) / 2), bounds.y + ((bounds.h - size.h) / 2), glyph, ts);
    }
}

bool BackgroundAppsIndicator::onClick(double x, double y) {
    pendingMenu_ = kNoMenu;
    if (overChevron(x)) {
        pendingMenu_ = kOverflow;
        return false;  // default activation opens createDetailedView()
    }
    const int i = iconIndexAt(x);
    if (i < 0) { return false; }
    if (backend_->items()[static_cast<size_t>(i)].itemIsMenu) {
        onSecondaryClick(x, y);
        return pendingMenu_ == kNoMenu;
    }
    backend_->activate(static_cast<size_t>(i), static_cast<int>(x), static_cast<int>(y));
    return true;
}

bool BackgroundAppsIndicator::onSecondaryClick(double x, double y) {
    pendingMenu_ = kNoMenu;
    if (overChevron(x)) {
        pendingMenu_ = kOverflow;
        return true;
    }
    const int i = iconIndexAt(x);
    if (i < 0) { return false; }
    if (dbusMenu_ != nullptr && !backend_->items()[static_cast<size_t>(i)].menuPath.empty()) {
        pendingMenu_ = i;
    } else {
        backend_->contextMenu(static_cast<size_t>(i), static_cast<int>(x), static_cast<int>(y));
    }
    return true;
}

bool BackgroundAppsIndicator::onMiddleClick(double x, double y) {
    const int i = iconIndexAt(x);
    if (i < 0) { return false; }
    backend_->secondaryActivate(static_cast<size_t>(i), static_cast<int>(x), static_cast<int>(y));
    return true;
}

bool BackgroundAppsIndicator::onScroll(double dx, double dy, double x, double y) {
    (void)y;
    const int i = iconIndexAt(x);
    if (i < 0) { return false; }
    backend_->scroll(static_cast<size_t>(i), static_cast<int>(dx), static_cast<int>(dy));
    return true;
}

std::unique_ptr<DetailedPopover> BackgroundAppsIndicator::createDetailedView() {
    const int i = std::exchange(pendingMenu_, kNoMenu);
    if (i == kOverflow) {
        return std::make_unique<OverflowPopover>(backend_, dbusMenu_, area_, id());
    }
    if (dbusMenu_ == nullptr || backend_ == nullptr || i < 0 ||
        static_cast<size_t>(i) >= backend_->items().size()) {
        return nullptr;
    }
    const auto& item = backend_->items()[static_cast<size_t>(i)];
    return std::make_unique<MenuPopover>(
        std::make_unique<DbusMenuSource>(dbusMenu_, item.service, item.menuPath), title(item));
}

using TrayIndicator = BackgroundAppsIndicator;
using SniIndicator = BackgroundAppsIndicator;
REGISTER_SESSION_INDICATOR("background-apps", Zone::Right, 600, BackgroundAppsIndicator)
REGISTER_SESSION_INDICATOR("sni", Zone::Right, 600, SniIndicator)
REGISTER_SESSION_INDICATOR("tray", Zone::Right, 600, TrayIndicator)

}  // namespace qypr
