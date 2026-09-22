// BarApp.hpp - Standalone status-bar shell for the unlocked desktop.
//
// A waybar replacement: it hosts the *same* StatusBar as the lock screen, but
// on wlr-layer-shell instead of a session-lock surface, and turns on the
// session-sensitive WM widgets (workspaces + active window) that the lock
// screen keeps hidden. No LockScreen, no PAM, no video — just the bar.
//
// Implements Invalidator (StatusBar's repaint hook — never RenderHost, so the
// bar cannot reach lock-only powers) and InputSink (pointer routing from Seat).

#pragma once

#include <cstdint>
#include <optional>
#include <string>

#include "core/BarBackendLifecycle.hpp"
#include "core/BarRuntime.hpp"
#include "core/BarSurfaceController.hpp"
#include "core/Config.hpp"
#include "core/ConfigRuntime.hpp"
#include "core/ConfigWatcher.hpp"
#include "core/EventLoop.hpp"
#include "core/Interfaces.hpp"
#include "core/ThemeRuntime.hpp"
#include "mpris/MprisController.hpp"
#include "notifications/NotificationActions.hpp"
#include "notifications/NotificationMonitor.hpp"
#include "power/SystemActions.hpp"
#include "system/PowerProfilesBackend.hpp"
#include "system/GeoClueBackend.hpp"
#include "system/IdleInhibitor.hpp"
#include "system/DesktopIndex.hpp"
#include "system/KeyboardLayout.hpp"
#include "system/BatteryBackend.hpp"
#include "system/BluetoothBackend.hpp"
#include "system/BrightnessBackend.hpp"
#include "system/DbusMenuBackend.hpp"
#include "system/DndState.hpp"
#include "system/NightLightBackend.hpp"
#include "system/SNIBackend.hpp"
#include "system/StateCache.hpp"
#include "system/SystemBus.hpp"
#include "system/ToplevelBackend.hpp"
#include "system/VolumeBackend.hpp"
#include "system/WifiBackend.hpp"
#include "system/WorkspaceBackend.hpp"
#include "ui/Theme.hpp"
#include "ui/statusbar/StatusBar.hpp"
#include "wayland/BarDisplay.hpp"

namespace qypr {

class BarApp : public Invalidator, public InputSink {
public:
    BarApp();

    int run();

    // Render standalone bar + QS frames to PNGs (no Wayland connection) for
    // visual verification. Writes <path>, <path>-qs.png, and <path>-dnd.png.
    int preview(const std::string& path, int width = 1920, int height = 1080);

    // Invalidator
    void invalidate() override;

    // InputSink
    void onTextInput(const std::string& utf8) override;
    void onSpecialKey(uint32_t keysym, uint32_t modifiers) override;
    void onLayoutChanged(const std::string& name, uint32_t index, uint32_t count) override;
    void onPointerMotion(int w, int h, double x, double y) override;
    void onPointerButton(int w, int h, double x, double y, uint32_t button, bool pressed) override;
    void onPointerScroll(int w, int h, double x, double y, double dx, double dy) override;
    void onPointerLeave() override;

private:
    void draw(cairo_t* cr, int w, int h, int scale);
    // Start every backend that can reach an external daemon. Runs from the event
    // loop *after* the first frame is on screen, so a daemon that is missing or
    // still being activated delays only its own indicator, never the bar.
    void startBackends();
    // Grow/shrink the layer surfaces when the open-overlay state flips.
    void syncOverlay();
    // Grab/release keyboard focus when a search popover (launcher) opens/closes.
    void syncKeyboard();
    // Re-read bar.conf and re-apply all sections without a restart.
    void reloadConfig();
    void reloadConfig(const Config& newConfig, const BarGeometry& newGeom,
                      const std::optional<IndicatorRegistry::ModuleSelection>& newModules);

    // Rebuild the owned State from config_ + palette_ and cascade to the
    // status bar + monitor.
    void applyTheme();

