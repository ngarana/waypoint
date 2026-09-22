// SNITrayHost.cpp - Status bar system-tray applet implementation.
#include "ui/indicators/SNITrayHost.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>

#include "render/Painter.hpp"
#include "system/DbusMenuBackend.hpp"
#include "system/SNIBackend.hpp"
#include "render/IconResolver.hpp"
#include "ui/Theme.hpp"
#include "ui/statusbar/IndicatorRegistry.hpp"
#include "ui/statusbar/MenuPopover.hpp"

namespace qypr {

namespace {

constexpr double kIconPx = 18.0;                // per-item icon size
constexpr double kGap = 6.0;                    // between adjacent icons
constexpr double kSidePad = 8.0;                // hover-zone padding each side
constexpr double kChevronW = 18.0;              // overflow button width
constexpr const char* kHiddenGlyph = "\uf553";  // angle-left endpoints

// Is every inked pixel of this surface (near) the same grey? Such an icon is a
// freedesktop *-symbolic / monochrome glyph (e.g. blueman-tray resolves to a
// dark #363636 bluetooth). Drawn as-is it vanishes on a dark strip, so a
// monochrome tray icon is recoloured to the bar foreground (like the status
// glyphs). A full-colour app icon has chromatic pixels and is left untouched.
bool surfaceIsMonochrome(cairo_surface_t* s) {
    if (s == nullptr || cairo_image_surface_get_format(s) != CAIRO_FORMAT_ARGB32) { return false; }
    const int w = cairo_image_surface_get_width(s);
    const int h = cairo_image_surface_get_height(s);
    if (w <= 0 || h <= 0) { return false; }
    cairo_surface_flush(s);
    const unsigned char* data = cairo_image_surface_get_data(s);
    const int stride = cairo_image_surface_get_stride(s);
    bool anyInk = false;
    for (int y = 0; y < h; ++y) {
        const auto* row =
            reinterpret_cast<const uint32_t*>(data + (static_cast<ptrdiff_t>(y) * stride));
        for (int x = 0; x < w; ++x) {
            const uint32_t px = row[x];
            const unsigned a = (px >> 24U) & 0xFFU;
            if (a < 24U) { continue; }  // ignore near-transparent antialiasing (<~10%)
            // ARGB32 is premultiplied; scale channels back to straight alpha
            // before comparing hue so faint edges don't read as "grey".
            // (All unsigned: no signed-bitwise hazards by construction.)
            const unsigned r = ((px >> 16U) & 0xFFU) * 255U / a;
            const unsigned g = ((px >> 8U) & 0xFFU) * 255U / a;
            const unsigned b = ((px & 0xFFU) * 255U) / a;
            const unsigned mx = std::max({r, g, b});
            const unsigned mn = std::min({r, g, b});
            if (mx - mn > 24U) { return false; }  // chromatic pixel → full-colour icon
            anyInk = true;
        }
    }
    return anyInk;
}

// ── Overflow popover ──────────────────────────────────────────────────────────
// Lists the SNI items whose status is "Passive" (hidden from the strip per
// spec). Each row carries its icon + title; clicking a row left-activates the
// item and closes the popover. Mirrors the MenuPopover glass-card look.
class OverflowPopover : public DetailedPopover {
public:
    OverflowPopover(SNIBackend* backend, std::vector<const SNIItem*> hidden, int originX,
                    int originY)
        : backend_(backend),
          hidden_(std::move(hidden)) {
        anchorX = originX;
        anchorY = originY;
        if (hidden_.empty()) { empty_ = true; }
    }

    [[nodiscard]] double contentWidth() const override { return 260.0; }
    [[nodiscard]] double contentHeight() const override {
        if (empty_) { return 56.0; }
        return 16.0 + (static_cast<double>(hidden_.size()) * 30.0) + 16.0;
    }

