// GeoClueBackend.cpp - GeoClue2 client implementation.
#include "system/GeoClueBackend.hpp"

#include <systemd/sd-bus.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <utility>

#include "core/EventLoop.hpp"
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
constexpr const char* kPropertiesIface = "org.freedesktop.DBus.Properties";
// Desktop ID GeoClue authorizes against (whitelist entry in
// /etc/geoclue/geoclue.conf, without the .desktop suffix).
constexpr const char* kDesktopId = "qypr-bar";
// City accuracy is plenty for sunrise/sunset (tens of kilometres move the
// window by a minute); street-level and exact fixes are never requested.
constexpr uint32_t kAccuracyCity = 4;
constexpr uint32_t kDistanceThresholdM = 20000;
constexpr uint32_t kTimeThresholdSec = 3600;

bool isRetryable(sd_bus_message* reply) {
    const sd_bus_error* error = sd_bus_message_get_error(reply);
    const char* name = error != nullptr ? error->name : nullptr;
    if (name == nullptr) { return true; }
    // A policy/configuration denial will not heal through retries. Service and
    // transport errors can, and RetryTimer caps their traffic at 30s intervals.
    return std::strstr(name, "AccessDenied") == nullptr && std::strstr(name, "Denied") == nullptr &&
           std::strstr(name, "NotAuthorized") == nullptr &&
           std::strstr(name, "PermissionDenied") == nullptr &&
           std::strstr(name, "Rejected") == nullptr;
}

const char* errorMessage(sd_bus_message* reply) {
    const sd_bus_error* error = sd_bus_message_get_error(reply);
    return error != nullptr && error->message != nullptr ? error->message : "request failed";
}

bool readDoubleVariant(sd_bus_message* message, double* value) {
    if (sd_bus_message_enter_container(message, 'v', "d") <= 0) {
        sd_bus_message_skip(message, "v");
        return false;
    }
    const int result = sd_bus_message_read_basic(message, 'd', value);
    sd_bus_message_exit_container(message);
    return result >= 0;
}

bool readLocation(sd_bus_message* message, GeoFix* fix) {
    bool latitudeRead = false;
    bool longitudeRead = false;
    bool accuracyRead = false;
    if (sd_bus_message_enter_container(message, 'a', "{sv}") <= 0) { return false; }
    while (sd_bus_message_enter_container(message, 'e', "sv") > 0) {
        const char* key = nullptr;
        sd_bus_message_read(message, "s", &key);
        bool recognized = false;
        if (key != nullptr && std::strcmp(key, "Latitude") == 0) {
            recognized = true;
            latitudeRead = readDoubleVariant(message, &fix->latitude);
        } else if (key != nullptr && std::strcmp(key, "Longitude") == 0) {
            recognized = true;
            longitudeRead = readDoubleVariant(message, &fix->longitude);
        } else if (key != nullptr && std::strcmp(key, "Accuracy") == 0) {
            recognized = true;
            accuracyRead = readDoubleVariant(message, &fix->accuracyM);
        }
        if (!recognized) { sd_bus_message_skip(message, "v"); }
        sd_bus_message_exit_container(message);
    }
    sd_bus_message_exit_container(message);
    return latitudeRead && longitudeRead && accuracyRead;
}
}  // namespace
#endif

GeoClueBackend::GeoClueBackend(EventLoop& loop, SystemBus& systemBus)
    : bus_(systemBus),
      retry_(loop, [this] {
          if (started_) {
              refreshFix();
          } else {
              start();
          }
      }) {}

GeoClueBackend::~GeoClueBackend() {
    retry_.cancel();
    if (slot_) { sd_bus_slot_unref(slot_); }
}

bool GeoClueBackend::start() {
#ifdef TESTING
    return false;  // TESTING: never touch the test host's bus
#else
    if (!bus_.available()) { return false; }
    if (started_ || startInFlight_) { return true; }
    retry_.cancel();
    startInFlight_ = true;
    const int result =
        sd_bus_call_method_async(bus_.get(), nullptr, kService, kManagerPath, kManagerIface,
                                 "GetClient", &GeoClueBackend::onGetClient, this, "");
    if (result < 0) {
        startInFlight_ = false;
        std::fprintf(stderr,
                     "qypr: could not queue GeoClue client request (%d); fixed theme hours apply\n",
                     -result);
        retry_.schedule();
        return false;
    }
    return true;
#endif
}

