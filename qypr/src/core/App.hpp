// App.hpp - Wires the platform, auth, power, and UI layers together.
//
// Implements RenderHost so the UI can request repaints and unlock. Owns the
// single event loop everything runs on.

#pragma once

#include "auth/PamAuthenticator.hpp"
#include "core/EventLoop.hpp"
#include "core/Interfaces.hpp"
#include "mpris/MprisController.hpp"
#include "notifications/NotificationMonitor.hpp"
#include "power/SystemActions.hpp"
#include "system/BatteryBackend.hpp"
#include "system/BluetoothBackend.hpp"
#include "system/BrightnessBackend.hpp"
#include "system/DndState.hpp"
#include "system/GeoClueBackend.hpp"
#include "system/SNIBackend.hpp"
#include "system/SystemBus.hpp"
#include "system/VolumeBackend.hpp"
#include "system/WifiBackend.hpp"
#include "core/LockRuntime.hpp"
#include "core/ThemeRuntime.hpp"
#include "ui/AudioController.hpp"
#include "ui/Shell.hpp"
#include "ui/Theme.hpp"
#include "video/VideoPlayer.hpp"
#include "wayland/LockSession.hpp"
#include "wayland/WaylandDisplay.hpp"

namespace qypr {

class Config;

class App : public RenderHost {
public:
    App();

    int run();

    // Idle seconds before the video pauses and the screen dims (default 60).
    void setIdleTimeout(int seconds);
    // Render idle + revealed frames to PNGs (no Wayland lock) for visual
    // verification and previewing. Writes <path> and <path>-idle.png.
    int preview(const std::string& path, int width = 1920, int height = 1080);

    // Exercise the video pipeline (EGL + mpv) offscreen without locking the
    // session. Runs for `seconds` and reports whether frames were produced.
    int videoTest(int seconds = 6);

    // RenderHost
    void invalidate() override;
    void requestUnlock() override;

private:
    EventLoop loop_;
    WaylandDisplay display_;
    LockSession lock_;
    PamAuthenticator pam_;
    SystemActions power_{loop_};
    MprisController mpris_;
    AudioController audio_;
    VideoPlayer video_;
    NotificationMonitor notifications_{loop_};

    // Status bar backends: one shared connection per bus (system + session);
    // the aggregate is handed to Shell → StatusBar (indicators consume
    // snapshots only).
    SystemBus systemBus_{loop_};
    SystemBus sessionBus_{loop_, BusKind::Session};
    BatteryBackend battery_{systemBus_};
    BrightnessBackend brightness_{loop_, systemBus_};
    WifiBackend wifi_{systemBus_};
    BluetoothBackend bluetooth_{systemBus_};
    VolumeBackend volume_{loop_};
    SNIBackend sni_{sessionBus_};
    // City-accurate fix for the solar auto-palette (same source as the bar).
    // Absent/denied → the lock keeps the fixed theme hours.
    GeoClueBackend geoClue_{systemBus_};
    DndState dnd_;
    SystemBackends backends_{.hasSession = false,
                             .battery = &battery_,
                             .volume = &volume_,
                             .brightness = &brightness_,
                             .wifi = &wifi_,
                             .bluetooth = &bluetooth_,
                             .sni = nullptr,
                             .dnd = &dnd_};

    ThemeRuntime themeRuntime_{loop_};
    LockRuntime lockRuntime_{loop_, display_, lock_};

    Shell shell_;
};

}  // namespace qypr