    void draw(Painter& p, int64_t now) override {
        Rect b = getBounds();
        b.y -= (1.0 - openProgress_.value(now)) * 6.0;
        if (!drawSharedBackdrop(p, b, theme().statusbar.popoverRadius)) {
            p.fillRoundedRectSource(b, theme().statusbar.popoverRadius, theme().panelSurface());
        }
        constexpr double kPopoverPad = 14.0;
        constexpr double kPopoverIconPx = 18.0;
        constexpr double kRowHeight = 30.0;
        const TextStyle hdr{.family = theme().font.family,
                            .size = 11.0,
                            .weight = PANGO_WEIGHT_BOLD,
                            .color = theme().colors.textSubtle};
        p.drawText(b.x + kPopoverPad, b.y + kPopoverPad, "HIDDEN ITEMS", hdr);
        rows_.clear();
        if (empty_) {
            const TextStyle e{.family = theme().font.family,
                              .size = 12.0,
                              .weight = PANGO_WEIGHT_NORMAL,
                              .color = theme().colors.textSubtle};
            p.drawText(b.x + kPopoverPad, b.y + kPopoverPad + 24.0, "No hidden tray items", e);
            return;
        }
        double y = b.y + kPopoverPad + 22.0;
        for (const SNIItem* it : hidden_) {
            const Rect row{
                .x = b.x + kPopoverPad, .y = y, .w = b.w - (kPopoverPad * 2), .h = kRowHeight};
            rows_.push_back(Row{.rect = row, .item = it});
            cairo_surface_t* s = nullptr;
            bool fromName = false;
            if (!it->iconName.empty()) {
                s = IconResolver::instance().get(it->iconName);
                fromName = s != nullptr;
            }
            if (s == nullptr) { s = it->pixmap; }
            const double iy = row.y + ((row.h - kPopoverIconPx) / 2.0);
            if (s != nullptr) {
                const Rect dest{
                    .x = row.x + 4.0, .y = iy, .w = kPopoverIconPx, .h = kPopoverIconPx};
                if (fromName && surfaceIsMonochrome(s)) {
                    p.drawSurfaceTinted(s, dest, theme().colors.text);
                } else {
                    p.drawSurface(s, dest);
                }
            }
            const TextStyle ts{.family = theme().font.family,
                               .size = 13.0,
                               .weight = PANGO_WEIGHT_NORMAL,
                               .color = theme().colors.text};
            p.drawText(row.x + 28.0, row.y + ((row.h - 14.0) / 2.0),
                       it->title.empty() ? it->service : it->title, ts, HAlign::Left, row.w - 32.0);
            y += kRowHeight;
        }
    }

    bool handleClick(double x, double y) override {
        for (const auto& r : rows_) {
            if (!r.rect.contains(x, y) || r.item == nullptr) { continue; }
            const auto& items = backend_->items();
            for (size_t i = 0; i < items.size(); ++i) {
                if (&items.at(i) == r.item) {
                    backend_->activate(i, static_cast<int>(x), static_cast<int>(y));
                    break;
                }
            }
            closeRequested_ = true;
            return true;
        }
        return false;
    }

