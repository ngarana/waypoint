// KdeConnectBackend.hpp - Paired phones over KDE Connect (session bus).
//
// Talks to the running kdeconnectd (org.kde.kdeconnect): which devices are
// paired and reachable, their battery and cellular signal, the notifications
// mirrored from the phone, and pairing requests. Every one of those changes
// is announced by a signal under /modules/kdeconnect, so the backend
// subscribes once and re-reads on change; a burst of signals (a phone
// reconnecting reloads all its plugins) is coalesced into one re-read.
//
// Phone notifications are the user's personal data: they live in memory for
// as long as the phone has them, are never written anywhere, and reach only
// the session bar (the module is session-sensitive).
//
// Actions are the daemon's own: ring, ping, send clipboard, share files,
// browse over SFTP, dismiss a notification, accept or reject a pairing.
#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

struct sd_bus_message;
struct sd_bus_slot;
#include <systemd/sd-bus.h>  // sd_bus_error is a typedef here, not a struct

namespace qypr {

class EventLoop;
class SystemBus;

struct PhoneNotification {
    std::string id;  // publicId, the object name under …/notifications/
    std::string app;
    std::string title;
    std::string text;
    bool dismissable = false;
    bool operator==(const PhoneNotification&) const = default;
};

struct PhoneDevice {
    std::string id;
    std::string name;
    std::string type;  // "phone", "tablet", "laptop", "desktop", "tv"
    bool reachable = false;
    bool paired = false;
    bool pairRequestedByPeer = false;
    std::vector<std::string> plugins;  // supportedPlugins ("kdeconnect_battery", …)
    // battery
    int charge = -1;  // percent, -1 unknown
    bool charging = false;
    // connectivity_report
    std::string network;  // "LTE", "5G", ""
    int signal = -1;      // 0..4, -1 unknown
    std::vector<PhoneNotification> notifications;

    [[nodiscard]] bool has(const std::string& plugin) const;
    bool operator==(const PhoneDevice&) const = default;
};

struct PhoneLinkSnapshot {
    bool available = false;            // kdeconnectd is on the bus
    std::vector<PhoneDevice> devices;  // see reducePhones()
    std::string error;                 // last failed action, until dismissed

    [[nodiscard]] const PhoneDevice* find(const std::string& id) const;
    bool operator==(const PhoneLinkSnapshot&) const = default;
};

// What the bar shows from the daemon's device list: paired devices and
// devices asking to pair; reachable before unreachable, then by name. A
// paired device that is out of reach while another paired device with the
// same name and type is connected is a stale record of the same phone (left
// behind by a reinstall or reset) and is dropped.
std::vector<PhoneDevice> reducePhones(std::vector<PhoneDevice> devices);

class KdeConnectBackend {
public:
    KdeConnectBackend(EventLoop& loop, SystemBus& sessionBus);
    ~KdeConnectBackend();

    KdeConnectBackend(const KdeConnectBackend&) = delete;
    KdeConnectBackend& operator=(const KdeConnectBackend&) = delete;
    KdeConnectBackend(KdeConnectBackend&&) = delete;
    KdeConnectBackend& operator=(KdeConnectBackend&&) = delete;

    bool start();
    [[nodiscard]] const PhoneLinkSnapshot& snapshot() const { return snap_; }
    void setOnChange(std::function<void()> cb) { onChange_ = std::move(cb); }

    void ring(const std::string& device);
    void ping(const std::string& device);
    void sendClipboard(const std::string& device);
    void browse(const std::string& device);
    // Pick files with the desktop's file chooser portal, then share them.
    void shareFiles(const std::string& device);
    void dismissNotification(const std::string& device, const std::string& notification);
    void acceptPairing(const std::string& device);
    void rejectPairing(const std::string& device);
    void clearError();

    // Coalescing window for signal bursts.
    static constexpr int64_t kSettleMs = 150;

private:
    using Reply = std::function<void(sd_bus_message* reply)>;
    template <typename... Args>
    bool callAsync(const char* dest, const std::string& path, const char* iface, const char* member,
                   Reply reply, const char* types, Args... args);
    static int onReplyThunk(sd_bus_message* m, void* userdata, sd_bus_error* err);
    // Properties.GetAll on `path` for interface `target`.
    bool getAll(const std::string& path, const char* target, Reply reply);
    // Fire-and-forget device-plugin call; a failure is reported as `error`.
    void act(const std::string& path, const char* iface, const char* member,
             const std::string& error);

    static int onSignal(sd_bus_message* m, void* userdata, sd_bus_error* err);
    static int onNameOwnerChanged(sd_bus_message* m, void* userdata, sd_bus_error* err);
    static int onPortalResponse(sd_bus_message* m, void* userdata, sd_bus_error* err);
    void scheduleRefresh();
    void refresh();
    void readDevice(const std::string& id);
    void readPlugins(size_t index);
    void readNotification(size_t index, const std::string& notificationId);
    void callDone();
    void publish(PhoneLinkSnapshot next);
    void setError(const std::string& error);

    EventLoop& loop_;
    SystemBus& bus_;
    sd_bus_slot* signalSlot_ = nullptr;
    sd_bus_slot* ownerSlot_ = nullptr;
    sd_bus_slot* portalSlot_ = nullptr;
    std::string portalDevice_;  // device the open file chooser is sharing to
    int settleTimer_ = -1;
    bool started_ = false;
    bool inFlight_ = false;
    bool pending_ = false;
    int outstanding_ = 0;
    std::vector<PhoneDevice> building_;
    PhoneLinkSnapshot snap_;
    std::function<void()> onChange_;
};

}  // namespace qypr
