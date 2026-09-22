// ServiceBundles.hpp - Narrow capability bundles for status indicators.
#pragma once

#include <cstdint>
#include <string>

namespace qypr {

class BatteryBackend;
class VolumeBackend;
class BrightnessBackend;
class WifiBackend;
class BluetoothBackend;
class SNIBackend;
class DbusMenuBackend;
class WorkspaceBackend;
class ToplevelBackend;
class DndState;
class NightLightBackend;
class SystemActions;
class PowerProfilesBackend;
class IdleInhibitor;
class DesktopIndex;
class KeyboardLayout;
class EventLoop;
class NotificationMonitor;
class NotificationActions;
class MprisController;
class Config;

// Connectivity services: Wi-Fi and Bluetooth.
struct ConnectivityServices {
    WifiBackend* wifi = nullptr;
    BluetoothBackend* bluetooth = nullptr;
};

// Media services: volume level and MPRIS media player.
struct MediaServices {
    VolumeBackend* volume = nullptr;
    MprisController* mpris = nullptr;
};

// Session services: workspaces, window toplevels, keyboard layout, desktop
// index, tray/dbus-menu, power actions, and child-process spawning.
// Unlocked-session only: NEVER provided to the lock screen runtime.
struct SessionServices {
    WorkspaceBackend* workspace = nullptr;
    ToplevelBackend* toplevel = nullptr;
    KeyboardLayout* keyboardLayout = nullptr;
    DesktopIndex* desktopIndex = nullptr;
    SNIBackend* sni = nullptr;
    DbusMenuBackend* dbusMenu = nullptr;
    SystemActions* power = nullptr;
    PowerProfilesBackend* powerProfiles = nullptr;
    EventLoop* loop = nullptr;
};

// Notification services: monitor, actions (dismiss/clear), DND toggle.
struct NotificationServices {
    NotificationMonitor* notifications = nullptr;
    NotificationActions* notificationActions = nullptr;
    DndState* dnd = nullptr;
};

// Safe lock services: safe indicators permitted on the lock screen (battery,
// brightness, night light, idle inhibitor, config, and surface policy).
struct SafeLockServices {
    BatteryBackend* battery = nullptr;
    BrightnessBackend* brightness = nullptr;
    NightLightBackend* nightLight = nullptr;
    IdleInhibitor* idleInhibitor = nullptr;
    const Config* config = nullptr;
    bool sessionSurface = false;
};

}  // namespace qypr