    // Declaration order is initialisation order: config_ must precede
    // everything that reads it (geometry_, modules_, display_, statusBar_).
    Config config_{ConfigRuntime::loadConfig()};
    BarGeometry geom_{ConfigRuntime::readGeometry(config_)};
    std::optional<IndicatorRegistry::ModuleSelection> modules_{ConfigRuntime::readModules(config_)};

    EventLoop loop_;
    BarDisplay display_{loop_, ConfigRuntime::reservedFor(geom_), geom_.bottom};

    // One shared connection per bus (system + session), same as the lock app.
    SystemBus systemBus_{loop_};
    SystemBus sessionBus_{loop_, BusKind::Session};
    BatteryBackend battery_{systemBus_};
    BrightnessBackend brightness_{loop_, systemBus_};
    WifiBackend wifi_{systemBus_};
    BluetoothBackend bluetooth_{systemBus_};
    VolumeBackend volume_{loop_};
    SNIBackend sni_{sessionBus_};
    // Tray item context menus, on the shared session bus (bar-only).
    DbusMenuBackend dbusMenu_{sessionBus_};
    WorkspaceBackend workspace_;  // ext-workspace-v1 (on display_'s wl_display)
    ToplevelBackend toplevel_;    // active window (wlr-foreign-toplevel)
    DndState dnd_;
    NightLightBackend nightLight_;  // init() + setOutputs() after display_.connect()
    // Session surface (Phase 10): already-built subsystems the lock app owns
    // too — the bar simply hosts them as applets.
    NotificationMonitor notifications_{loop_};
    // Dismissal must not ride the monitor connection (BecomeMonitor may never
    // send); it reuses the shared session bus instead.
    NotificationActions notificationActions_{sessionBus_};
    SystemActions power_{loop_};
    PowerProfilesBackend powerProfiles_{systemBus_};
    // City-accurate fix for the solar auto-palette (bar-only; the lock app
    // never constructs this). Absent/denied → theme keeps fixed hours.
    GeoClueBackend geoClue_{systemBus_};
    IdleInhibitor idleInhibitor_;  // init()'d after display_.connect()
    MprisController mpris_;
    // Application launcher index (bar-only). load()'ed in run(); the lock app
    // never constructs this, so its launcher never appears while locked.
    DesktopIndex desktopIndex_;
    // Active keyboard-layout state (bar-only), fed by the Seat via
    // onLayoutChanged(); the lock app never constructs this.
    KeyboardLayout kbLayout_;
    SystemBackends backends_{.battery = &battery_,
                             .volume = &volume_,
                             .brightness = &brightness_,
                             .wifi = &wifi_,
                             .bluetooth = &bluetooth_,
                             .sni = &sni_,
                             .dbusMenu = &dbusMenu_,
                             .workspace = &workspace_,
                             .toplevel = &toplevel_,
                             .dnd = &dnd_,
                             .nightLight = &nightLight_,
                             .power = &power_,
                             .powerProfiles = &powerProfiles_,
                             .idleInhibitor = &idleInhibitor_,
                             .desktopIndex = &desktopIndex_,
                             .keyboardLayout = &kbLayout_,
                             .loop = &loop_,
                             .notifications = &notifications_,
                             .notificationActions = &notificationActions_,
                             .mpris = &mpris_,
                             .config = &config_,
                             .sessionSurface = true};

    StatusBar statusBar_{loop_, *this, backends_, modules_ ? &*modules_ : nullptr};
    // Last-known indicator values, so the first frame shows real numbers instead
    // of neutral placeholders while the daemons are still starting.
    StateCache stateCache_;

    // Runtime coordinators
    ConfigRuntime configRuntime_{loop_};
    ThemeRuntime themeRuntime_{loop_};
    BarSurfaceController surfaceController_{display_, loop_, ConfigRuntime::reservedFor(geom_)};
    BarBackendLifecycle backendLifecycle_{loop_, backends_, stateCache_, geoClue_, *this};
    BarRuntime runtime_{loop_,          display_,      surfaceController_,
                        configRuntime_, themeRuntime_, backendLifecycle_};
};

}  // namespace qypr
