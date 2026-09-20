// SystemBus.hpp - Shared sd-bus connection for status bar backends.
//
// One connection per bus, one fd in the epoll EventLoop, any number of
// subscribers (minimal-footprint principle). Backends add signal matches
// through addMatch() and may issue their single startup fetch on get();
// after that everything is push — no polling.
//
// Named for its original (and default) role as the system-bus connection;
// BusKind::Session opens the user session bus with identical semantics.

#pragma once

#include <systemd/sd-bus.h>

namespace qypr {

class EventLoop;

enum class BusKind { System, Session };

class SystemBus {
public:
    explicit SystemBus(EventLoop& loop, BusKind kind = BusKind::System);
    ~SystemBus();

    SystemBus(const SystemBus&) = delete;
    SystemBus& operator=(const SystemBus&) = delete;

    // nullptr when the system bus is unavailable; backends degrade, non-fatal.
    sd_bus* get() const { return bus_; }
    bool available() const { return bus_ != nullptr; }

    // Subscribe to a match rule. The returned slot is owned by the caller
    // (sd_bus_slot_unref to unsubscribe); nullptr on failure.
    sd_bus_slot* addMatch(const char* rule, sd_bus_message_handler_t handler, void* userdata);

    // Ceiling for every method call on this connection, applied at open time.
    //
    // sd-bus defaults to 25 seconds. That is a sane ceiling for a call the user
    // deliberately triggered, but several of ours are *synchronous* — tray item
    // properties, watcher presence — and block the event loop, so one wedged
    // peer would freeze the whole bar for 25 seconds: no repaints, no input.
    // Third-party tray applets, which are exactly the peers most likely to be
    // wedged or still starting at login, are the realistic case.
    //
    // Five seconds rather than one because the same bound applies to async
    // calls, and an async call is what *activates* a D-Bus service — UPower is
    // not running when the bar starts and takes a second or two to come up.
    // Five is comfortably above that and far below a visible freeze. A call that
    // does time out is not fatal either way: every backend re-fetches on the
    // NameOwnerChanged that fires when its daemon finally appears.
    static constexpr uint64_t kCallTimeoutUs = 5'000'000;

private:
    void drain();
    void teardown();
    // Re-sync the epoll mask and the deadline timer with what sd-bus currently
    // wants. sd-bus owns both: sd_bus_get_events() may ask for writability when
    // output is queued, and sd_bus_get_timeout() carries the deadline of the
    // soonest pending method call. Neither is static, so both are recomputed
    // before each wait — the loop still blocks indefinitely when sd-bus has
    // nothing outstanding, so this stays push-driven rather than polling.
    void updateWatch();

    EventLoop& loop_;
    sd_bus* bus_ = nullptr;
    int fd_ = -1;
    // Fires at sd-bus's next deadline so call timeouts are actually noticed.
    // Without it a request whose peer never answers hangs forever: sd-bus only
    // expires calls from inside sd_bus_process(), which used to run on incoming
    // traffic alone — and a silent peer produces none.
    int timerFd_ = -1;
    uint32_t watchMask_ = 0;  // last epoll mask applied to fd_
    uint64_t deadline_ = 0;   // last deadline armed (absolute CLOCK_MONOTONIC us)
    bool deadlineSet_ = false;
};

}  // namespace qypr
