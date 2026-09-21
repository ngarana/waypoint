// GeoClueBackend.cpp - GeoClue2 client implementation.
#include "system/GeoClueBackend.hpp"

#include <cstdio>

#include "system/SystemBus.hpp"

namespace qypr {

// Client constants live behind the same TESTING wall as their uses: the test
// binary never touches the bus, so without this they read as dead code.
#ifndef TESTING
namespace {
constexpr const char* kService = "org.freedesktop.GeoClue2";
constexpr const char* kManagerPath = "/org/freedesktop/GeoClue2/Manager";
constexpr const char* kManagerIface = "org.freedesktop.GeoClue2.Manager";
constexpr const char* kClientIface = "org.freedesktop.GeoClue2.Client";
constexpr const char* kLocationIface = "org.freedesktop.GeoClue2.Location";
// Desktop ID GeoClue authorizes against (whitelist entry in
// /etc/geoclue/geoclue.conf, without the .desktop suffix).
constexpr const char* kDesktopId = "qypr-bar";
// City accuracy is plenty for sunrise/sunset (tens of kilometres move the
// window by a minute); never request street-level or exact fixes.
constexpr uint32_t kAccuracyCity = 4;
// Coarse thresholds: re-fixes only matter across real travel, not across
// the street — and hourly polls catch a stale fix anyway.
constexpr uint32_t kDistanceThresholdM = 20000;
constexpr uint32_t kTimeThresholdSec = 3600;
}  // namespace
#endif

GeoClueBackend::~GeoClueBackend() {
    if (slot_) { sd_bus_slot_unref(slot_); }
}

bool GeoClueBackend::start() {
#ifdef TESTING
    return false;  // TESTING: never touch the test host's bus
#else
    if (!clientPath_.empty()) { return available(); }
    if (!bus_.available()) { return false; }
    sd_bus* bus = bus_.get();

    // Our client object (created for our peer when none exists yet).
    {
        sd_bus_error err = SD_BUS_ERROR_NULL;
        sd_bus_message* reply = nullptr;
        const int r = sd_bus_call_method(bus, kService, kManagerPath, kManagerIface, "GetClient",
                                         &err, &reply, "");
        if (r < 0) {
            std::fprintf(stderr, "qypr: geoclue unavailable (%s); fixed theme hours apply\n",
                         err.message != nullptr ? err.message : "no client");
            sd_bus_error_free(&err);
            return false;
        }
        const char* path = nullptr;
        sd_bus_message_read_basic(reply, 'o', &path);
        if (path != nullptr) { clientPath_ = path; }
        sd_bus_message_unref(reply);
        sd_bus_error_free(&err);
        if (clientPath_.empty()) { return false; }
    }

    // Configure the client: identity for the whitelist, then coarse accuracy.
    sd_bus_error err = SD_BUS_ERROR_NULL;
    int r = sd_bus_set_property(bus, kService, clientPath_.c_str(), kClientIface, "DesktopId", &err,
                                "s", kDesktopId);
    if (r < 0) {
        std::fprintf(stderr, "qypr: geoclue denied (%s); fixed theme hours apply\n",
                     err.message != nullptr ? err.message : "DesktopId rejected");
        sd_bus_error_free(&err);
        clientPath_.clear();
        return false;
    }
    sd_bus_error_free(&err);
    sd_bus_set_property(bus, kService, clientPath_.c_str(), kClientIface, "RequestedAccuracyLevel",
                        &err, "u", kAccuracyCity);
    sd_bus_error_free(&err);
    sd_bus_set_property(bus, kService, clientPath_.c_str(), kClientIface, "DistanceThreshold", &err,
                        "u", kDistanceThresholdM);
    sd_bus_error_free(&err);
    sd_bus_set_property(bus, kService, clientPath_.c_str(), kClientIface, "TimeThreshold", &err,
                        "u", kTimeThresholdSec);
    sd_bus_error_free(&err);

    r = sd_bus_call_method(bus, kService, clientPath_.c_str(), kClientIface, "Start", &err, nullptr,
                           "");
    if (r < 0) {
        std::fprintf(stderr, "qypr: geoclue start failed (%s); fixed theme hours apply\n",
                     err.message != nullptr ? err.message : "unknown");
        sd_bus_error_free(&err);
        clientPath_.clear();
        return false;
    }
    sd_bus_error_free(&err);

    // Push: re-read the fix whenever GeoClue publishes a new location. Scoped
    // to our own client path so other apps' fixes never re-theme the bar.
    const std::string rule = std::string("type='signal',sender='") + kService + "',interface='" +
                             kClientIface + "',member='LocationUpdated',path='" + clientPath_ + "'";
    slot_ = bus_.addMatch(rule.c_str(), &GeoClueBackend::onLocationUpdated, this);

    refreshFix();
    return available();
#endif
}

int GeoClueBackend::onLocationUpdated(sd_bus_message*, void* userdata, sd_bus_error*) {
    static_cast<GeoClueBackend*>(userdata)->refreshFix();
    return 0;
}

bool GeoClueBackend::refreshFix() {
#ifndef TESTING
    if (clientPath_.empty() || !bus_.available()) { return false; }
    sd_bus* bus = bus_.get();

    // The client's Location object path ("/" while no fix is known yet).
    std::string locationPath;
    {
        sd_bus_error err = SD_BUS_ERROR_NULL;
        sd_bus_message* reply = nullptr;
        const int r = sd_bus_get_property(bus, kService, clientPath_.c_str(), kClientIface,
                                          "Location", &err, &reply, "o");
        if (r < 0 || reply == nullptr) {
            sd_bus_error_free(&err);
            return false;
        }
        const char* path = nullptr;
        sd_bus_message_read_basic(reply, 'o', &path);
        if (path != nullptr) { locationPath = path; }
        sd_bus_message_unref(reply);
        sd_bus_error_free(&err);
        if (locationPath.empty() || locationPath == "/") { return false; }
    }

    GeoFix next;
    {
        sd_bus_error err = SD_BUS_ERROR_NULL;
        const bool ok =
            sd_bus_get_property_trivial(bus, kService, locationPath.c_str(), kLocationIface,
                                        "Latitude", &err, 'd', &next.latitude) >= 0 &&
            sd_bus_get_property_trivial(bus, kService, locationPath.c_str(), kLocationIface,
                                        "Longitude", &err, 'd', &next.longitude) >= 0 &&
            sd_bus_get_property_trivial(bus, kService, locationPath.c_str(), kLocationIface,
                                        "Accuracy", &err, 'd', &next.accuracyM) >= 0;
        sd_bus_error_free(&err);
        if (!ok) { return false; }
    }

    if (fix_ == next) { return true; }
    fix_ = next;
    if (onChange_) { onChange_(); }
    return true;
#else
    return false;
#endif
}

}  // namespace qypr
