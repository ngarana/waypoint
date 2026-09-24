#include "core/BarApp.hpp"

#include <cairo/cairo.h>
#include <unistd.h>

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <string>

#include "core/SolarCalc.hpp"
#include "core/Types.hpp"
#include "render/Painter.hpp"
#include "ui/PaletteSource.hpp"
#include "ui/Theme.hpp"
#include "ui/indicators/NotificationIndicator.hpp"  // previewNotificationCentre()
#include "wayland/Seat.hpp"                         // Mod bits

namespace qypr {

BarApp::BarApp() = default;

int BarApp::run() {
    themeRuntime_.setOnPaletteTransition([this](const std::string& mode) {
        if (mode == "dark") { syncNightLightWithPalette(); }
    });
    themeRuntime_.init(
        config_,
        [this](const theme::State& s) {
            statusBar_.setTheme(s);
            notifications_.setTheme(s);
        },
        [this] { display_.invalidateAll(); });

    return runtime_.run(
        /*sink=*/this,
        /*renderFn=*/[this](cairo_t* cr, int w, int h, int s) { draw(cr, w, h, s); },
        /*animatingFn=*/[this] { return statusBar_.animating(nowMs()); },
        /*setupProtocols=*/
        [this] {
            statusBar_.setSessionContentVisible(true);
            const double alpha = config_.getDouble("bar", "backdrop", -1.0);
            statusBar_.setBackdrop(alpha != 0.0, alpha);
            statusBar_.setGeometry(geom_);

            idleInhibitor_.init(display_.idleInhibitManager(), display_.anchorSurface(),
                                display_.display());
            nightLight_.init(display_.gammaControlManager(), display_.display());
            nightLight_.setOutputs(display_.outputs());
            nightLight_.setOnChange([this] { invalidate(); });
            syncNightLightWithPalette();
            workspace_.start(display_.display());
            toplevel_.start(display_.display());

            stateCache_.load();
            stateCache_.seed(backends_);
            statusBar_.refreshFromBackends();
        },
        /*onFirstFrame=*/
        [this] {
            // Workspace/toplevel registries are dispatched by the initial
            // Wayland roundtrip. Refresh once after that dispatch so session
            // indicators do not depend on the compositor delivering their
            // initial snapshot before the layer-surface configure callback.
            statusBar_.refreshFromBackends();
            configRuntime_.startWatching(
                config_.path(),
                [this](const Config& newConfig, const BarGeometry& newGeom,
                       const std::optional<IndicatorRegistry::ModuleSelection>& newMods) {
                    reloadConfig(newConfig, newGeom, newMods);
                });
            themeRuntime_.watchPalette(config_);
            if (themeRuntime_.palette().mode == "auto") { themeRuntime_.startMinuteTimer(config_); }
            loop_.post([this] {
                backendLifecycle_.start([this] {
                    if (themeRuntime_.refreshSolarTimes(geoClue_.fix())) {
                        themeRuntime_.applyTheme(config_);
                        invalidate();
                    }
                });
                if (themeRuntime_.refreshSolarTimes(geoClue_.fix())) {
                    themeRuntime_.applyTheme(config_);
                }
            });
        });
}

void BarApp::draw(cairo_t* cr, int w, int h, int /*scale*/) {
    Painter p(cr);
    statusBar_.layout(w, h);
    statusBar_.draw(p, nowMs());

    // Keep the surface height in step with the overlay state (deferred; never
    // resize mid-render).
    syncOverlay();
}
namespace {
void renderToPng(StatusBar& bar, const std::string& path, int w, int h) {
    cairo_surface_t* surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, w, h);
    cairo_t* cr = cairo_create(surface);
    Painter p(cr);
    bar.layout(w, h);
    bar.draw(p, nowMs());
    cairo_destroy(cr);
    cairo_surface_write_to_png(surface, path.c_str());
    cairo_surface_destroy(surface);
}

std::string stripExt(const std::string& path) {
    const auto dot = path.rfind('.');
    return dot != std::string::npos ? path.substr(0, dot) : path;
}
}  // namespace

