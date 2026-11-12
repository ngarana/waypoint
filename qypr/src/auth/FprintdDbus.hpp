// FprintdDbus.hpp - sd-bus implementation of FprintdTransport.
//
// Rides the lock's shared system-bus connection (SystemBus): every call is
// asynchronous, so the event loop never blocks on fprintd.
//
// Trust: a VerifyStatus "verify-match" is what unlocks the session, so it is
// only accepted from the fprintd connection that granted our claim. The unique
// bus name of that connection is taken from the GetDefaultDevice reply (a reply
// can only come from the peer we called; the broker stamps the sender), the
// VerifyStatus match is installed for that unique name and device path only,
// and the handler re-checks both. The system bus's default policy lets any
// local process send signals, and a unicast signal reaches its destination no
// matter what it subscribed to, so without these checks a forged "verify-match"
// sent straight to qypr-lock would unlock the session.

#pragma once

#include <systemd/sd-bus.h>

#include <functional>
#include <list>
#include <string>

#include "auth/FprintdTransport.hpp"

namespace qypr {

class EventLoop;
class SystemBus;

class FprintdDbus final : public FprintdTransport {
public:
    FprintdDbus(EventLoop& loop, SystemBus& bus);
    ~FprintdDbus() override;

    FprintdDbus(const FprintdDbus&) = delete;
    FprintdDbus& operator=(const FprintdDbus&) = delete;

    void setHandlers(StatusHandler status, VanishedHandler vanished) override;
    void claim(Reply done) override;
    void verifyStart(Reply done) override;
    void verifyStop(Reply done) override;
    void release(Reply done) override;

    // Unique bus name of the fprintd instance holding our claim ("" if none).
    const std::string& owner() const { return owner_; }

private:
    using OnReply = std::function<void(sd_bus_message* reply, const FprintdError& error)>;
    struct Pending {
        FprintdDbus* self = nullptr;
        sd_bus_slot* slot = nullptr;
        OnReply onReply;
    };

    // One async method call. `arg` is the single string argument, or nullptr
    // for none. `onReply` runs exactly once, on the loop thread; a call that
    // cannot even be sent completes with an error on the next loop pass.
    void call(const char* destination, const char* path, const char* interface, const char* member,
              const char* arg, OnReply onReply);
    void deviceCall(const char* member, const char* arg, Reply done);
    void fail(OnReply onReply, std::string name, std::string message);
    bool bind(const char* owner, const char* path);
    void unbind();

    static FprintdError errorOf(sd_bus_message* reply);
    static int onReply(sd_bus_message* message, void* userdata, sd_bus_error* error);
    static int onVerifyStatus(sd_bus_message* message, void* userdata, sd_bus_error* error);
    static int onNameOwnerChanged(sd_bus_message* message, void* userdata, sd_bus_error* error);

    EventLoop& loop_;
    SystemBus& bus_;
    std::string owner_;       // unique name of the claiming fprintd connection
    std::string devicePath_;  // e.g. /net/reactivated/Fprint/Device/0
    sd_bus_slot* statusSlot_ = nullptr;
    sd_bus_slot* ownerSlot_ = nullptr;
    std::list<Pending> pending_;  // stable addresses: each is its slot's userdata
    StatusHandler status_;
    VanishedHandler vanished_;
    bool warnedForeign_ = false;
};

}  // namespace qypr