void GeoClueBackend::failStart(const char* stage, sd_bus_message* reply) {
#ifndef TESTING
    startInFlight_ = false;
    clientPath_.clear();
    const bool retry = isRetryable(reply);
    std::fprintf(stderr, "qypr: GeoClue %s failed (%s); fixed theme hours apply%s\n", stage,
                 errorMessage(reply), retry ? "; retrying with backoff" : "");
    if (retry) {
        retry_.schedule();
    } else {
        retry_.cancel();
    }
#else
    (void)stage;
    (void)reply;
#endif
}

#ifndef TESTING
int GeoClueBackend::onGetClient(sd_bus_message* reply, void* userdata, sd_bus_error*) {
    auto* self = static_cast<GeoClueBackend*>(userdata);
    if (sd_bus_message_is_method_error(reply, nullptr) != 0) {
        self->failStart("client request", reply);
        return 0;
    }
    const char* path = nullptr;
    if (sd_bus_message_read(reply, "o", &path) < 0 || path == nullptr || *path == '\0') {
        self->startInFlight_ = false;
        self->retry_.schedule();
        std::fprintf(stderr, "qypr: GeoClue returned no client; fixed theme hours apply\n");
        return 0;
    }
    self->clientPath_ = path;
    const int result = sd_bus_call_method_async(
        self->bus_.get(), nullptr, kService, self->clientPath_.c_str(), kPropertiesIface, "Set",
        &GeoClueBackend::onDesktopIdSet, self, "ssv", kClientIface, "DesktopId", "s", kDesktopId);
    if (result < 0) {
        self->startInFlight_ = false;
        self->clientPath_.clear();
        self->retry_.schedule();
        std::fprintf(stderr,
                     "qypr: could not queue GeoClue identity (%d); fixed theme hours apply\n",
                     -result);
    }
    return 0;
}

int GeoClueBackend::onDesktopIdSet(sd_bus_message* reply, void* userdata, sd_bus_error*) {
    auto* self = static_cast<GeoClueBackend*>(userdata);
    if (sd_bus_message_is_method_error(reply, nullptr) != 0) {
        self->failStart("authorization", reply);
        return 0;
    }

    sd_bus* bus = self->bus_.get();
    const int accuracy = sd_bus_call_method_async(
        bus, nullptr, kService, self->clientPath_.c_str(), kPropertiesIface, "Set", nullptr,
        nullptr, "ssv", kClientIface, "RequestedAccuracyLevel", "u", kAccuracyCity);
    const int distance = sd_bus_call_method_async(
        bus, nullptr, kService, self->clientPath_.c_str(), kPropertiesIface, "Set", nullptr,
        nullptr, "ssv", kClientIface, "DistanceThreshold", "u", kDistanceThresholdM);
    const int time = sd_bus_call_method_async(
        bus, nullptr, kService, self->clientPath_.c_str(), kPropertiesIface, "Set", nullptr,
        nullptr, "ssv", kClientIface, "TimeThreshold", "u", kTimeThresholdSec);
    if (accuracy < 0 || distance < 0 || time < 0) {
        self->startInFlight_ = false;
        self->retry_.schedule();
        std::fprintf(stderr,
                     "qypr: could not queue GeoClue accuracy settings; fixed theme hours apply\n");
        return 0;
    }

    if (!self->slot_) {
        const std::string rule = std::string("type='signal',sender='") + kService +
                                 "',interface='" + kClientIface +
                                 "',member='LocationUpdated',path='" + self->clientPath_ + "'";
        self->slot_ = self->bus_.addMatch(rule.c_str(), &GeoClueBackend::onLocationUpdated, self);
    }
    if (!self->slot_) {
        self->startInFlight_ = false;
        self->retry_.schedule();
        return 0;
    }

    const int result =
        sd_bus_call_method_async(bus, nullptr, kService, self->clientPath_.c_str(), kClientIface,
                                 "Start", &GeoClueBackend::onStartClient, self, "");
    if (result < 0) {
        self->startInFlight_ = false;
        self->retry_.schedule();
        std::fprintf(stderr, "qypr: could not queue GeoClue start (%d); fixed theme hours apply\n",
                     -result);
    }
    return 0;
}