int BarApp::preview(const std::string& path, int width, int height) {
    applyTheme();
    // Start backends for real indicator state.
    backendLifecycle_.startPreview();
    sni_.start();
    mpris_.refresh();

    // Configure as standalone bar (mirrors run()).
    statusBar_.setSessionContentVisible(true);
    // Honour the configured backdrop opacity (same resolution as run()) so the
    // preview frames — including the Quick Settings panel — match the live bar.
    const double alpha = config_.getDouble("bar", "backdrop", -1.0);
    statusBar_.setBackdrop(alpha != 0.0, alpha);
    statusBar_.setGeometry(geom_);

    // Pump the event loop briefly so volume (async connect) and the SNI tray
    // (session bus fetch) populate.
    loop_.addTimer(400, false, [this] { loop_.quit(); });
    loop_.run();

    // 1. Idle bar.
    statusBar_.layout(width, height);
    renderToPng(statusBar_, path, width, height);

    // 2. Quick Settings open: click the Control Center icon
    //    (the rightmost indicator, the "power" indicator with id "power").
    {
        // Find the power indicator's bounds and click its center.
        const auto& rgb = statusBar_.rightGroupBounds();
        const double iconCx = rgb.x + rgb.w - 8.0;  // rightmost item is the Control Center
        const double iconCy = rgb.y + (rgb.h / 2.0);
        statusBar_.handlePointerButton(iconCx, iconCy, 0x110, true, nowMs());
    }
    usleep(300 * 1000);
    // Re-layout so the opened panel's anchor is resolved and we can read its
    // position for the DND click.
    statusBar_.layout(width, height);
    renderToPng(statusBar_, stripExt(path) + "-qs.png", width, height);

    // 3. DND toggle on (find DND tile bounds and click its center).
    {
        Rect const dndBounds = statusBar_.quickSettings().boundsFor(QSTile::Role::Dnd);
        if (dndBounds.valid()) {
            statusBar_.handlePointerButton(dndBounds.cx(), dndBounds.cy(), 0x110, true, nowMs());
        } else {
            // Fallback
            const double barRight = statusBar_.bounds.x + statusBar_.bounds.w;
            const double panelX = barRight - 380.0;
            const double tileX = panelX + 14.0 + 56.0;
            const double statusBarBottom = statusBar_.bounds.y + statusBar_.bounds.h;
            const double tileY = statusBarBottom + 6.0 + 52.0 + 14.0 + 76.0 + 8.0 + 38.0;
            statusBar_.handlePointerButton(tileX, tileY, 0x110, true, nowMs());
        }
    }
    usleep(400 * 1000);
    renderToPng(statusBar_, stripExt(path) + "-dnd.png", width, height);

    // 4. Notification centre with demo data (anchored top-right like the live
    //    popover). Rendered directly — independent of the configured modules.
    {
        cairo_surface_t* s = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, width, height);
        cairo_t* cr = cairo_create(s);
        Painter p(cr);
        previewNotificationCentre(p, width - 20.0, statusBar_.bounds.y + statusBar_.bounds.h + 6.0,
                                  geom_.bottom, true, config_.getDouble("bar", "backdrop", -1.0));
        cairo_destroy(cr);
        cairo_surface_write_to_png(s, (stripExt(path) + "-notif.png").c_str());
        cairo_surface_destroy(s);
    }

    std::fprintf(stderr, "qypr-bar: wrote preview frames near %s\n", path.c_str());
    return 0;
}

void BarApp::invalidate() {
    display_.invalidateAll();
    // Every backend push funnels through here on its way to a repaint, which
    // makes this the one place that sees all of them without competing for the
    // backends' single onChange_ slot (StatusBar owns that). Hover and animation
    // repaints arrive here too, but the cache debounces and skips writes whose
    // content is unchanged, so they cost nothing.
    stateCache_.noteChanged();
}

void BarApp::reloadConfig() {
    configRuntime_.reload(
        [this](const Config& newConfig, const BarGeometry& newGeom,
               const std::optional<IndicatorRegistry::ModuleSelection>& newMods) {
            reloadConfig(newConfig, newGeom, newMods);
        },
        config_.path());
}

