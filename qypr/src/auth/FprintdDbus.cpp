// FprintdDbus.cpp - sd-bus implementation of FprintdTransport.
#include "auth/FprintdDbus.hpp"

#include <cstdio>
#include <cstring>
#include <utility>

#include "core/EventLoop.hpp"
#include "system/SystemBus.hpp"

namespace qypr {

namespace {
constexpr const char* kFprintd = "net.reactivated.Fprint";
constexpr const char* kManagerPath = "/net/reactivated/Fprint/Manager";
constexpr const char* kManagerIface = "net.reactivated.Fprint.Manager";
constexpr const char* kDeviceIface = "net.reactivated.Fprint.Device";
constexpr const char* kLocalError = "org.qypr.Error.SendFailed";
}  // namespace

FprintdDbus::FprintdDbus(EventLoop& loop, SystemBus& bus) : loop_(loop), bus_(bus) {}

FprintdDbus::~FprintdDbus() {
    // Unreffing a pending call's slot cancels its reply callback, so nothing
    // can call back into a destroyed object.
    for (Pending& p : pending_) { sd_bus_slot_unref(p.slot); }
    pending_.clear();
    sd_bus_slot_unref(statusSlot_);
    sd_bus_slot_unref(ownerSlot_);
}

void FprintdDbus::setHandlers(StatusHandler status, VanishedHandler vanished) {
    status_ = std::move(status);
    vanished_ = std::move(vanished);
}

FprintdError FprintdDbus::errorOf(sd_bus_message* reply) {
    if (reply == nullptr || sd_bus_message_is_method_error(reply, nullptr) == 0) { return {}; }
    const sd_bus_error* e = sd_bus_message_get_error(reply);
    return FprintdError{.name = (e != nullptr && e->name != nullptr) ? e->name : kLocalError,
                        .message = (e != nullptr && e->message != nullptr) ? e->message : ""};
}

void FprintdDbus::fail(OnReply onReply, std::string name, std::string message) {
    // Deferred, so callers never see a completion re-entering them.
    loop_.post([onReply = std::move(onReply),
                error = FprintdError{.name = std::move(name), .message = std::move(message)}] {
        onReply(nullptr, error);
    });
}

void FprintdDbus::call(const char* destination, const char* path, const char* interface,
                       const char* member, const char* arg, OnReply onReply) {
    if (!bus_.available()) {
        fail(std::move(onReply), kLocalError, "system bus unavailable");
        return;
    }
    Pending& p = pending_.emplace_back();
    p.self = this;
    p.onReply = std::move(onReply);
    int const r = arg != nullptr
                      ? sd_bus_call_method_async(bus_.get(), &p.slot, destination, path, interface,
                                                 member, &FprintdDbus::onReply, &p, "s", arg)
                      : sd_bus_call_method_async(bus_.get(), &p.slot, destination, path, interface,
                                                 member, &FprintdDbus::onReply, &p, nullptr);
    if (r < 0) {
        OnReply cb = std::move(p.onReply);
        pending_.pop_back();
        fail(std::move(cb), kLocalError, std::string(member) + ": " + std::strerror(-r));
    }
}

int FprintdDbus::onReply(sd_bus_message* message, void* userdata, sd_bus_error* /*error*/) {
    auto* p = static_cast<Pending*>(userdata);
    FprintdDbus* self = p->self;
    OnReply cb = std::move(p->onReply);
    // The async slot is single-shot: drop our reference and the bookkeeping
    // before running the callback, which may issue the next call.
    sd_bus_slot_unref(p->slot);
    for (auto it = self->pending_.begin(); it != self->pending_.end(); ++it) {
        if (&*it == p) {
            self->pending_.erase(it);
            break;
        }
    }
    if (cb) { cb(message, errorOf(message)); }
    return 0;
}

void FprintdDbus::claim(Reply done) {
    // The well-known name activates fprintd if it has exited; its reply names
    // the unique connection every later call and signal must come from.
    call(kFprintd, kManagerPath, kManagerIface, "GetDefaultDevice", nullptr,
         [this, done = std::move(done)](sd_bus_message* reply, const FprintdError& error) {
             if (error.failed()) {
                 done(error);
                 return;
             }
             const char* path = nullptr;
             const char* sender = sd_bus_message_get_sender(reply);
             if (sd_bus_message_read(reply, "o", &path) < 0 || path == nullptr ||
                 sender == nullptr) {
                 done({.name = fprintd_error::kInternal, .message = "bad GetDefaultDevice reply"});
                 return;
             }
             if (!bind(sender, path)) {
                 done({.name = kLocalError, .message = "cannot subscribe to VerifyStatus"});
                 return;
             }
             // "" = the calling user (fprintd resolves it from our credentials).
             deviceCall("Claim", "", done);
         });
}

void FprintdDbus::verifyStart(Reply done) {
    deviceCall("VerifyStart", "any", std::move(done));
}

void FprintdDbus::verifyStop(Reply done) {
    deviceCall("VerifyStop", nullptr, std::move(done));
}

void FprintdDbus::release(Reply done) {
    deviceCall("Release", nullptr, std::move(done));
}

void FprintdDbus::deviceCall(const char* member, const char* arg, Reply done) {
    if (owner_.empty()) {
        fail([done = std::move(done)](sd_bus_message*, const FprintdError& e) { done(e); },
             fprintd_error::kClaimDevice, "no claimed fprintd device");
        return;
    }
    // Addressed to the unique name, not the well-known one: if fprintd has
    // restarted, the claim died with it, and the call should fail rather than
    // reach (or activate) a fresh instance that never granted it.
    call(owner_.c_str(), devicePath_.c_str(), kDeviceIface, member, arg,
         [done = std::move(done)](sd_bus_message*, const FprintdError& e) { done(e); });
}

bool FprintdDbus::bind(const char* owner, const char* path) {
    if (statusSlot_ != nullptr && owner_ == owner && devicePath_ == path) { return true; }
    unbind();
    // Unique names (":1.42") and object paths cannot contain quotes, so they
    // are safe to splice into a match rule.
    std::string const rule = std::string("type='signal',sender='") + owner + "',path='" + path +
                             "',interface='" + kDeviceIface + "',member='VerifyStatus'";
    statusSlot_ = bus_.addMatch(rule.c_str(), &FprintdDbus::onVerifyStatus, this);
    if (statusSlot_ == nullptr) { return false; }
    owner_ = owner;
    devicePath_ = path;
    if (ownerSlot_ == nullptr) {
        ownerSlot_ = bus_.addMatch(
            "type='signal',sender='org.freedesktop.DBus',interface='org.freedesktop.DBus',"
            "member='NameOwnerChanged',arg0='net.reactivated.Fprint'",
            &FprintdDbus::onNameOwnerChanged, this);
    }
    return true;
}

void FprintdDbus::unbind() {
    sd_bus_slot_unref(statusSlot_);
    statusSlot_ = nullptr;
    owner_.clear();
    devicePath_.clear();
}

int FprintdDbus::onVerifyStatus(sd_bus_message* message, void* userdata, sd_bus_error* /*error*/) {
    auto* self = static_cast<FprintdDbus*>(userdata);
    const char* sender = sd_bus_message_get_sender(message);
    const char* path = sd_bus_message_get_path(message);
    if (self->owner_.empty() || sender == nullptr || path == nullptr || self->owner_ != sender ||
        self->devicePath_ != path) {
        if (!self->warnedForeign_) {
            self->warnedForeign_ = true;
            std::fprintf(stderr, "qypr-lock: ignoring VerifyStatus from %s (fprintd is %s)\n",
                         sender != nullptr ? sender : "?",
                         self->owner_.empty() ? "unclaimed" : self->owner_.c_str());
        }
        return 0;
    }
    const char* result = nullptr;
    int done = 0;
    if (sd_bus_message_read(message, "sb", &result, &done) < 0 || result == nullptr) { return 0; }
    if (self->status_) { self->status_(result, done != 0); }
    return 0;
}

int FprintdDbus::onNameOwnerChanged(sd_bus_message* message, void* userdata,
                                    sd_bus_error* /*error*/) {
    auto* self = static_cast<FprintdDbus*>(userdata);
    const char* name = nullptr;
    const char* oldOwner = nullptr;
    const char* newOwner = nullptr;
    if (sd_bus_message_read(message, "sss", &name, &oldOwner, &newOwner) < 0) { return 0; }
    if (self->owner_.empty() || oldOwner == nullptr || self->owner_ != oldOwner) { return 0; }
    if (newOwner != nullptr && self->owner_ == newOwner) { return 0; }
    self->unbind();
    if (self->vanished_) { self->vanished_(); }
    return 0;
}

}  // namespace qypr
