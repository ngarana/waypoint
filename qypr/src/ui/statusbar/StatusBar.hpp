// StatusBar.hpp - Container for zones, indicators, popovers, and layout.
#pragma once

#include <cairo/cairo.h>

#include <memory>
#include <vector>

#include "ui/Theme.hpp"
#include "ui/Widget.hpp"
#include "ui/statusbar/IndicatorRegistry.hpp"
#include "ui/statusbar/PopoverManager.hpp"
#include "ui/statusbar/QuickSettingsPanel.hpp"
#include "ui/statusbar/StatusIndicator.hpp"

namespace qypr {

class EventLoop;
class Invalidator;

// Panel geometry. Defaults reproduce the compiled-in lockscreen strip; qypr-bar
// overrides them from bar.conf (Phase 9). `bottom` mirrors the bar to the lower
// screen edge — including the direction popovers open.
struct BarGeometry {
    double height = theme::statusbar::height;
    double edgeMargin = theme::statusbar::topMargin;  // gap from the anchored edge
    double sideMargin = theme::statusbar::sideMargin;
    bool bottom = false;
};

class StatusBar : public Widget {
public:
    // Deliberately takes Invalidator, not RenderHost: the status bar is
    // lock-agnostic and must stay hostable outside the lockscreen.
    //
    // `sel` (optional) is the config-driven module set; nullptr means "every
    // registered indicator, compiled zones and priorities" — the lock screen's
    // behaviour, unchanged.
    StatusBar(EventLoop& loop, Invalidator& host, const SystemBackends& backends,
              const IndicatorRegistry::ModuleSelection* sel = nullptr);
    ~StatusBar() override;

    // Layout the bar based on focused screen width
    void layout(int screenW, int screenH);

    void draw(Painter& p, int64_t now) override;

    bool animating(int64_t now) const;

    // Event routing. Handlers return true when the event was consumed.
    bool handlePointerMotion(double x, double y, int64_t now);
    bool handlePointerButton(double x, double y, uint32_t button, bool pressed, int64_t now);
    void handlePointerLeave(int64_t now);
    bool handleScroll(double x, double y, double dx, double dy);
    bool handleKey(uint32_t keysym);
    // Committed keyboard text → the active popover (launcher search box).
    bool handleTextInput(const std::string& utf8);
    // True while a popover needing typed input is open (the host then grabs
    // keyboard focus for the bar surface).
    bool wantsKeyboard() const;

    // Focus navigation
    bool cycleFocus(bool reverse);
    void clearFocus();
    bool hasFocusedChild() const;

    QuickSettingsPanel& quickSettings() { return qsPanel_; }
    PopoverManager& popovers() { return popovers_; }

    // Hot-reload: rebuild indicators from a new module selection. Called by the
    // host when bar.conf changes. Clears the current indicators, recreates them
    // from the new selection, and rebuilds QS tiles.
    void reloadModules(const SystemBackends& backends,
                       const IndicatorRegistry::ModuleSelection* sel);

    // Opt into showing session-sensitive indicators (workspaces, active
    // window). The lock screen never enables this, so those widgets stay
    // hidden while locked; the standalone (unlocked) bar turns it on.
    void setSessionContentVisible(bool v);

    // True while Quick Settings or a popover is open (or animating closed).
    bool hasOpenOverlay() const;

    // Logical height the layer-shell host must give its surface (measured from
    // the anchored screen edge) to show the open popover — the strip plus the
    // tallest drawing popover, NOT the whole output. Zero when nothing is open,
    // so the host shrinks back to its idle strip. Sizing to the popover (instead
    // of full-screen) keeps the surface small, so opening a popover no longer
    // resizes a full-window surface — and any compositor blur on the "qypr-bar"
    // layer covers only the glass, not the entire screen.
    int overlayHeight() const;

    // Draw a subtle rounded backdrop behind the strip. Off by default (the lock
    // screen stays chromeless over its dark video); the standalone desktop bar
    // turns it on so the glyphs stay legible over an arbitrary wallpaper.
    // `alpha` < 0 keeps the built-in default.
    void setBackdrop(bool enabled, double alpha = -1.0);

    // Panel geometry (size + which edge the bar is anchored to).
    void setGeometry(const BarGeometry& g);
    const BarGeometry& geometry() const { return geom_; }

    // Right-group chip bounds (for preview click targeting).
    const Rect& rightGroupBounds() const { return rightGroupBounds_; }

