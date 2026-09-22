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
#include "ui/Theme.hpp"
#include "ui/indicators/NotificationIndicator.hpp"  // previewNotificationCentre()
#include "wayland/Seat.hpp"                         // Mod bits

namespace qypr {

// -----------------------------------------------------------------------------
// Config (Phase 9). Every knob falls back to the compiled default, so a missing
// or partial bar.conf still yields exactly the bar we shipped before.
// -----------------------------------------------------------------------------
Config BarApp::loadConfig() {
    Config c;
    if (c.load()) { std::fprintf(stderr, "qypr-bar: config %s\n", c.path().c_str()); }
    return c;
}

BarGeometry BarApp::readGeometry(const Config& c) {
    BarGeometry g;  // defaults = theme constants (the lockscreen strip)
    g.height = c.getDouble("bar", "height", g.height);
    g.edgeMargin = c.getDouble("bar", "margin", g.edgeMargin);
    g.sideMargin = c.getDouble("bar", "margin-side", g.sideMargin);
    const std::string pos = c.getString("bar", "position", "top");
    g.bottom = (pos == "bottom");
    if (pos != "top" && pos != "bottom") {
        std::fprintf(stderr, "qypr-bar: unknown position '%s' (want top|bottom); using top\n",
                     pos.c_str());
    }
    return g;
}

std::optional<IndicatorRegistry::ModuleSelection> BarApp::readModules(const Config& c) {
    const bool any = c.has("bar", "modules-left") || c.has("bar", "modules-center") ||
                     c.has("bar", "modules-right");
    if (!any) {
        return std::nullopt;  // no module keys: keep the compiled default set
    }

    IndicatorRegistry::ModuleSelection sel;
    sel.left = c.getList("bar", "modules-left");
    sel.center = c.getList("bar", "modules-center");
    sel.right = c.getList("bar", "modules-right");

    // A typo silently drops a module, which is baffling in a bar you cannot
    // introspect — so name the unknown ids and list what was available.
    const auto known = IndicatorRegistry::instance().registeredIds();
    for (const auto* zone : {&sel.left, &sel.center, &sel.right}) {
        for (const auto& id : *zone) {
            if (std::ranges::find(known, id) == known.end()) {
                std::string all;
                for (const auto& k : known) { all += (all.empty() ? "" : ", ") + k; }
                std::fprintf(stderr, "qypr-bar: unknown module '%s' (have: %s)\n", id.c_str(),
                             all.c_str());
            }
        }
    }
    return sel;
}

BarApp::BarApp() = default;

int BarApp::run() {
    // The bar owns its day/night state: apply the palette before anything
    // draws (previously done inside loadConfig).
    applyTheme();
    if (!display_.connect()) {
        std::fprintf(stderr, "qypr-bar: no Wayland display or no wlr-layer-shell support\n");
        return 1;
    }

    display_.setInputSink(this);
    display_.setRenderFn([this](cairo_t* cr, int w, int h, int s) { draw(cr, w, h, s); });
    display_.setAnimatingFn([this] { return statusBar_.animating(nowMs()); });

    // This is the unlocked bar: reveal the session-sensitive WM widgets and
    // start their backends on the same wl_display (they bind their own registry).
    statusBar_.setSessionContentVisible(true);
    // Give the chromeless strip a subtle backdrop so it stays legible over an
    // arbitrary desktop wallpaper (the lock screen never does this). `backdrop`
    // is 0..1; 0 restores the pure chromeless look.
    const double alpha = config_.getDouble("bar", "backdrop", -1.0);
    statusBar_.setBackdrop(alpha != 0.0, alpha);
    statusBar_.setGeometry(geom_);

    // Wayland-local wiring only — these talk to the compositor we just connected
    // to (present and fast), never to an external daemon, so they are safe on the
    // pre-first-paint path.
    idleInhibitor_.init(display_.idleInhibitManager(), display_.anchorSurface(),
                        display_.display());
    nightLight_.init(display_.gammaControlManager(), display_.display());
    nightLight_.setOutputs(display_.outputs());
    nightLight_.setOnChange([this] { invalidate(); });
    workspace_.start(display_.display());
    toplevel_.start(display_.display());

    // Seed every hardware indicator from the last session's values so the very
    // first frame carries real numbers (battery %, SSID, volume) instead of the
    // neutral "unknown" glyphs. Purely a local file read — no daemon involved,
    // which is the whole point: at boot the daemons that own this state are
    // typically not running yet (UPower in particular is D-Bus-activated and
    // starts *after* the bar). Live pushes overwrite these within moments.
    stateCache_.load();
    stateCache_.seed(backends_);
    statusBar_.refreshFromBackends();  // pull the seeded values into the indicators

    // Paint the strip *now*, before touching a single daemon. The layer
    // surface's initial configure is only dispatched when we pump the
    // connection, so every call made before this point is time the desktop
    // spends with no bar on screen. One roundtrip is enough: the configure
    // arrives, BarWindow::render() draws and commits the first frame.
    display_.roundtrip();

    // Live config reload: watch bar.conf for edits and re-apply all sections
    // without a restart (theme, geometry, modules, per-indicator config).
    configWatcher_.watch(config_.path(), [this] { reloadConfig(); });
    // Also watch the matugen palette ([theme] colors-file): regenerating it
    // (e.g. after a wallpaper change) re-themes the bar without touching
    // bar.conf.
    watchPalette();

    // Auto palette: refresh the solar window (midnight rollover, late
    // GeoClue fix) and re-check the clock once a minute; re-theme on a
    // light/dark flip (tick returns true only when the resolved mode changed).
    if (palette_.mode == "auto") {
        loop_.addTimer(60'000, /*repeat=*/true, [this] {
            refreshSolarTimes();
            if (palette_.tick(theme::localHourNow())) {
                applyTheme();
                invalidate();
            }
        });
    }