void BarApp::reloadConfig(const Config& newConfig, const BarGeometry& newGeom,
                          const std::optional<IndicatorRegistry::ModuleSelection>& newMods) {
    std::fprintf(stderr, "qypr-bar: config changed, reloading\n");
    config_ = newConfig;

    themeRuntime_.init(
        config_,
        [this](const theme::State& s) {
            statusBar_.setTheme(s);
            notifications_.setTheme(s);
        },
        [this] { display_.invalidateAll(); });
    themeRuntime_.refreshSolarTimes(geoClue_.fix());
    themeRuntime_.applyTheme(config_);
    themeRuntime_.watchPalette(config_);
    syncNightLightWithPalette();

    geom_ = newGeom;
    statusBar_.setGeometry(geom_);
    const int reserved = ConfigRuntime::reservedFor(geom_);
    display_.setOverlayHeight(reserved);
    surfaceController_.setIdleHeight(reserved);
    surfaceController_.syncOverlay(statusBar_.overlayHeight());

    const double alpha = config_.getDouble("bar", "backdrop", -1.0);
    statusBar_.setBackdrop(alpha != 0.0, alpha);

    modules_ = newMods;
    statusBar_.reloadModules(backends_, modules_ ? &*modules_ : nullptr);

    invalidate();
}

void BarApp::applyTheme() {
    themeRuntime_.applyTheme(config_);
}

void BarApp::syncNightLightWithPalette() {
    // Only the automatic day/night mode owns this side effect. Explicitly
    // configured dark mode is a theme choice, not consent to change gamma.
    if (themeRuntime_.palette().mode != "auto" || themeRuntime_.palette().resolved != "dark") {
        return;
    }
    if (!nightLight_.enabled()) { nightLight_.setEnabled(true); }
}

void BarApp::syncOverlay() {
    surfaceController_.syncOverlay(statusBar_.overlayHeight());
}

void BarApp::syncKeyboard() {
    surfaceController_.syncKeyboard(statusBar_.wantsKeyboard());
}

// -----------------------------------------------------------------------------
// InputSink — route straight to StatusBar (no LockScreen peer to arbitrate).
// -----------------------------------------------------------------------------
void BarApp::onTextInput(const std::string& utf8) {
    // Committed text only reaches here while we hold the keyboard grab (launcher
    // open) — route it to the active search box and repaint.
    statusBar_.handleTextInput(utf8);
    invalidate();
}

void BarApp::onSpecialKey(uint32_t keysym, uint32_t modifiers) {
    if (keysym == XKB_KEY_Tab || keysym == XKB_KEY_ISO_Left_Tab) {
        const bool reverse = keysym == XKB_KEY_ISO_Left_Tab || ((modifiers & MOD_SHIFT) != 0U);
        if (!statusBar_.cycleFocus(reverse)) { statusBar_.clearFocus(); }
    } else {
        statusBar_.handleKey(keysym);
    }
    invalidate();
    syncOverlay();
    syncKeyboard();  // Enter/Escape may have closed the launcher — drop the grab.
}

void BarApp::onLayoutChanged(const std::string& name, uint32_t index, uint32_t count) {
    // The Seat observed a layout change (or the initial layout). Mirror it into
    // the backend; update() fires onChange → the indicator refreshes + repaints.
    kbLayout_.update(name, index, count);
}

void BarApp::onPointerMotion(int /*surfaceW*/, int /*surfaceH*/, double x, double y) {
    statusBar_.handlePointerMotion(x, y, nowMs());
    invalidate();
}

void BarApp::onPointerButton(int /*surfaceW*/, int /*surfaceH*/, double x, double y,
                             uint32_t button, bool pressed) {
    statusBar_.handlePointerButton(x, y, button, pressed, nowMs());
    invalidate();
    syncOverlay();
    syncKeyboard();  // opening/closing the launcher flips the keyboard grab.
}

void BarApp::onPointerScroll(int /*surfaceW*/, int /*surfaceH*/, double x, double y, double dx,
                             double dy) {
    statusBar_.handleScroll(x, y, dx, dy);
    invalidate();
    syncOverlay();
}

void BarApp::onPointerLeave() {
    statusBar_.handlePointerLeave(nowMs());
    invalidate();
}

}  // namespace qypr
