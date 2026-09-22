// StateCacheCodec.hpp - Typed snapshot ↔ text conversion for the state cache.
//
// Handles INI serialization and deserialization of backend snapshots,
// plus the seed logic that pushes loaded values into backends. Pure
// data conversion: no file I/O, no event-loop, no side effects.

#pragma once

#include <string>

#include "system/BatteryBackend.hpp"
#include "system/BluetoothBackend.hpp"
#include "system/BrightnessBackend.hpp"
#include "system/VolumeBackend.hpp"
#include "system/WifiBackend.hpp"
#include "ui/statusbar/StatusIndicator.hpp"

namespace qypr {

class StateCacheCodec {
public:
    // Read the cache file into the snapshot members. Never fails:
    // an absent or unparsable file leaves every value at its default.
    void load(const std::string& path);

    // Push the loaded values into the backends that support seeding.
    // Call before the first paint and before the backends are started,
    // so the first frame carries real numbers. Backends that have
    // already produced a live reply ignore the seed.
    void seed(const SystemBackends& backends) const;

    // Serialize the tracked backends to the cache format.
    [[nodiscard]] std::string serialize(const SystemBackends& backends) const;

private:
    BatterySnapshot battery_;
    VolumeSnapshot volume_;
    WifiSnapshot wifi_;
    BluetoothSnapshot bluetooth_;
    BrightnessSnapshot brightness_;
    bool loaded_ = false;
};

}  // namespace qypr
