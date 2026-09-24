// LockRuntime.cpp - Implementation of lock runtime coordinator.
#include "core/LockRuntime.hpp"

#include <cstdio>

#include "notifications/NotificationMonitor.hpp"
#include "system/BatteryBackend.hpp"
#include "system/BluetoothBackend.hpp"
#include "system/BrightnessBackend.hpp"
#include "system/SNIBackend.hpp"
#include "system/VolumeBackend.hpp"
#include "system/WifiBackend.hpp"

namespace qypr {

LockRuntime::LockRuntime(EventLoop& loop, WaylandDisplay& display, LockSession& lock)
    : loop_(loop),
      display_(display),
      lock_(lock) {}

bool LockRuntime::acquire(InputSink* sink, RenderFn renderFn, AnimatingFn animatingFn) {
    if (!display_.connect()) {
        std::fprintf(stderr, "qypr-lock: no Wayland display or no ext-session-lock support\n");
        return false;
    }

    display_.setInputSink(sink);
    display_.setRenderFn(std::move(renderFn));
    display_.setAnimatingFn(std::move(animatingFn));

    lock_.setOnFinished([this] {
        std::fprintf(stderr, "qypr-lock: session lock refused or lost\n");
        loop_.quit();
    });

    if (!lock_.lock()) {
        std::fprintf(stderr, "qypr-lock: failed to acquire session lock\n");
        return false;
    }

    // The lock request and per-output surfaces are queued by lock(), but the
    // event loop does not start until optional startup work has completed
    // (notification backlog, GeoClue, and mpv). Flush and dispatch the first
    // configure here so the compositor can cover the desktop immediately;
    // the initial frame is the normal no-video fallback and later startup work
    // can replace it without leaving the desktop exposed.
    display_.roundtrip();

    return true;
}

void LockRuntime::startBackends(SystemBackends& backends, NotificationMonitor& notifications) {
    notifications.start(/*seedFromLog=*/true);

    if (backends.battery) backends.battery->start();
    if (backends.brightness) backends.brightness->start();
    if (backends.wifi) backends.wifi->start();
    if (backends.bluetooth) backends.bluetooth->start();
    if (backends.volume) backends.volume->start();
    if (backends.sni) backends.sni->start();
}

void LockRuntime::unlockAndQuit() {
    lock_.unlock();
    loop_.quit();
}

}  // namespace qypr