    // Pull current state out of every backend, exactly as a backend push would.
    // Needed after StateCache seeds the backends from the previous session:
    // indicators cache their snapshot in onBackendUpdate(), and at that point no
    // daemon has replied yet, so nothing else would move the restored values
    // into the first frame.
    void refreshFromBackends() { notifyBackendUpdate(); }

private:
    void toggleQuickSettings();
    void activateIndicator(StatusIndicator& ind);
    // Open the named indicator's detail popover (right-click on its QS tile;
    // no-op when the module has none or is absent).
    void openIndicatorDetail(const std::string& id);
    void notifyBackendUpdate();
    // (Re)arm the active popover's auto-dismiss timer from its autoDismissMs().
    // Called on open and on every interaction; a resting pointer over the panel
    // keeps it alive. No-op for popovers that don't opt in (autoDismissMs() == 0).
    void resetAutoDismiss();
    // Point a popover away from the anchored screen edge (down for a top bar,
    // up for a bottom bar).
    void anchorPopoverY(DetailedPopover& pop) const;

    // Effective visibility: a sensitive indicator is hidden unless session
    // content is enabled. QS-only indicators never appear in the bar.
    // All layout/draw/hit-testing goes through this.
    bool isShown(const StatusIndicator& ind) const {
        return ind.visible && !ind.qsOnly() && (!ind.sensitive() || sessionContentVisible_);
    }

    // Effective interactivity: while this bar shows *locked* session content
    // (sessionContentVisible_ == false — the lock screen's mode) only indicators
    // that opted in via lockInteractive() receive any input. Default deny, so a
    // new indicator is inert on the lock screen until someone decides otherwise.
    // See StatusIndicator::lockInteractive() and QL-1/QL-2/QL-4 in
    // docs/LOCK_SECURITY_REVIEW.md.
    bool isInteractive(const StatusIndicator& ind) const {
        return sessionContentVisible_ || ind.lockInteractive();
    }

    EventLoop& loop_;
    Invalidator& host_;

    // Indicators split by zone
    std::vector<std::unique_ptr<StatusIndicator>> leftIndicators_;
    std::vector<std::unique_ptr<StatusIndicator>> centerIndicators_;
    std::vector<std::unique_ptr<StatusIndicator>> rightIndicators_;

    // Right-zone group chip: all right-side indicators sit inside a single
    // rounded filled surface tile. Clicking anywhere on the chip opens Quick
    // Settings (Ubuntu-style reveal).
    Rect rightGroupBounds_;

    // Centered content area: the span of visible indicators, centered within
    // bounds. Used for backdrop drawing and content clipping.
    Rect contentBounds_;

    // Popover Management
    QuickSettingsPanel qsPanel_;
    PopoverManager popovers_;

    // Gates session-sensitive indicators (default hidden — lock screen safe).
    bool sessionContentVisible_ = false;

    // True only when this bar is hosted by the unlocked desktop bar (qypr-bar).
    // Complementary to sessionContentVisible_: that one says "session content may
    // be shown", this one says "this is the session surface, so session-only
    // affordances may exist at all". The Quick Settings *command* tiles (power
    // menu, screenshot) shell out, so the lock process must never build them —
    // they used to sit in the lock binary, unreachable only because the power
    // indicator happened to be invisible (QL-7).
    bool sessionSurface_ = false;

    // Subtle strip backdrop (standalone desktop bar only; lock screen stays
    // chromeless). Off by default; alpha is config-tunable.
    bool backdrop_ = false;
    double backdropAlpha_ = -1.0;  // <0 → use the built-in default

    // Panel geometry; defaults reproduce the lockscreen strip.
    BarGeometry geom_;

    // 1s tick driving indicator poll() — the bar's own timer, never
    // LockScreen's (decoupling principle 1).
    int tickTimer_ = -1;

    // Auto-dismiss timer for transient popovers (the notification centre). Reset
    // on interaction; the callback keeps the panel up while the pointer rests
    // over it (tracked by lastPtr*). -1 when unarmed.
    int dismissTimer_ = -1;
    double lastPtrX_ = -1.0, lastPtrY_ = -1.0;

    // A pointer press landed inside the active (non-QS) popover: route the
    // following motion to it as a drag so the compact slider popup tracks the
    // thumb. QS keeps its own activeDragTile_ path. Cleared on release/leave.
    bool popoverDragging_ = false;

    // Offscreen 1x1 context so layout() can measure text without a frame.
    cairo_surface_t* measureSurface_ = nullptr;
    cairo_t* measureCr_ = nullptr;

    // Borrowed wifi backend (may be null on the lock screen): feeds the QS
    // Wi-Fi combo tile on every backend push (see notifyBackendUpdate).
    WifiBackend* wifi_ = nullptr;

    // ── Tooltips ─────────────────────────────────────────────────────────
    // After the pointer sits over one indicator for `kTooltipDelayMs` we paint
    // a small glass-card label below (or above, on a bottom-anchored bar) it.
    // Switching the hovered indicator or opening any popover cancels it.
    constexpr static int kTooltipDelayMs = 500;
    StatusIndicator* tooltipTarget_ = nullptr;
    int64_t tooltipHoverStartMs_ = 0;
    Animated tooltipAlpha_{0.0};

    void drawTooltip(Painter& p, int64_t now) const;
};

}  // namespace qypr
