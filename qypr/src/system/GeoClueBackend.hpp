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

#include "core/RetryTimer.hpp"

struct sd_bus_slot;
#include <systemd/sd-bus.h>  // sd_bus_error is a typedef here, not a struct

namespace qypr {

class SystemBus;
class EventLoop;

struct GeoFix {
    double latitude = 0.0;
    double longitude = 0.0;
    double accuracyM = 0.0;  // radius of the fix, metres

    bool operator==(const GeoFix&) const = default;
};

class GeoClueBackend {
public:
    GeoClueBackend(EventLoop& loop, SystemBus& systemBus);
    ~GeoClueBackend();

    GeoClueBackend(const GeoClueBackend&) = delete;
    GeoClueBackend& operator=(const GeoClueBackend&) = delete;

    // Asynchronously fetch/configure/start our GeoClue client and subscribe to
    // LocationUpdated. Returns false only when the system bus is unavailable;
    // a remote timeout cannot stall the shared event loop. Safe to call again.
    bool start();

    bool available() const { return fix_.has_value(); }
    const std::optional<GeoFix>& fix() const { return fix_; }
    void setOnChange(std::function<void()> cb) { onChange_ = std::move(cb); }

private:
    static int onLocationUpdated(sd_bus_message*, void*, sd_bus_error*);
    static int onGetClient(sd_bus_message*, void*, sd_bus_error*);
    static int onDesktopIdSet(sd_bus_message*, void*, sd_bus_error*);
    static int onStartClient(sd_bus_message*, void*, sd_bus_error*);
    static int onLocationPath(sd_bus_message*, void*, sd_bus_error*);
    static int onLocationProperties(sd_bus_message*, void*, sd_bus_error*);
    // Start or coalesce an async read of the client's current fix.
    void refreshFix();
    void finishFixFetch(bool success);
    void failStart(const char* stage, sd_bus_message* reply);

    SystemBus& bus_;
    RetryTimer retry_;
    sd_bus_slot* slot_ = nullptr;
    std::string clientPath_;
    bool startInFlight_ = false;
    bool started_ = false;
    bool fixFetchInFlight_ = false;
    bool fixRefreshPending_ = false;
    std::optional<GeoFix> fix_;
    std::function<void()> onChange_;
};

}  // namespace qypr
