// BluetoothAgent.cpp - org.bluez.Agent1 implementation (deferred replies).
#include "system/BluetoothAgent.hpp"

#include <systemd/sd-bus.h>

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <utility>

#include "system/SystemBus.hpp"

namespace qypr {

namespace {
constexpr const char* kBlueZ = "org.bluez";
constexpr const char* kBlueZPath = "/org/bluez";
constexpr const char* kAgentManagerIface = "org.bluez.AgentManager1";
constexpr const char* kAgentIface = "org.bluez.Agent1";
constexpr const char* kAgentPath = "/org/qypr/bluez/agent";

// Both a display and (through the picker's inline entry) a keyboard, so BlueZ
// may pick any pairing method. Claiming less would silently downgrade some
// pairings to methods that are weaker or that simply fail.
constexpr const char* kCapability = "KeyboardDisplay";

constexpr const char* kErrRejected = "org.bluez.Error.Rejected";
constexpr const char* kErrCanceled = "org.bluez.Error.Canceled";

// BlueZ passkeys are always six digits, zero-padded for display.
std::string formatPasskey(uint32_t passkey) {
    const std::string digits = std::to_string(passkey % 1000000);
    return std::string(6 - std::min<size_t>(6, digits.size()), '0') + digits;
}

// Read the leading object path argument every Agent1 method starts with.
std::string readDevicePath(sd_bus_message* m) {
    const char* path = nullptr;
    if (sd_bus_message_read(m, "o", &path) < 0 || path == nullptr) { return {}; }
    return path;
}
}  // namespace

BluetoothAgent::BluetoothAgent(SystemBus& bus) : bus_(bus) {}

BluetoothAgent::~BluetoothAgent() {
    // Drop the observer *before* replying. rejectPending() clears the prompt,
    // which would otherwise notify an owner that is already tearing down — and
    // the owner's snapshot may be gone by then, so the callback would copy a
    // destroyed object and double-free its strings on the way out.
    onRequest_ = nullptr;
    nameResolver_ = nullptr;
    // Never leave BlueZ waiting on a reply that can no longer come.
    rejectPending(kErrCanceled, "qypr is shutting down");
    if (registered_ && bus_.available()) {
        sd_bus_call_method_async(bus_.get(), nullptr, kBlueZ, kBlueZPath, kAgentManagerIface,
                                 "UnregisterAgent", nullptr, nullptr, "o", kAgentPath);
    }
    sd_bus_slot_unref(vtableSlot_);
}

// ─── Vtable ─────────────────────────────────────────────────────────────────

const sd_bus_vtable BluetoothAgent::kVtable[] = {
    SD_BUS_VTABLE_START(0),
    SD_BUS_METHOD("Release", "", "", &BluetoothAgent::onRelease, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD("RequestPinCode", "o", "s", &BluetoothAgent::onRequestPinCode,
                  SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD("DisplayPinCode", "os", "", &BluetoothAgent::onDisplayPinCode,
                  SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD("RequestPasskey", "o", "u", &BluetoothAgent::onRequestPasskey,
                  SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD("DisplayPasskey", "ouq", "", &BluetoothAgent::onDisplayPasskey,
                  SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD("RequestConfirmation", "ou", "", &BluetoothAgent::onRequestConfirmation,
                  SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD("RequestAuthorization", "o", "", &BluetoothAgent::onRequestAuthorization,
                  SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD("AuthorizeService", "os", "", &BluetoothAgent::onAuthorizeService,
                  SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD("Cancel", "", "", &BluetoothAgent::onCancel, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_VTABLE_END,
};

void BluetoothAgent::start() {
    if (!bus_.available()) { return; }

    if (!exported_) {
        const int r = sd_bus_add_object_vtable(bus_.get(), &vtableSlot_, kAgentPath, kAgentIface,
                                               kVtable, this);
        if (r < 0) {
            std::fprintf(stderr, "qypr: failed to export Bluetooth agent: %d\n", -r);
            return;
        }
        exported_ = true;
    }

    // Register (again) with BlueZ. A daemon restart forgets every agent, so
    // this runs on each (re)appearance; a duplicate registration comes back as
    // AlreadyExists, which is harmless.
    registered_ = true;
    sd_bus_call_method_async(bus_.get(), nullptr, kBlueZ, kBlueZPath, kAgentManagerIface,
                             "RegisterAgent", &BluetoothAgent::onRegisterReply, this, "os",
                             kAgentPath, kCapability);
}

// Registration failing means every non-"Just Works" pairing will fail later
// with an opaque BlueZ error, so say so once, here, where the cause is known.
int BluetoothAgent::onRegisterReply(sd_bus_message* reply, void* userdata,
                                    sd_bus_error* /*unused*/) {
    auto* self = static_cast<BluetoothAgent*>(userdata);
    if (sd_bus_message_is_method_error(reply, nullptr) == 0) { return 0; }
    const sd_bus_error* e = sd_bus_message_get_error(reply);
    const char* name = (e != nullptr && e->name != nullptr) ? e->name : "unknown";
    // Re-registering an already-registered agent is expected on reconnect.
    if (std::strcmp(name, "org.bluez.Error.AlreadyExists") == 0) { return 0; }
    self->registered_ = false;
    std::fprintf(stderr, "qypr: BlueZ rejected the pairing agent (%s); pairing will be limited\n",
                 name);
    return 0;
}

void BluetoothAgent::onBluezLost() {
    registered_ = false;
    // Any prompt on screen belongs to a pairing that died with the daemon.
    rejectPending(kErrCanceled, "BlueZ went away");
}

// ─── Pending-call bookkeeping ───────────────────────────────────────────────

int BluetoothAgent::defer(sd_bus_message* m, BtPairRequest&& req) {
    if (pending_ != nullptr) {
        // One prompt at a time: the picker can only show one, and answering the
        // wrong call would complete the wrong pairing.
        return -EBUSY;
    }
    pending_ = sd_bus_message_ref(m);
    req_ = std::move(req);
    if (onRequest_) { onRequest_(); }
    return 1;  // sd-bus: this callback takes responsibility for replying
}

void BluetoothAgent::rejectPending(const char* errName, const char* message) {
    if (pending_ == nullptr) {
        clearRequest();
        return;
    }
    sd_bus_message* m = pending_;
    pending_ = nullptr;  // clear first: replying must not re-enter this path
    sd_bus_error err = SD_BUS_ERROR_NULL;
    sd_bus_error_set(&err, errName, message);
    sd_bus_reply_method_error(m, &err);
    sd_bus_error_free(&err);
    sd_bus_message_unref(m);
    clearRequest();
}

void BluetoothAgent::clearRequest() {
    if (!req_.active()) { return; }
    req_ = {};
    if (onRequest_) { onRequest_(); }
}

std::string BluetoothAgent::nameFor(const std::string& path) const {
    if (nameResolver_) {
        std::string name = nameResolver_(path);
        if (!name.empty()) { return name; }
    }
    return "device";
}

// ─── UI answers ─────────────────────────────────────────────────────────────

void BluetoothAgent::respond(bool accept) {
    if (pending_ == nullptr) {
        clearRequest();
        return;
    }
    if (!accept) {
        rejectPending(kErrRejected, "Rejected by user");
        return;
    }
    // Confirm / Authorize / Service all reply with an empty return; consenting
    // *is* the answer.
    sd_bus_message* m = pending_;
    pending_ = nullptr;
    sd_bus_reply_method_return(m, "");
    sd_bus_message_unref(m);
    clearRequest();
}

void BluetoothAgent::respondInput(const std::string& text) {
    if (pending_ == nullptr) {
        clearRequest();
        return;
    }
    if (text.empty()) {
        rejectPending(kErrRejected, "Rejected by user");
        return;
    }
    sd_bus_message* m = pending_;
    const bool numeric = req_.numericEntry;
    pending_ = nullptr;
    if (numeric) {
        // RequestPasskey returns a plain number, not the padded display form.
        const auto passkey = static_cast<uint32_t>(std::strtoul(text.c_str(), nullptr, 10));
        sd_bus_reply_method_return(m, "u", passkey);
    } else {
        sd_bus_reply_method_return(m, "s", text.c_str());
    }
    sd_bus_message_unref(m);
    clearRequest();
}

// ─── Agent1 methods ─────────────────────────────────────────────────────────

int BluetoothAgent::onRelease(sd_bus_message* m, void* userdata, sd_bus_error* /*unused*/) {
    auto* self = static_cast<BluetoothAgent*>(userdata);
    // BlueZ is done with this agent (unregistered, or the daemon is stopping).
    self->registered_ = false;
    self->rejectPending(kErrCanceled, "Agent released");
    sd_bus_reply_method_return(m, "");
    return 1;
}

// Numeric comparison: both ends display the same six digits and the user
// confirms they match. The usual flow for phones and modern headsets.
int BluetoothAgent::onRequestConfirmation(sd_bus_message* m, void* userdata,
                                          sd_bus_error* /*unused*/) {
    auto* self = static_cast<BluetoothAgent*>(userdata);
    const std::string path = readDevicePath(m);
    uint32_t passkey = 0;
    sd_bus_message_read(m, "u", &passkey);
    return self->defer(m, {.kind = BtPairRequest::Kind::Confirm,
                           .devicePath = path,
                           .deviceName = self->nameFor(path),
                           .passkey = formatPasskey(passkey)});
}

// "Just Works" pairing that still wants an explicit yes.
int BluetoothAgent::onRequestAuthorization(sd_bus_message* m, void* userdata,
                                           sd_bus_error* /*unused*/) {
    auto* self = static_cast<BluetoothAgent*>(userdata);
    const std::string path = readDevicePath(m);
    return self->defer(m, {.kind = BtPairRequest::Kind::Authorize,
                           .devicePath = path,
                           .deviceName = self->nameFor(path)});
}

// A paired-but-untrusted device asking for a profile. Prompted rather than
// auto-accepted: qypr marks devices it pairs as Trusted, so BlueZ never asks
// about those — anything reaching here was paired outside this picker.
int BluetoothAgent::onAuthorizeService(sd_bus_message* m, void* userdata,
                                       sd_bus_error* /*unused*/) {
    auto* self = static_cast<BluetoothAgent*>(userdata);
    const std::string path = readDevicePath(m);
    const char* uuid = nullptr;
    sd_bus_message_read(m, "s", &uuid);
    return self->defer(m, {.kind = BtPairRequest::Kind::Service,
                           .devicePath = path,
                           .deviceName = self->nameFor(path),
                           .service = uuid != nullptr ? uuid : ""});
}

// The user types the six digits shown on the remote device.
int BluetoothAgent::onRequestPasskey(sd_bus_message* m, void* userdata, sd_bus_error* /*unused*/) {
    auto* self = static_cast<BluetoothAgent*>(userdata);
    const std::string path = readDevicePath(m);
    return self->defer(m, {.kind = BtPairRequest::Kind::Entry,
                           .devicePath = path,
                           .deviceName = self->nameFor(path),
                           .numericEntry = true});
}

// Legacy PIN entry (older headsets, car kits): a string, not a number.
int BluetoothAgent::onRequestPinCode(sd_bus_message* m, void* userdata, sd_bus_error* /*unused*/) {
    auto* self = static_cast<BluetoothAgent*>(userdata);
    const std::string path = readDevicePath(m);
    return self->defer(m, {.kind = BtPairRequest::Kind::Entry,
                           .devicePath = path,
                           .deviceName = self->nameFor(path),
                           .numericEntry = false});
}

// We show a code for the user to type on the *remote* device. Nothing to
// answer, so this replies at once; BlueZ repeats the call as digits are
// entered, which is what advances the progress readout.
int BluetoothAgent::onDisplayPasskey(sd_bus_message* m, void* userdata, sd_bus_error* /*unused*/) {
    auto* self = static_cast<BluetoothAgent*>(userdata);
    const std::string path = readDevicePath(m);
    uint32_t passkey = 0;
    uint16_t entered = 0;
    sd_bus_message_read(m, "u", &passkey);
    sd_bus_message_read(m, "q", &entered);
    // A display prompt owns no pending call, so it may replace an earlier one.
    if (self->pending_ == nullptr) {
        self->req_ = {.kind = BtPairRequest::Kind::Display,
                      .devicePath = path,
                      .deviceName = self->nameFor(path),
                      .passkey = formatPasskey(passkey),
                      .entered = entered};
        if (self->onRequest_) { self->onRequest_(); }
    }
    sd_bus_reply_method_return(m, "");
    return 1;
}

int BluetoothAgent::onDisplayPinCode(sd_bus_message* m, void* userdata, sd_bus_error* /*unused*/) {
    auto* self = static_cast<BluetoothAgent*>(userdata);
    const std::string path = readDevicePath(m);
    const char* pin = nullptr;
    sd_bus_message_read(m, "s", &pin);
    if (self->pending_ == nullptr) {
        self->req_ = {.kind = BtPairRequest::Kind::Display,
                      .devicePath = path,
                      .deviceName = self->nameFor(path),
                      .passkey = pin != nullptr ? pin : ""};
        if (self->onRequest_) { self->onRequest_(); }
    }
    sd_bus_reply_method_return(m, "");
    return 1;
}

// BlueZ withdrawing the request (the remote gave up, or pairing timed out).
int BluetoothAgent::onCancel(sd_bus_message* m, void* userdata, sd_bus_error* /*unused*/) {
    auto* self = static_cast<BluetoothAgent*>(userdata);
    self->rejectPending(kErrCanceled, "Cancelled by BlueZ");
    sd_bus_reply_method_return(m, "");
    return 1;
}

}  // namespace qypr
