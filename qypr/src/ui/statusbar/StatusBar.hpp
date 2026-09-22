// StatusBar.hpp - Container for zones, indicators, popovers, and layout.
#pragma once

#include <cairo/cairo.h>

#include <memory>
#include <vector>

#include "ui/Theme.hpp"
#include "ui/Widget.hpp"
#include "ui/statusbar/IndicatorHost.hpp"
#include "ui/statusbar/IndicatorRegistry.hpp"
#include "ui/statusbar/PopoverManager.hpp"
#include "ui/statusbar/QuickSettingsPanel.hpp"
#include "ui/statusbar/StatusBarInput.hpp"
#include "ui/statusbar/StatusBarLayout.hpp"
#include "ui/statusbar/TooltipController.hpp"

namespace qypr {

class EventLoop;
class Invalidator;

// StatusBar composes IndicatorHost (ownership/bucketing), StatusBarLayout
// (pure geometry), StatusBarInput (hit-testing and routing), PopoverManager
// (overlays + anchoring/auto-dismiss), and TooltipController (hover labels).
// It keeps only composition, invalidation, and the public host contract.
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
    IndicatorHost& indicators() { return indicators_; }

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
    // so the host shrinks back to its idle strip.
    int overlayHeight() const;

    // Draw a subtle rounded backdrop behind the strip. Off by default (the lock
    // screen stays chromeless over its dark video); the standalone desktop bar
    // turns it on so the glyphs stay legible over an arbitrary wallpaper.
    // `alpha` < 0 keeps the built-in default.
    void setBackdrop(bool enabled, double alpha = -1.0);

    // ThemeAware: the bar OWNS its theme copy (derived values like the
    // nested-surface alpha are folded in here), and every indicator, tile,
    // and popover under it reads this copy.
    void setTheme(const theme::State& state) override;
    const theme::State& theme() const override;

    // Panel geometry (size + which edge the bar is anchored to).
    void setGeometry(const BarGeometry& g);
    const BarGeometry& geometry() const { return geom_; }

    // Right-group chip bounds (for preview click targeting).
    const Rect& rightGroupBounds() const { return rightGroupBounds_; }

    // Pull current state out of every backend, exactly as a backend push would.
    void refreshFromBackends() { notifyBackendUpdate(); }

private:
    void toggleQuickSettings();
    void activateIndicator(StatusIndicator& ind);
    void openIndicatorDetail(const std::string& id);
    void notifyBackendUpdate();
    // (Re)arm auto-dismiss on the active popover via the manager.
    void resetAutoDismiss();

    // Effective visibility: a sensitive indicator is hidden unless session
    // content is enabled. QS-only indicators never appear in the bar.
    bool isShown(const StatusIndicator& ind) const {
        return ind.visible && !ind.qsOnly() && (!ind.sensitive() || sessionContentVisible_);
    }

    // Effective interactivity: while this bar shows *locked* session content
    // only indicators that opted in via lockInteractive() receive any input.
    // Default deny (QL-1/QL-2/QL-4).
    bool isInteractive(const StatusIndicator& ind) const {
        return sessionContentVisible_ || ind.lockInteractive();
    }

    // Hit-test one zone: first shown+interactive indicator whose bounds contain
    // (x,y), or nullptr. Shared by click/scroll/hover paths.
    StatusIndicator* hitTestZone(std::vector<std::unique_ptr<StatusIndicator>>& zone, double x,
                                 double y, bool requireInteractive = false);

    StatusBarInput::Context makeInputContext();

    EventLoop& loop_;
    Invalidator& host_;

    // Ownership + zone bucketing (one load path for ctor and reload).
    IndicatorHost indicators_;

    Rect rightGroupBounds_;
    Rect contentBounds_;

    QuickSettingsPanel qsPanel_;
    PopoverManager popovers_;
    TooltipController tooltips_;
    StatusBarInput input_;

    bool sessionContentVisible_ = false;
    bool sessionSurface_ = false;
    bool backdrop_ = false;
    double backdropAlpha_ = -1.0;

    BarGeometry geom_;

    int tickTimer_ = -1;

    cairo_surface_t* measureSurface_ = nullptr;
    cairo_t* measureCr_ = nullptr;

    WifiBackend* wifi_ = nullptr;

    theme::State theme_;
    void cascadeTheme();
    void applyBackdropAlpha();
};

}  // namespace qypr
