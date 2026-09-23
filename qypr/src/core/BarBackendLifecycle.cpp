// BarBackendLifecycle.cpp - Implementation of backend lifecycle manager.
#include "core/BarBackendLifecycle.hpp"

#include <cstdio>

#include "mpris/MprisController.hpp"
#include "notifications/NotificationMonitor.hpp"
#include "system/BatteryBackend.hpp"
#include "system/BluetoothBackend.hpp"
#include "system/BrightnessBackend.hpp"
#include "system/GeoClueBackend.hpp"
#include "system/PowerProfilesBackend.hpp"
#include "system/SNIBackend.hpp"
#include "system/StateCache.hpp"
#include "system/VolumeBackend.hpp"
#include "system/WifiBackend.hpp"

namespace qypr {

BarBackendLifecycle::BarBackendLifecycle(EventLoop& loop, SystemBackends& backends,
                                         StateCache& stateCache, GeoClueBackend& geoClue,
                                         Invalidator& invalidator)
    : loop_(loop),
      backends_(backends),
      stateCache_(stateCache),
      geoClue_(geoClue),
      invalidator_(invalidator) {}

void BarBackendLifecycle::start(std::function<void()> onGeoClueFix) {
    if (backends_.battery) backends_.battery->start();
    if (backends_.brightness) backends_.brightness->start();
    if (backends_.wifi) backends_.wifi->start();
    if (backends_.bluetooth) backends_.bluetooth->start();
    if (backends_.volume) backends_.volume->start();
    if (backends_.sni) backends_.sni->start();

    if (backends_.powerProfiles) {
        backends_.powerProfiles->setOnChange([this] { invalidator_.invalidate(); });
        backends_.powerProfiles->start();
    }

    if (onGeoClueFix) { geoClue_.setOnChange(std::move(onGeoClueFix)); }
    geoClue_.start();

    if (backends_.notifications) {
        backends_.notifications->setOnChange([this] { invalidator_.invalidate(); });
        if (!backends_.notifications->start(/*seedFromLog=*/false)) {
            std::fprintf(stderr, "qypr-bar: notification monitor unavailable\n");
        }
    }

    if (backends_.mpris) { backends_.mpris->enablePush(loop_); }

    // Persist each push so the next start has fresh values to seed from.
    stateCache_.track(loop_, backends_);
}

void BarBackendLifecycle::startPreview() const {
    if (backends_.battery) backends_.battery->start();
    if (backends_.brightness) backends_.brightness->start();
    if (backends_.wifi) backends_.wifi->start();
    if (backends_.bluetooth) backends_.bluetooth->start();
    if (backends_.volume) backends_.volume->start();
}

}  // namespace qypr
