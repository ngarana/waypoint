// SystemBus.cpp - Shared system-bus connection implementation.
#include "system/SystemBus.hpp"

#include <poll.h>
#include <sys/epoll.h>

#include <cstdio>
#include <ctime>

#include "core/EventLoop.hpp"

namespace qypr {

SystemBus::SystemBus(EventLoop& loop, BusKind kind) : loop_(loop) {
    int r = kind == BusKind::Session ? sd_bus_open_user(&bus_) : sd_bus_open_system(&bus_);
    if (r < 0) {
        std::fprintf(stderr, "qypr: %s bus unavailable (%d); dependent indicators disabled\n",
                     kind == BusKind::Session ? "session" : "system", r);
        bus_ = nullptr;
        return;
    }
    // Bound every call on this connection (see kCallTimeoutUs): the synchronous
    // ones block the event loop, and 25 seconds of frozen bar is not a failure
    // mode worth keeping.
    sd_bus_set_method_call_timeout(bus_, kCallTimeoutUs);
    fd_ = sd_bus_get_fd(bus_);
    watchMask_ = EPOLLIN;
    loop_.addFd(fd_, watchMask_, [this](uint32_t) { drain(); });
    // Parked disarmed; updateWatch() arms it whenever a call is outstanding.
    timerFd_ = loop_.addTimer(0, true, [this] { drain(); });
    loop_.resetTimer(timerFd_, -1);
    loop_.addPrepare([this] { updateWatch(); });
    drain();
}

SystemBus::~SystemBus() {
    teardown();
}

sd_bus_slot* SystemBus::addMatch(const char* rule, sd_bus_message_handler_t handler,
                                 void* userdata) {
    if (!bus_) return nullptr;
    sd_bus_slot* slot = nullptr;
    int r = sd_bus_add_match(bus_, &slot, rule, handler, userdata);
    if (r < 0) {
        std::fprintf(stderr, "qypr: sd_bus_add_match failed (%d)\n", r);
        return nullptr;
    }
    return slot;
}

void SystemBus::drain() {
    int r;
    while ((r = sd_bus_process(bus_, nullptr)) > 0) {}
    if (r < 0) {
        std::fprintf(stderr, "qypr: system bus error (%d); disconnecting\n", r);
        // Deferred: tearing down from inside the fd callback would destroy the
        // std::function currently executing.
        loop_.post([this] { teardown(); });
    }
}

void SystemBus::updateWatch() {
    if (bus_ == nullptr) { return; }

    // sd-bus asks for writability while it has queued output it could not flush.
    const int events = sd_bus_get_events(bus_);
    if (events >= 0) {
        const auto want = static_cast<unsigned>(events);
        uint32_t mask = 0;
        if ((want & static_cast<unsigned>(POLLIN)) != 0U) { mask |= EPOLLIN; }
        if ((want & static_cast<unsigned>(POLLOUT)) != 0U) { mask |= EPOLLOUT; }
        if (mask != 0 && mask != watchMask_) {
            watchMask_ = mask;
            loop_.modifyFd(fd_, mask);
        }
    }

    // Absolute CLOCK_MONOTONIC deadline, or UINT64_MAX for "nothing pending".
    uint64_t usec = 0;
    if (sd_bus_get_timeout(bus_, &usec) < 0 || usec == UINT64_MAX) {
        if (deadlineSet_) {
            deadlineSet_ = false;
            loop_.resetTimer(timerFd_, -1);
        }
        return;
    }
    // The deadline holds still while a call is in flight, so re-arming only on
    // change keeps this to one syscall per actual change, not per loop pass.
    if (deadlineSet_ && usec == deadline_) { return; }
    deadline_ = usec;
    deadlineSet_ = true;

    timespec ts{};
    clock_gettime(CLOCK_MONOTONIC, &ts);
    const auto now = (static_cast<uint64_t>(ts.tv_sec) * 1'000'000ULL) +
                     (static_cast<uint64_t>(ts.tv_nsec) / 1'000ULL);
    const int64_t delayMs = usec > now ? static_cast<int64_t>((usec - now + 999) / 1'000) : 0;
    loop_.resetTimer(timerFd_, delayMs);
}

void SystemBus::teardown() {
    if (timerFd_ >= 0) {
        loop_.removeTimer(timerFd_);
        timerFd_ = -1;
    }
    if (fd_ >= 0) {
        loop_.removeFd(fd_);
        fd_ = -1;
    }
    if (bus_) {
        sd_bus_unref(bus_);
        bus_ = nullptr;
    }
}

}  // namespace qypr