int GeoClueBackend::onStartClient(sd_bus_message* reply, void* userdata, sd_bus_error*) {
    auto* self = static_cast<GeoClueBackend*>(userdata);
    if (sd_bus_message_is_method_error(reply, nullptr) != 0) {
        self->failStart("start", reply);
        return 0;
    }
    self->startInFlight_ = false;
    self->started_ = true;
    self->retry_.reset();
    self->refreshFix();
    return 0;
}

int GeoClueBackend::onLocationUpdated(sd_bus_message*, void* userdata, sd_bus_error*) {
    static_cast<GeoClueBackend*>(userdata)->refreshFix();
    return 0;
}

void GeoClueBackend::refreshFix() {  // NOLINT(misc-no-recursion) async D-Bus reply cycle
    if (!started_ || clientPath_.empty() || !bus_.available()) { return; }
    if (fixFetchInFlight_) {
        fixRefreshPending_ = true;
        return;
    }
    fixFetchInFlight_ = true;
    const int result = sd_bus_call_method_async(
        bus_.get(), nullptr, kService, clientPath_.c_str(), kPropertiesIface, "Get",
        &GeoClueBackend::onLocationPath, this, "ss", kClientIface, "Location");
    if (result < 0) {
        finishFixFetch(false);
        std::fprintf(stderr, "qypr: could not queue GeoClue location read (%d)\n", -result);
    }
}

int GeoClueBackend::onLocationPath(sd_bus_message* reply, void* userdata, sd_bus_error*) {
    auto* self = static_cast<GeoClueBackend*>(userdata);
    if (sd_bus_message_is_method_error(reply, nullptr) != 0) {
        self->finishFixFetch(false);
        return 0;
    }
    if (sd_bus_message_enter_container(reply, 'v', "o") <= 0) {
        self->finishFixFetch(false);
        return 0;
    }
    const char* locationPath = nullptr;
    // NOLINTNEXTLINE(bugprone-multi-level-implicit-pointer-conversion) // sd-bus API
    const int readResult = sd_bus_message_read_basic(reply, 'o', &locationPath);
    sd_bus_message_exit_container(reply);
    if (readResult < 0 || locationPath == nullptr) {
        self->finishFixFetch(false);
        return 0;
    }
    if (std::strcmp(locationPath, "/") == 0) {
        self->finishFixFetch(true);  // no fix yet; wait for LocationUpdated
        return 0;
    }

    const int result = sd_bus_call_method_async(
        self->bus_.get(), nullptr, kService, locationPath, kPropertiesIface, "GetAll",
        &GeoClueBackend::onLocationProperties, self, "s", kLocationIface);
    if (result < 0) {
        self->finishFixFetch(false);
        std::fprintf(stderr, "qypr: could not queue GeoClue fix read (%d)\n", -result);
    }
    return 0;
}

int GeoClueBackend::onLocationProperties(sd_bus_message* reply, void* userdata, sd_bus_error*) {
    auto* self = static_cast<GeoClueBackend*>(userdata);
    if (sd_bus_message_is_method_error(reply, nullptr) != 0) {
        self->finishFixFetch(false);
        return 0;
    }
    GeoFix next;
    if (!readLocation(reply, &next)) {
        self->finishFixFetch(false);
        return 0;
    }
    if (self->fix_ != next) {
        self->fix_ = next;
        if (self->onChange_) { self->onChange_(); }
    }
    self->finishFixFetch(true);
    return 0;
}

void GeoClueBackend::finishFixFetch(bool success) {  // NOLINT(misc-no-recursion) async reply cycle
    fixFetchInFlight_ = false;
    if (success) {
        retry_.reset();
    } else {
        retry_.schedule();
    }
    if (fixRefreshPending_) {
        fixRefreshPending_ = false;
        refreshFix();
    }
}
#else
void GeoClueBackend::refreshFix() {}
#endif

}  // namespace qypr
