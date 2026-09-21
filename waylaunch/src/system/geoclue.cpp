#include "waylaunch/geoclue.h"

#include <systemd/sd-bus.h>

#include <cstdio>

namespace waylaunch {
namespace {

const char* kService = "org.freedesktop.GeoClue2";
const char* kManagerPath = "/org/freedesktop/GeoClue2/Manager";
const char* kManagerIface = "org.freedesktop.GeoClue2.Manager";
const char* kClientIface = "org.freedesktop.GeoClue2.Client";
const char* kLocationIface = "org.freedesktop.GeoClue2.Location";
// Desktop ID GeoClue authorizes against (whitelist entry in
// /etc/geoclue/geoclue.conf, without the .desktop suffix).
const char* kDesktopId = "waylaunch";
const unsigned kAccuracyCity = 4;
const unsigned kDistanceThresholdM = 20000;
const unsigned kTimeThresholdSec = 3600;

sd_bus* bus_of(void* p) { return static_cast<sd_bus*>(p); }

} // namespace

geoclue_client::~geoclue_client() {
    if (bus_ != nullptr) {
        sd_bus_unref(bus_of(bus_));
        bus_ = nullptr;
    }
}

bool geoclue_client::start() {
    if (bus_ != nullptr) return fix_.has_value();
    sd_bus* bus = nullptr;
    if (sd_bus_open_system(&bus) < 0) return false;

    sd_bus_error err = SD_BUS_ERROR_NULL;
    sd_bus_message* reply = nullptr;
    int r = sd_bus_call_method(bus, kService, kManagerPath, kManagerIface, "GetClient", &err,
                               &reply, "");
    if (r < 0) {
        std::fprintf(stderr, "waylaunch: geoclue unavailable (%s); static theme mode applies\n",
                     err.message != nullptr ? err.message : "no client");
        sd_bus_error_free(&err);
        sd_bus_unref(bus);
        return false;
    }
    const char* path = nullptr;
    {
        // read_basic takes void*: route through a void* slot with an explicit
        // cast rather than converting const char** implicitly.
        void* raw = nullptr;
        sd_bus_message_read_basic(reply, 'o', static_cast<void*>(&raw));
        path = static_cast<const char*>(raw);
    }
    if (path != nullptr) client_path_ = path;
    sd_bus_message_unref(reply);
    sd_bus_error_free(&err);
    if (client_path_.empty()) {
        sd_bus_unref(bus);
        return false;
    }

    r = sd_bus_set_property(bus, kService, client_path_.c_str(), kClientIface, "DesktopId", &err,
                            "s", kDesktopId);
    if (r < 0) {
        std::fprintf(stderr, "waylaunch: geoclue denied (%s); static theme mode applies\n",
                     err.message != nullptr ? err.message : "DesktopId rejected");
        sd_bus_error_free(&err);
        sd_bus_unref(bus);
        client_path_.clear();
        return false;
    }
    sd_bus_error_free(&err);
    sd_bus_set_property(bus, kService, client_path_.c_str(), kClientIface, "RequestedAccuracyLevel",
                        &err, "u", kAccuracyCity);
    sd_bus_error_free(&err);
    sd_bus_set_property(bus, kService, client_path_.c_str(), kClientIface, "DistanceThreshold",
                        &err, "u", kDistanceThresholdM);
    sd_bus_error_free(&err);
    sd_bus_set_property(bus, kService, client_path_.c_str(), kClientIface, "TimeThreshold", &err,
                        "u", kTimeThresholdSec);
    sd_bus_error_free(&err);

    r = sd_bus_call_method(bus, kService, client_path_.c_str(), kClientIface, "Start", &err,
                           nullptr, "");
    if (r < 0) {
        std::fprintf(stderr, "waylaunch: geoclue start failed (%s); static theme mode applies\n",
                     err.message != nullptr ? err.message : "unknown");
        sd_bus_error_free(&err);
        sd_bus_unref(bus);
        client_path_.clear();
        return false;
    }
    sd_bus_error_free(&err);

    bus_ = bus;
    return refresh();
}

bool geoclue_client::refresh() {
    if (bus_ == nullptr || client_path_.empty()) return false;
    sd_bus* bus = bus_of(bus_);

    std::string location_path;
    {
        sd_bus_error err = SD_BUS_ERROR_NULL;
        sd_bus_message* reply = nullptr;
        int r = sd_bus_get_property(bus, kService, client_path_.c_str(), kClientIface, "Location",
                                    &err, &reply, "o");
        if (r < 0 || reply == nullptr) {
            sd_bus_error_free(&err);
            return fix_.has_value();
        }
        const char* path = nullptr;
        {
            void* raw = nullptr;
            sd_bus_message_read_basic(reply, 'o', static_cast<void*>(&raw));
            path = static_cast<const char*>(raw);
        }
        if (path != nullptr) location_path = path;
        sd_bus_message_unref(reply);
        sd_bus_error_free(&err);
        if (location_path.empty() || location_path == "/") return fix_.has_value();
    }

    geo_fix next;
    {
        sd_bus_error err = SD_BUS_ERROR_NULL;
        bool ok = sd_bus_get_property_trivial(bus, kService, location_path.c_str(), kLocationIface,
                                              "Latitude", &err, 'd', &next.latitude) >= 0 &&
                  sd_bus_get_property_trivial(bus, kService, location_path.c_str(), kLocationIface,
                                              "Longitude", &err, 'd', &next.longitude) >= 0 &&
                  sd_bus_get_property_trivial(bus, kService, location_path.c_str(), kLocationIface,
                                              "Accuracy", &err, 'd', &next.accuracy) >= 0;
        sd_bus_error_free(&err);
        if (!ok) return fix_.has_value();
    }
    fix_ = next;
    return true;
}

} // namespace waylaunch
