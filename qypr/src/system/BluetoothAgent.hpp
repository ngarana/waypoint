// BluetoothAgent.hpp - org.bluez.Agent1 pairing agent exported on the system bus.
//
// Pairing anything beyond "Just Works" gear needs an agent: BlueZ calls back
// into *us* to have the user confirm a passkey, type a PIN, or authorize a
// service, and will not complete the pairing until we answer. Without one,
// Device1.Pair() fails outright on phones, keyboards and most modern headsets.
//
// Answering means replying to a live D-Bus method call *after* the user has
// decided, which is why every prompt path defers: the vtable handler refs the
// incoming message and returns a positive integer, which tells sd-bus this
// callback takes responsibility for replying later (see sd_bus_add_object_vtable
// NOTES). The event loop is never blocked waiting on a human.
//
// Scope: this registers an agent but deliberately does *not* call
// RequestDefaultAgent. BlueZ routes a pairing's prompts to the agent registered
// by the client that initiated it, so qypr answers for pairings started in its
// own picker — where a popover is on screen to show the prompt — and leaves
// unsolicited/incoming pairing to whatever default agent the session runs.

#pragma once

#include <cstdint>
#include <functional>
#include <string>

#include <array>

#include <systemd/sd-bus.h>  // sd_bus_vtable must be complete for the member below

namespace qypr {

class SystemBus;

// A pending user decision. Mirrors the Agent1 method that raised it; the picker
// renders one prompt card per kind.
struct BtPairRequest {
    enum class Kind : uint8_t {
        None,
        // RequestConfirmation: both ends show the same 6 digits; the user says
        // whether they match. The common flow for phones and modern headsets.
        Confirm,
        // RequestAuthorization: "Just Works" pairing that still wants a yes.
        Authorize,
        // DisplayPasskey / DisplayPinCode: we show a code for the user to type
        // on the *remote* device. Nothing to accept — only cancel.
        Display,
        // RequestPasskey (6-digit) / RequestPinCode (string): the user types
        // the code shown on the remote device.
        Entry,
        // AuthorizeService: a paired-but-untrusted device wants a profile.
        Service,
    };

    Kind kind = Kind::None;
    std::string devicePath;
    std::string deviceName;
    std::string passkey;  // Confirm/Display: the formatted 6-digit code
    std::string service;  // Service: the requested profile UUID
    // Entry: RequestPasskey wants a number ("u"), RequestPinCode a string.
    bool numericEntry = false;
    // Display: how many digits the remote has typed so far, as BlueZ reports
    // progress through repeated DisplayPasskey calls.
    uint16_t entered = 0;

    [[nodiscard]] bool active() const { return kind != Kind::None; }

    bool operator==(const BtPairRequest&) const = default;
};

class BluetoothAgent {
public:
    explicit BluetoothAgent(SystemBus& bus);
    ~BluetoothAgent();

    // Holds a live D-Bus call and a vtable slot bound to `this`; neither
    // survives being copied or moved.
    BluetoothAgent(const BluetoothAgent&) = delete;
    BluetoothAgent& operator=(const BluetoothAgent&) = delete;
    BluetoothAgent(BluetoothAgent&&) = delete;
    BluetoothAgent& operator=(BluetoothAgent&&) = delete;

    // Export the Agent1 object and register it with BlueZ. Idempotent: safe to
    // call again when BlueZ (re)appears, which is when re-registration is
    // actually needed — a restarted daemon forgets every agent.
    void start();
    // Called when BlueZ drops off the bus: the registration is gone with it.
    void onBluezLost();

    [[nodiscard]] const BtPairRequest& request() const { return req_; }

    // Fired whenever the pending request changes, including back to None.
    void setOnRequest(std::function<void()> cb) { onRequest_ = std::move(cb); }
    // Supplies a human name for a device object path (the backend's snapshot
    // is the only place that knows it).
    void setNameResolver(std::function<std::string(const std::string&)> cb) {
        nameResolver_ = std::move(cb);
    }

    // ── UI answers ──
    // Accept or decline the pending Confirm/Authorize/Service prompt; also the
    // cancel path for Display.
    void respond(bool accept);
    // Answer an Entry prompt with what the user typed. An empty string declines.
    void respondInput(const std::string& text);

private:
    static int onRelease(sd_bus_message* m, void* userdata, sd_bus_error* err);
    static int onRequestPinCode(sd_bus_message* m, void* userdata, sd_bus_error* err);
    static int onDisplayPinCode(sd_bus_message* m, void* userdata, sd_bus_error* err);
    static int onRequestPasskey(sd_bus_message* m, void* userdata, sd_bus_error* err);
    static int onDisplayPasskey(sd_bus_message* m, void* userdata, sd_bus_error* err);
    static int onRequestConfirmation(sd_bus_message* m, void* userdata, sd_bus_error* err);
    static int onRequestAuthorization(sd_bus_message* m, void* userdata, sd_bus_error* err);
    static int onAuthorizeService(sd_bus_message* m, void* userdata, sd_bus_error* err);
    static int onCancel(sd_bus_message* m, void* userdata, sd_bus_error* err);
    static int onRegisterReply(sd_bus_message* reply, void* userdata, sd_bus_error* err);

    // Member rather than file-scope so its initializer can name the private
    // handlers above.
    static const std::array<sd_bus_vtable, 11> kVtable;

    // Park `m` as the pending call and raise `req` to the UI. Returns the value
    // the vtable handler must return (1 = "I will reply later"), or a negative
    // errno when another prompt is already up.
    int defer(sd_bus_message* m, BtPairRequest&& req);
    // Release the pending call with a D-Bus error and clear the prompt.
    void rejectPending(const char* errName, const char* message);
    void clearRequest();
    [[nodiscard]] std::string nameFor(const std::string& path) const;

    SystemBus& bus_;
    sd_bus_slot* vtableSlot_ = nullptr;
    bool exported_ = false;
    bool registered_ = false;
    // The in-flight Agent1 call awaiting the user. Owned (ref'd) while pending;
    // exactly one reply is ever sent for it.
    sd_bus_message* pending_ = nullptr;
    BtPairRequest req_;
    std::function<void()> onRequest_;
    std::function<std::string(const std::string&)> nameResolver_;
};

}  // namespace qypr
