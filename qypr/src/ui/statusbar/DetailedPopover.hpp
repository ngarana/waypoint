// DetailedPopover.hpp - Base popover widget (glass card, popover contents).
#pragma once

#include "core/Types.hpp"
#include "render/Painter.hpp"
#include "ui/Theme.hpp"
#include <cstdint>
#include <string>

namespace qypr {

class Painter;

class DetailedPopover : public theme::ThemeAware {
public:
    virtual ~DetailedPopover() = default;

    virtual void draw(Painter& p, int64_t now) = 0;
    virtual double contentHeight() const = 0;
    virtual double contentWidth() const { return 280.0; }

    // Input
    virtual bool handleClick(double x, double y) { return false; }
    // Right-click (secondary) inside the popover — the context action: forget a
    // saved network, dismiss a notification card, open a tile's detail view.
    // Return true when consumed. Default: unused.
    virtual bool handleSecondaryClick(double x, double y) { return false; }
    virtual bool handleDrag(double x, double y) { return false; }
    // Pointer moved with no button held. Popovers with internal hover states
    // (e.g. the notification centre's cards) override this to track the cursor.
    // Return true when the pointer is inside the popover so the host repaints and
    // suppresses the indicator hover-tooltip behind it. Default: pointer-only.
    virtual bool handleMotion(double x, double y) { return false; }
    virtual bool handleScroll(double dx, double dy) { return false; }
    virtual bool handleKey(uint32_t keysym) { return false; }
    // Committed text (UTF-8) from the keyboard. Only reaches a popover whose
    // wantsKeyboard() is true and while the host holds keyboard focus.
    virtual bool handleText(const std::string& utf8) { return false; }

    // A popover that needs typed input (the launcher search box). The host
    // (BarApp) grabs keyboard focus for the bar surface while such a popover is
    // open, and releases it on close. Default: pointer-only.
    virtual bool wantsKeyboard() const { return false; }

    // Auto-dismiss: when > 0, the host closes this popover after that many
    // milliseconds without *interaction* (click/scroll/drag/keyboard over it — a
    // parked pointer does not count). Every bar popover is transient by default,
    // so it defaults to a timeout; the persistent ones (Quick Settings, the
    // keyboard-driven launcher) override this to 0. The notification centre and
    // slider popup set their own values.
    virtual int autoDismissMs() const { return 6000; }

    // Polled by the host right after handleClick: return true (once) to ask the
    // manager to close this popover — e.g. a menu that just fired an item. The
    // default popover never self-closes.
    virtual bool consumeCloseRequest() { return false; }

    // Check if pointer is inside popover bounds
    bool contains(double px, double py) const { return getBounds().contains(px, py); }

    // Get popover rectangle bounds (calculated dynamically based on anchor)
    Rect getBounds() const {
        double w = contentWidth();
        double h = contentHeight();
        // Shift popover left so the anchor point aligns with the top-right of
        // popover. growUp extends upward from the anchor instead of down, so a
        // bottom-anchored bar opens its panels toward the screen centre.
        return {anchorX - w, growUp ? anchorY - h : anchorY, w, h};
    }

    // Anchor point (set by PopoverManager or StatusBar)
    double anchorX = 0;
    double anchorY = 0;
    // Open upward from the anchor (set by StatusBar for a bottom-edge bar).
    bool growUp = false;

    // Animation progress (0.0 to 1.0)
    Animated openProgress_{0.0};

    bool isOpen() const { return openProgress_.target() > 0.5; }
    void open() { openProgress_.animateTo(1.0, theme().anim.fast, ease::inOutQuad); }
    void close() { openProgress_.animateTo(0.0, theme().anim.fast, ease::inOutQuad); }

    // Standalone-bar overlays share the bar's backdrop. The lock-screen host
    // never enables this, so its existing opaque/glass card backgrounds remain
    // unchanged.
    void setBackdrop(bool enabled, double alpha) {
        backdropEnabled_ = enabled;
        backdropAlpha_ = alpha;
        backdropConfigured_ = true;
    }

protected:
    // Draw the common bar/overlay slab. Returns false when the host did not
    // configure a shared backdrop, allowing each popover to retain its legacy
    // lock-screen fallback. A configured alpha of 0 is intentionally a fully
    // transparent panel, not a request to fall back to the theme default.
    bool drawSharedBackdrop(Painter& p, const Rect& r, double radius) const {
        if (!backdropConfigured_) return false;
        if (!backdropEnabled_) return true;

        const double alpha =
            clamp01(backdropAlpha_ >= 0.0 ? backdropAlpha_ : theme().statusbar.barTintAlpha);
        p.fillRoundedRect(r, radius, theme().statusbar.barTint.withAlpha(alpha));
        if (theme().statusbar.barBorderEnabled && theme().statusbar.barBorderAlpha > 0.0) {
            p.strokeRoundedRect(
                r, radius,
                theme().statusbar.barBorder.withAlpha(clamp01(theme().statusbar.barBorderAlpha)),
                1.0);
        }
        return true;
    }

private:
    bool backdropEnabled_ = false;
    double backdropAlpha_ = -1.0;
    bool backdropConfigured_ = false;
};

}  // namespace qypr