    bool consumeCloseRequest() override {
        const bool c = closeRequested_;
        closeRequested_ = false;
        return c;
    }

private:
    struct Row {
        Rect rect;
        const SNIItem* item = nullptr;
    };
    SNIBackend* backend_ = nullptr;
    std::vector<const SNIItem*> hidden_;
    std::vector<Row> rows_;
    bool empty_ = false;
    bool closeRequested_ = false;
};

}  // namespace

SNITrayHost::SNITrayHost(const SystemBackends& backends)
    : StatusIndicator("sni", Zone::Right, 600),
      backend_(backends.sni),
      dbusMenu_(backends.dbusMenu) {
    visible = false;  // hidden until the host mirrors at least one item
}

std::string SNITrayHost::tooltip() const {
    if (backend_ == nullptr) { return "System tray"; }
    const auto& items = backend_->items();
    if (items.empty()) { return "System tray"; }
    if (items.size() == 1 && !items.at(0).title.empty()) { return items.at(0).title; }
    return std::to_string(items.size()) + " tray items";
}

double SNITrayHost::measureWidth(Painter& painter) {
    (void)painter;  // widths derive from backend data, not the context
    if (backend_ == nullptr) { return 0; }
    size_t n = 0;
    for (const auto& it : backend_->items()) {
        if (!onStrip(it)) { continue; }
        if (!it.iconName.empty() || it.pixmap != nullptr) { ++n; }
    }
    if (n == 0 && !overflowBoxShown_) { return 0; }
    double w = 0;
    if (n > 0) {
        const auto count = static_cast<double>(n);
        w = (count * kIconPx) + ((count - 1.0) * kGap);
    }
    if (overflowBoxShown_) {
        if (n > 0) { w += kGap; }
        w += kChevronW;
    }
    return w + (2 * kSidePad);
}

void SNITrayHost::draw(Painter& p, int64_t now) {
    if (!visible || backend_ == nullptr) { return; }
    const auto& items = backend_->items();
    if (items.empty()) { return; }

    // Hover pill + focus ring (mirrors the base StatusIndicator chrome).
    const double alpha = hoverAlpha_.value(now);
    if (alpha > 0.01) {
        const Color bg = theme().colors.glassHover.withAlpha(alpha * theme().colors.glassHover.a);
        Rect hoverRect = bounds;
        hoverRect.y += 2.0;
        hoverRect.h -= 4.0;
        p.fillRoundedRect(hoverRect, 8.0, bg);
    }
    if (focused) {
        Rect focusRect = bounds;
        focusRect.y += 1.0;
        focusRect.h -= 2.0;
        p.strokeRoundedRect(focusRect, 8.0, theme().colors.primary, 1.5);
    }

    double x = bounds.x + kSidePad;
    const double y = bounds.y + ((bounds.h - kIconPx) / 2.0);
    overflowBoxStart_ = -1.0;
    for (const auto& it : items) {
        if (!onStrip(it)) { continue; }
        cairo_surface_t* s = nullptr;
        bool fromName = false;
        if (!it.iconName.empty()) {
            s = IconResolver::instance().get(it.iconName);
            fromName = s != nullptr;
        }
        if (s == nullptr) { s = it.pixmap; }
        if (s == nullptr) { continue; }
        // Recolour a monochrome/symbolic themed icon to the bar foreground so it
        // stays legible on the strip; leave full-colour app icons and app-supplied
        // pixmaps untouched.
        const Rect dest{.x = x, .y = y, .w = kIconPx, .h = kIconPx};
        if (fromName && iconIsMonochrome(it.iconName, s)) {
            p.drawSurfaceTinted(s, dest, iconColor());
        } else {
            p.drawSurface(s, dest);
        }
        x += kIconPx + kGap;
    }
    // Overflow chevron at the end (only when passive items exist). The popover
    // itself is the hover affordance; the chevron is left to its glyph alone so
    // we don't need a separate hover state.
    if (overflowBoxShown_) {
        if (x > bounds.x + kSidePad + kIconPx) { x -= kGap; }  // no double gap before chevron
        overflowBoxStart_ = x;
        const TextStyle gs{.family = theme().font.iconFamily,
                           .size = 16.0,
                           .weight = PANGO_WEIGHT_NORMAL,
                           .color = theme().colors.textSubtle};
        const Size sz = p.measureText(kHiddenGlyph, gs);
        p.drawText(x + ((kChevronW - sz.w) / 2.0), bounds.y + ((bounds.h - sz.h) / 2.0),
                   kHiddenGlyph, gs);
    }
}

void SNITrayHost::onBackendUpdate() {
    if (backend_ == nullptr) {
        visible = false;
        return;
    }
    // Visible whenever any on-strip item has a renderable icon. Passive items
    // by themselves keep the indicator hidden (the user already dismissed them
    // by setting them Passive; the overflow chevron alone should not clutter
    // the strip), but they re-emerge the moment any on-strip item appears.
    bool anyStrip = false;
    bool anyPassive = false;
    for (const auto& it : backend_->items()) {
        const bool renderable = !it.iconName.empty() || it.pixmap != nullptr;
        if (it.status == "Passive") {
            if (renderable) { anyPassive = true; }
        } else if (renderable) {
            anyStrip = true;
        }
    }
    overflowBoxShown_ = anyPassive && anyStrip;
    visible = anyStrip;
}

// Memoised per icon name: whether the resolved surface is monochrome/symbolic
// (and so needs recolouring to the foreground). An icon name's monochromy is
// stable, and there are only a handful of tray names, so the cache never needs
// invalidating.
bool SNITrayHost::iconIsMonochrome(const std::string& name, cairo_surface_t* s) {
    auto it = monoCache_.find(name);
    if (it != monoCache_.end()) { return it->second; }
    const bool m = surfaceIsMonochrome(s);
    monoCache_.emplace(name, m);
    return m;
}

int SNITrayHost::iconIndexAt(double x) const {
    if (backend_ == nullptr || !visible) { return -1; }
    const auto& items = backend_->items();
    if (items.empty()) { return -1; }
    const double localX = x - (bounds.x + kSidePad);
    if (localX < 0) { return -1; }
    // Walk on-strip items only; Passive items are in the overflow popover.
    double cx = 0;
    for (size_t i = 0; i < items.size(); ++i) {
        if (!onStrip(items.at(i))) { continue; }
        if (localX >= cx && localX < cx + kIconPx) { return static_cast<int>(i); }
        cx += kIconPx + kGap;
    }
    return -1;
}

bool SNITrayHost::overOverflow(double x) const {
    if (!overflowBoxShown_ || overflowBoxStart_ < 0) { return false; }
    return x >= overflowBoxStart_ && x < overflowBoxStart_ + kChevronW;
}

bool SNITrayHost::onScroll(double dx, double dy, double x, double y) {
    (void)y;
    if (backend_ == nullptr || !visible) { return false; }
    // Forward to the on-strip item under the cursor (status "Passive" items
    // cannot be scrolled on the strip — they are in the overflow popover).
    const int idx = iconIndexAt(x);
    if (idx < 0) { return false; }
    backend_->scroll(static_cast<size_t>(idx), static_cast<int>(dx), static_cast<int>(dy));
    return true;
}

bool SNITrayHost::onClick(double x, double y) {
    (void)y;
    if (overOverflow(x)) {
        // Open the overflow list. Returning false lets StatusBar's default
        // activateIndicator run, which sees hasDetailedView()=true (pendingMenu_
        // is set to -2 — overflow sentinel) and calls createDetailedView().
        pendingMenu_ = -2;
        return false;
    }
    const int idx = iconIndexAt(x);
    if (idx < 0) { return false; }
    backend_->activate(static_cast<size_t>(idx), static_cast<int>(x), static_cast<int>(y));
    return true;
}

bool SNITrayHost::onSecondaryClick(double x, double y) {
    (void)y;
    // Right-click opens the item's dbusmenu — but only on the unlocked bar
    // (dbusMenu_ is null on the lock screen, by design).
    if (dbusMenu_ == nullptr) { return false; }
    const int idx = iconIndexAt(x);
    if (idx < 0) { return false; }
    if (backend_->items().at(static_cast<size_t>(idx)).menuPath.empty()) { return false; }
    pendingMenu_ = idx;  // consumed by createDetailedView()
    return true;
}

bool SNITrayHost::onMiddleClick(double x, double y) {
    (void)y;
    const int idx = iconIndexAt(x);
    if (idx < 0) { return false; }
    backend_->secondaryActivate(static_cast<size_t>(idx), static_cast<int>(x), static_cast<int>(y));
    return true;
}

std::unique_ptr<DetailedPopover> SNITrayHost::createDetailedView() {
    const int idx = pendingMenu_;
    pendingMenu_ = -1;
    if (idx == -2) {
        // Overflow request: gather passive items and open the list popover.
        std::vector<const SNIItem*> passive;
        if (backend_ != nullptr) {
            for (const auto& it : backend_->items()) {
                if (it.status == "Passive" && (!it.iconName.empty() || it.pixmap != nullptr)) {
                    passive.push_back(&it);
                }
            }
        }
        // Anchor the popover below (or above on a bottom bar) the chevron. The
        // StatusBar places popovers using anchorX/anchorY; for the Right-zone
        // the anchor is the bar's right edge, so we use that and the popover
        // opens just below the strip.
        auto pop = std::make_unique<OverflowPopover>(backend_, std::move(passive),
                                                     static_cast<int>(bounds.x + bounds.w),
                                                     static_cast<int>(bounds.y + bounds.h + 4));
        return pop;
    }
    if (dbusMenu_ == nullptr || idx < 0 || static_cast<size_t>(idx) >= backend_->items().size()) {
        return nullptr;
    }
    const SNIItem& it = backend_->items().at(static_cast<size_t>(idx));
    if (it.menuPath.empty()) { return nullptr; }
    auto root = dbusMenu_->fetch(it.service, it.menuPath, 0);
    return std::make_unique<MenuPopover>(dbusMenu_, it.service, it.menuPath, std::move(root),
                                         it.title);
}

REGISTER_INDICATOR("sni", Zone::Right, 600, SNITrayHost)

}  // namespace qypr
