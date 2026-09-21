// GeoClueBackend.hpp - org.freedesktop.GeoClue2 location client.
//
// A city-accurate fix for the bar's solar auto-palette: the bar only needs
// to know roughly where it is to compute sunrise/sunset. Requests
// RequestedAccuracyLevel=City (4) — street-level and below are never asked
// for. Degrades to unavailable (no fix) when GeoClue is absent, denies us,
// or yields no location: the theme falls back to the configured fixed
// hours. GeoClue consent is enforced platform-side (geoclue.conf whitelist
// or agent prompt), never by this code.
#pragma once

#include <functional>
#include <optional>
#include <string>

struct sd_bus_slot;
#include <systemd/sd-bus.h>  // sd_bus_error is a typedef here, not a struct

namespace qypr {

class SystemBus;

struct GeoFix {
    double latitude = 0.0;
    double longitude = 0.0;
    double accuracyM = 0.0;  // radius of the fix, metres

    bool operator==(const GeoFix&) const = default;
};

class GeoClueBackend {
public:
    explicit GeoClueBackend(SystemBus& systemBus) : bus_(systemBus) {}
    ~GeoClueBackend();

    GeoClueBackend(const GeoClueBackend&) = delete;
    GeoClueBackend& operator=(const GeoClueBackend&) = delete;

    // Fetch our GeoClue client, configure it (DesktopId, City accuracy,
    // coarse thresholds), Start() it and subscribe to LocationUpdated.
    // Returns false when GeoClue is absent or denies us — the caller keeps
    // the fixed-hour fallback. Safe to call once; a second call is a no-op
    // reporting current availability.
    bool start();

    bool available() const { return fix_.has_value(); }
    const std::optional<GeoFix>& fix() const { return fix_; }
    void setOnChange(std::function<void()> cb) { onChange_ = std::move(cb); }

private:
    static int onLocationUpdated(sd_bus_message*, void*, sd_bus_error*);
    // (Re)read the client's Location object, then latitude/longitude/
    // accuracy. Fires onChange only when the fix actually moved.
    bool refreshFix();

    SystemBus& bus_;
    sd_bus_slot* slot_ = nullptr;
    std::string clientPath_;
    std::optional<GeoFix> fix_;
    std::function<void()> onChange_;
};

}  // namespace qypr