    // Everything that can reach an external daemon now runs *behind* that first
    // frame, dispatched by the loop rather than ahead of it.
    loop_.post([this] { startBackends(); });

    loop_.run();
    return 0;
}

// Bring the applets to life. Called from the event loop after the first frame
// is on screen, so a daemon that is slow, missing, or still being activated
// delays only its own indicator — never the bar itself.
void BarApp::startBackends() {
    battery_.start();
    brightness_.start();
    wifi_.start();
    bluetooth_.start();
    volume_.start();
    sni_.start();
    powerProfiles_.setOnChange([this] { invalidate(); });
    powerProfiles_.start();
    // Location for the solar auto-palette: re-theme when the first fix lands
    // (or a later one moves the window). Absent/denied GeoClue simply never
    // fires — the fixed hours carry the mode.
    geoClue_.setOnChange([this] {
        refreshSolarTimes();
        if (palette_.tick(theme::localHourNow())) {
            applyTheme();
            invalidate();
        }
    });
    geoClue_.start();
    refreshSolarTimes();
    notifications_.setOnChange([this] { invalidate(); });
    if (!notifications_.start(/*seedFromLog=*/false)) {
        std::fprintf(stderr, "qypr-bar: notification monitor unavailable\n");
    }
    mpris_.enablePush(loop_);

    // Persist each push so the *next* start has fresh values to seed from.
    stateCache_.track(loop_, backends_);
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
    battery_.start();
    brightness_.start();
    wifi_.start();
    bluetooth_.start();
    volume_.start();
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
        Rect const dndBounds = statusBar_.quickSettings().findTileBounds("Do Not Disturb");
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

void BarApp::watchPalette() {
    paletteWatcher_.stop();
    paletteLightWatcher_.stop();

    // Arm one watcher per palette file: dark is always watched; the light file
    // too when the mode can be light (auto or light), so a matugen run in
    // either mode re-themes the bar.
    const auto arm = [this](ConfigWatcher& w, const std::string& path) {
        if (path.empty()) { return; }
        // The palette file may not exist yet (matugen's first run); watching
        // the directory still catches its creation. A missing *directory*,
        // though, cannot be watched at all — wait until a config reload names
        // one that exists.
        std::error_code ec;
        const std::filesystem::path parent = std::filesystem::path(path).parent_path();
        if (!std::filesystem::is_directory(parent, ec)) { return; }
        w.watch(path, [this] { reloadConfig(); });
    };

    arm(paletteWatcher_, theme::resolveColorsPath(config_, /*lightPalette=*/false));
    if (palette_.mode != "dark") {
        arm(paletteLightWatcher_, theme::resolveColorsPath(config_, /*lightPalette=*/true));
    }
}

void BarApp::reloadConfig() {
    Config c;
    if (!c.load(config_.path())) {
        return;  // file vanished or unreadable — keep current theme
    }

    std::fprintf(stderr, "qypr-bar: config changed, reloading\n");
    config_ = std::move(c);

    // Reload theme (colours, fonts, spacing, style): re-parse the mode
    // keys (they may have flipped), refresh the solar cache against the new
    // mode, re-resolve, and apply once.
    palette_ = theme::AutoPalette::fromConfig(config_, theme::localHourNow());
    refreshSolarTimes();
    palette_.tick(theme::localHourNow());
    applyTheme();

    watchPalette();

    // Reload bar geometry (height, margin, position).
    geom_ = readGeometry(config_);
    statusBar_.setGeometry(geom_);
    const int reserved = reservedFor(geom_);
    display_.setOverlayHeight(reserved);  // update exclusive zone

    // Reload backdrop.
    const double alpha = config_.getDouble("bar", "backdrop", -1.0);
    statusBar_.setBackdrop(alpha != 0.0, alpha);

    // Reload modules (recreate indicators from new module list).
    modules_ = readModules(config_);
    statusBar_.reloadModules(backends_, modules_ ? &*modules_ : nullptr);

    invalidate();
}

void BarApp::applyTheme() {
    theme_ = theme::loadThemeState(config_, palette_);
    statusBar_.setTheme(theme_);
    notifications_.setTheme(theme_);
}

void BarApp::refreshSolarTimes() {
    if (palette_.location != "auto") {
        palette_.clearSolarTimes();
        return;
    }
    const auto& fix = geoClue_.fix();
    if (!fix) {
        palette_.clearSolarTimes();
        return;
    }
    const auto times =
        solarTimesForDate(fix->latitude, fix->longitude, localDateNow(), localTzOffsetMin());
    if (!times) {
        palette_.clearSolarTimes();  // polar day/night: fixed hours carry the mode
        return;
    }
    palette_.setSolarTimes(times->sunriseMin, times->sunsetMin);
}

void BarApp::syncOverlay() {
    const int overlay = statusBar_.overlayHeight();
    const int want = overlay > 0 ? overlay : reservedFor(geom_);
    if (want == overlayHeight_) { return; }
    overlayHeight_ = want;
    loop_.post([this, want] { display_.setOverlayHeight(want); });
}

void BarApp::syncKeyboard() {
    // Grab keyboard focus (layer-shell EXCLUSIVE) only while a popover that needs
    // typed input is open — the launcher search box — and release it otherwise so
    // the bar never steals keys from the focused application. Called from the
    // input handlers (not draw), so a direct commit is safe here.
    const bool want = statusBar_.wantsKeyboard();
    if (want == kbActive_) { return; }
    kbActive_ = want;
    display_.setKeyboardInteractive(want);
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
