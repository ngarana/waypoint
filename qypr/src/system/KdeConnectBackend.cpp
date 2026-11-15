// KdeConnectBackend.cpp - kdeconnectd client: devices, plugins, actions.
#include "system/KdeConnectBackend.hpp"

#include <systemd/sd-bus.h>

#include <algorithm>
#include <cstdlib>
#include <string_view>
#include <utility>

#include "core/EventLoop.hpp"
#include "system/DbusRead.hpp"
#include "system/SystemBus.hpp"

namespace qypr {

namespace {

constexpr const char* kSvc = "org.kde.kdeconnect";
constexpr const char* kDaemonPath = "/modules/kdeconnect";
constexpr const char* kDaemonIface = "org.kde.kdeconnect.daemon";
constexpr const char* kDeviceIface = "org.kde.kdeconnect.device";
constexpr const char* kBatteryIface = "org.kde.kdeconnect.device.battery";
constexpr const char* kConnectivityIface = "org.kde.kdeconnect.device.connectivity_report";
constexpr const char* kNotificationsIface = "org.kde.kdeconnect.device.notifications";
constexpr const char* kNotificationIface = "org.kde.kdeconnect.device.notifications.notification";
constexpr const char* kProps = "org.freedesktop.DBus.Properties";
constexpr const char* kPortalSvc = "org.freedesktop.portal.Desktop";
constexpr const char* kPortalPath = "/org/freedesktop/portal/desktop";

std::string devicePath(const std::string& id) {
    return std::string(kDaemonPath) + "/devices/" + id;
}

// Signals that change what the bar shows. Media remote, SMS and the like
// signal often and change nothing here.
bool relevant(std::string_view iface) {
    return iface == kDaemonIface || iface == kDeviceIface || iface == kBatteryIface ||
           iface == kConnectivityIface || iface == kNotificationsIface ||
           iface == kNotificationIface;
}

long numericId(const std::string& id) {
    char* end = nullptr;
    const long v = std::strtol(id.c_str(), &end, 10);
    return end != nullptr && *end == '\0' ? v : -1;
}

struct ReplyThunk {
    std::function<void(sd_bus_message*)> reply;
};

}  // namespace

bool PhoneDevice::has(const std::string& plugin) const {
    return std::ranges::find(plugins, plugin) != plugins.end();
}

const PhoneDevice* PhoneLinkSnapshot::find(const std::string& id) const {
    const auto it = std::ranges::find(devices, id, &PhoneDevice::id);
    return it == devices.end() ? nullptr : &*it;
}

std::vector<PhoneDevice> reducePhones(std::vector<PhoneDevice> devices) {
    std::erase_if(devices,
                  [](const PhoneDevice& d) { return !d.paired && !d.pairRequestedByPeer; });
    std::erase_if(devices, [&](const PhoneDevice& d) {
        if (d.reachable || !d.paired) { return false; }
        return std::ranges::any_of(devices, [&](const PhoneDevice& other) {
            return other.reachable && other.paired && other.id != d.id && other.name == d.name &&
                   other.type == d.type;
        });
    });
    std::ranges::stable_sort(devices, [](const PhoneDevice& a, const PhoneDevice& b) {
        if (a.reachable != b.reachable) { return a.reachable; }
        return a.name < b.name;
    });
    return devices;
}

KdeConnectBackend::KdeConnectBackend(EventLoop& loop, SystemBus& sessionBus)
    : loop_(loop),
      bus_(sessionBus) {}

KdeConnectBackend::~KdeConnectBackend() {
    if (settleTimer_ >= 0) { loop_.removeTimer(settleTimer_); }
    sd_bus_slot_unref(signalSlot_);
    sd_bus_slot_unref(ownerSlot_);
    sd_bus_slot_unref(portalSlot_);
}

bool KdeConnectBackend::start() {
    if (!bus_.available()) { return false; }
    if (started_) {
        refresh();
        return true;
    }
    started_ = true;
    signalSlot_ = bus_.addMatch(
        "type='signal',sender='org.kde.kdeconnect',path_namespace='/modules/kdeconnect'",
        &KdeConnectBackend::onSignal, this);
    ownerSlot_ = bus_.addMatch(
        "type='signal',sender='org.freedesktop.DBus',interface='org.freedesktop.DBus',"
        "member='NameOwnerChanged',arg0='org.kde.kdeconnect'",
        &KdeConnectBackend::onNameOwnerChanged, this);
    refresh();
    return true;
}

// ── Calls ───────────────────────────────────────────────────────────────────

template <typename... Args>
bool KdeConnectBackend::callAsync(const char* dest, const std::string& path, const char* iface,
                                  const char* member, Reply reply, const char* types,
                                  Args... args) {
    sd_bus* bus = bus_.get();
    if (bus == nullptr) { return false; }
    // NOLINTNEXTLINE(cppcoreguidelines-owning-memory) // sd-bus userdata; freed on reply
    auto* thunk = new ReplyThunk{std::move(reply)};
    const int r = sd_bus_call_method_async(bus, nullptr, dest, path.c_str(), iface, member,
                                           &KdeConnectBackend::onReplyThunk, thunk, types, args...);
    if (r < 0) {
        // NOLINTNEXTLINE(cppcoreguidelines-owning-memory) // never handed to sd-bus
        delete thunk;
        return false;
    }
    return true;
}

bool KdeConnectBackend::getAll(const std::string& path, const char* target, Reply reply) {
    return callAsync(kSvc, path, kProps, "GetAll", std::move(reply), "s", target);
}

int KdeConnectBackend::onReplyThunk(sd_bus_message* m, void* userdata, sd_bus_error*) {
    // NOLINTNEXTLINE(cppcoreguidelines-owning-memory) // allocated in callAsync()
    auto* thunk = static_cast<ReplyThunk*>(userdata);
    if (thunk->reply) { thunk->reply(m); }
    // NOLINTNEXTLINE(cppcoreguidelines-owning-memory) // allocated in callAsync()
    delete thunk;
    return 0;
}

void KdeConnectBackend::act(const std::string& path, const char* iface, const char* member,
                            const std::string& error) {
    const bool sent = callAsync(
        kSvc, path, iface, member,
        [this, error](sd_bus_message* reply) {
            if (sd_bus_message_is_method_error(reply, nullptr) != 0) { setError(error); }
        },
        "");
    if (!sent) { setError(error); }
}

void KdeConnectBackend::ring(const std::string& device) {
    act(devicePath(device) + "/findmyphone", "org.kde.kdeconnect.device.findmyphone", "ring",
        "Couldn't ring the phone");
}

void KdeConnectBackend::ping(const std::string& device) {
    act(devicePath(device) + "/ping", "org.kde.kdeconnect.device.ping", "sendPing",
        "Couldn't ping the phone");
}

void KdeConnectBackend::sendClipboard(const std::string& device) {
    act(devicePath(device) + "/clipboard", "org.kde.kdeconnect.device.clipboard", "sendClipboard",
        "Couldn't send the clipboard");
}

void KdeConnectBackend::browse(const std::string& device) {
    const std::string error = "Couldn't open the phone's files (is sshfs installed?)";
    // startBrowsing mounts the phone over SFTP and opens the file manager on
    // it; false when the mount failed.
    const bool sent = callAsync(
        kSvc, devicePath(device) + "/sftp", "org.kde.kdeconnect.device.sftp", "startBrowsing",
        [this, error](sd_bus_message* reply) {
            int ok = 0;
            if (sd_bus_message_is_method_error(reply, nullptr) != 0 ||
                sd_bus_message_read(reply, "b", &ok) < 0 || ok == 0) {
                setError(error);
            }
        },
        "");
    if (!sent) { setError(error); }
}

void KdeConnectBackend::dismissNotification(const std::string& device,
                                            const std::string& notification) {
    act(devicePath(device) + "/notifications/" + notification, kNotificationIface, "dismiss",
        "Couldn't dismiss the notification");
}

void KdeConnectBackend::acceptPairing(const std::string& device) {
    act(devicePath(device), kDeviceIface, "acceptPairing", "Couldn't accept the pairing");
}

void KdeConnectBackend::rejectPairing(const std::string& device) {
    act(devicePath(device), kDeviceIface, "cancelPairing", "Couldn't reject the pairing");
}

void KdeConnectBackend::shareFiles(const std::string& device) {
    sd_bus* bus = bus_.get();
    const PhoneDevice* dev = snap_.find(device);
    if (bus == nullptr || dev == nullptr) { return; }
    // The portal answers on a Request object whose path is derived from our
    // unique name and a token we choose, so the match is in place before the
    // call (no race with a fast reply).
    const char* unique = nullptr;
    if (sd_bus_get_unique_name(bus, &unique) < 0 || unique == nullptr) {
        setError("No file chooser available");
        return;
    }
    std::string sender = unique + 1;  // drop ':'
    std::ranges::replace(sender, '.', '_');
    static unsigned counter = 0;
    const std::string token = "qypr_share_" + std::to_string(++counter);
    const std::string handle = std::string(kPortalPath) + "/request/" + sender + "/" + token;
    sd_bus_slot_unref(portalSlot_);
    portalSlot_ = bus_.addMatch(("type='signal',sender='org.freedesktop.portal.Desktop',"
                                 "interface='org.freedesktop.portal.Request',member='Response',"
                                 "path='" +
                                 handle + "'")
                                    .c_str(),
                                &KdeConnectBackend::onPortalResponse, this);
    portalDevice_ = device;
    const std::string title = "Send to " + dev->name;
    const bool sent = callAsync(
        kPortalSvc, kPortalPath, "org.freedesktop.portal.FileChooser", "OpenFile",
        [this](sd_bus_message* reply) {
            if (sd_bus_message_is_method_error(reply, nullptr) != 0) {
                setError("No file chooser available");
                sd_bus_slot_unref(portalSlot_);
                portalSlot_ = nullptr;
            }
        },
        "ssa{sv}", "", title.c_str(), 2, "handle_token", "s", token.c_str(), "multiple", "b", 1);
    if (!sent) { setError("No file chooser available"); }
}

int KdeConnectBackend::onPortalResponse(sd_bus_message* m, void* userdata, sd_bus_error*) {
    auto* self = static_cast<KdeConnectBackend*>(userdata);
    uint32_t response = 1;
    std::vector<std::string> uris;
    if (sd_bus_message_read(m, "u", &response) >= 0 && response == 0) {
        dbus::forEachProperty(m, [&](std::string_view key) {
            if (key == "uris") {
                dbus::readStrings(m, &uris);
            } else {
                sd_bus_message_skip(m, "v");
            }
        });
    }
    const std::string device = std::exchange(self->portalDevice_, {});
    // Done with this request; dropping the slot from inside its own callback
    // is allowed by sd-bus.
    sd_bus_slot_unref(self->portalSlot_);
    self->portalSlot_ = nullptr;
    for (const auto& uri : uris) {
        self->callAsync(
            kSvc, devicePath(device) + "/share", "org.kde.kdeconnect.device.share", "shareUrl",
            [self](sd_bus_message* reply) {
                if (sd_bus_message_is_method_error(reply, nullptr) != 0) {
                    self->setError("Couldn't send the file");
                }
            },
            "s", uri.c_str());
    }
    return 0;
}

void KdeConnectBackend::setError(const std::string& error) {
    if (snap_.error == error) { return; }
    snap_.error = error;
    if (onChange_) { onChange_(); }
}

void KdeConnectBackend::clearError() {
    setError({});
}

// ── Reading ─────────────────────────────────────────────────────────────────

int KdeConnectBackend::onSignal(sd_bus_message* m, void* userdata, sd_bus_error*) {
    const char* iface = sd_bus_message_get_interface(m);
    if (iface != nullptr && relevant(iface)) {
        static_cast<KdeConnectBackend*>(userdata)->scheduleRefresh();
    }
    return 0;
}

int KdeConnectBackend::onNameOwnerChanged(sd_bus_message* m, void* userdata, sd_bus_error*) {
    auto* self = static_cast<KdeConnectBackend*>(userdata);
    const char* name = nullptr;
    const char* oldOwner = nullptr;
    const char* newOwner = nullptr;
    if (sd_bus_message_read(m, "sss", &name, &oldOwner, &newOwner) < 0) { return 0; }
    if (newOwner == nullptr || *newOwner == '\0') {
        self->publish({});  // daemon gone: no devices, no notifications
        return 0;
    }
    self->scheduleRefresh();
    return 0;
}

void KdeConnectBackend::scheduleRefresh() {
    if (settleTimer_ >= 0) { return; }
    settleTimer_ = loop_.addTimer(kSettleMs, /*repeat=*/false, [this] {
        settleTimer_ = -1;  // one-shot timers are removed before the callback
        refresh();
    });
}

// NOLINTNEXTLINE(misc-no-recursion) Async D-Bus replies resume on the event loop.
void KdeConnectBackend::refresh() {
    if (bus_.get() == nullptr) { return; }
    if (inFlight_) {
        pending_ = true;
        return;
    }
    inFlight_ = true;
    building_.clear();
    outstanding_ = 1;
    const bool sent = callAsync(
        kSvc, kDaemonPath, kDaemonIface, "devices",
        [this](sd_bus_message* reply) {  // NOLINT(misc-no-recursion)
            if (sd_bus_message_is_method_error(reply, nullptr) != 0) {
                // Not running (or not answering): nothing to show.
                building_.clear();
                outstanding_ = 0;
                inFlight_ = false;
                publish({});
                return;
            }
            std::vector<std::string> ids;
            if (sd_bus_message_enter_container(reply, 'a', "s") > 0) {
                const char* id = nullptr;
                while (sd_bus_message_read(reply, "s", &id) > 0) {
                    if (id != nullptr) { ids.emplace_back(id); }
                }
                sd_bus_message_exit_container(reply);
            }
            for (const auto& id : ids) { readDevice(id); }
            callDone();
        },
        "");
    if (!sent) {
        inFlight_ = false;
        outstanding_ = 0;
    }
}

void KdeConnectBackend::readDevice(const std::string& id) {
    ++outstanding_;
    const bool sent = getAll(devicePath(id), kDeviceIface,
                             [this, id](sd_bus_message* reply) {  // NOLINT(misc-no-recursion)
                                 if (sd_bus_message_is_method_error(reply, nullptr) == 0) {
                                     PhoneDevice d;
                                     d.id = id;
                                     int pairState = 0;
                                     dbus::forEachProperty(reply, [&](std::string_view key) {
                                         if (key == "name") {
                                             dbus::readString(reply, 's', &d.name);
                                         } else if (key == "type") {
                                             dbus::readString(reply, 's', &d.type);
                                         } else if (key == "isReachable") {
                                             dbus::readBool(reply, &d.reachable);
                                         } else if (key == "isPaired") {
                                             dbus::readBool(reply, &d.paired);
                                         } else if (key == "isPairRequestedByPeer") {
                                             dbus::readBool(reply, &d.pairRequestedByPeer);
                                         } else if (key == "pairState") {
                                             dbus::readInt(reply, &pairState);
                                         } else if (key == "supportedPlugins") {
                                             dbus::readStrings(reply, &d.plugins);
                                         } else {
                                             sd_bus_message_skip(reply, "v");
                                         }
                                     });
                                     if (d.paired || d.pairRequestedByPeer) {
                                         building_.push_back(std::move(d));
                                         const auto& added = building_.back();
                                         if (added.reachable && added.paired) {
                                             readPlugins(building_.size() - 1);
                                         }
                                     }
                                 }
                                 callDone();
                             });
    if (!sent) { --outstanding_; }
}

void KdeConnectBackend::readPlugins(size_t index) {
    const std::string id = building_[index].id;
    const std::string path = devicePath(id);
    if (building_[index].has("kdeconnect_battery")) {
        ++outstanding_;
        const bool sent =
            getAll(path + "/battery", kBatteryIface,
                   [this, index](sd_bus_message* reply) {  // NOLINT(misc-no-recursion)
                       if (sd_bus_message_is_method_error(reply, nullptr) == 0) {
                           PhoneDevice& d = building_[index];
                           bool hasBattery = true;
                           int charge = -1;
                           dbus::forEachProperty(reply, [&](std::string_view key) {
                               if (key == "charge") {
                                   dbus::readInt(reply, &charge);
                               } else if (key == "isCharging") {
                                   dbus::readBool(reply, &d.charging);
                               } else if (key == "hasBattery") {
                                   dbus::readBool(reply, &hasBattery);
                               } else {
                                   sd_bus_message_skip(reply, "v");
                               }
                           });
                           d.charge = hasBattery ? charge : -1;
                       }
                       callDone();
                   });
        if (!sent) { --outstanding_; }
    }
    if (building_[index].has("kdeconnect_connectivity_report")) {
        ++outstanding_;
        const bool sent =
            getAll(path + "/connectivity_report", kConnectivityIface,
                   [this, index](sd_bus_message* reply) {  // NOLINT(misc-no-recursion)
                       if (sd_bus_message_is_method_error(reply, nullptr) == 0) {
                           PhoneDevice& d = building_[index];
                           dbus::forEachProperty(reply, [&](std::string_view key) {
                               if (key == "cellularNetworkType") {
                                   dbus::readString(reply, 's', &d.network);
                               } else if (key == "cellularNetworkStrength") {
                                   dbus::readInt(reply, &d.signal);
                               } else {
                                   sd_bus_message_skip(reply, "v");
                               }
                           });
                           if (d.network == "Unknown") { d.network.clear(); }
                       }
                       callDone();
                   });
        if (!sent) { --outstanding_; }
    }
    if (building_[index].has("kdeconnect_notifications")) {
        ++outstanding_;
        const bool sent = callAsync(
            kSvc, path + "/notifications", kNotificationsIface, "activeNotifications",
            [this, index](sd_bus_message* reply) {  // NOLINT(misc-no-recursion)
                if (sd_bus_message_is_method_error(reply, nullptr) == 0 &&
                    sd_bus_message_enter_container(reply, 'a', "s") > 0) {
                    const char* nid = nullptr;
                    std::vector<std::string> ids;
                    while (sd_bus_message_read(reply, "s", &nid) > 0) {
                        if (nid != nullptr) { ids.emplace_back(nid); }
                    }
                    sd_bus_message_exit_container(reply);
                    for (const auto& n : ids) { readNotification(index, n); }
                }
                callDone();
            },
            "");
        if (!sent) { --outstanding_; }
    }
}

void KdeConnectBackend::readNotification(size_t index, const std::string& notificationId) {
    ++outstanding_;
    const std::string path = devicePath(building_[index].id) + "/notifications/" + notificationId;
    const bool sent =
        getAll(path, kNotificationIface,
               [this, index, notificationId](sd_bus_message* reply) {  // NOLINT(misc-no-recursion)
                   if (sd_bus_message_is_method_error(reply, nullptr) == 0) {
                       PhoneNotification n;
                       n.id = notificationId;
                       bool silent = false;
                       dbus::forEachProperty(reply, [&](std::string_view key) {
                           if (key == "appName") {
                               dbus::readString(reply, 's', &n.app);
                           } else if (key == "title") {
                               dbus::readString(reply, 's', &n.title);
                           } else if (key == "text") {
                               dbus::readString(reply, 's', &n.text);
                           } else if (key == "dismissable") {
                               dbus::readBool(reply, &n.dismissable);
                           } else if (key == "silent") {
                               dbus::readBool(reply, &silent);
                           } else {
                               sd_bus_message_skip(reply, "v");
                           }
                       });
                       // Silent ones are ongoing status (media, navigation), not news.
                       if (!silent) { building_[index].notifications.push_back(std::move(n)); }
                   }
                   callDone();
               });
    if (!sent) { --outstanding_; }
}

// NOLINTNEXTLINE(misc-no-recursion) Async D-Bus replies resume on the event loop.
void KdeConnectBackend::callDone() {
    if (--outstanding_ > 0) { return; }
    for (auto& d : building_) {
        // Newest first: publicIds count up on the phone.
        std::ranges::stable_sort(d.notifications, [](const auto& a, const auto& b) {
            return numericId(a.id) > numericId(b.id);
        });
    }
    PhoneLinkSnapshot next;
    next.available = true;
    next.devices = reducePhones(std::move(building_));
    building_.clear();
    next.error = snap_.error;
    inFlight_ = false;
    publish(std::move(next));
    if (pending_) {
        pending_ = false;
        refresh();
    }
}

void KdeConnectBackend::publish(PhoneLinkSnapshot next) {
    if (next == snap_) { return; }
    snap_ = std::move(next);
    if (onChange_) { onChange_(); }
}

}  // namespace qypr
