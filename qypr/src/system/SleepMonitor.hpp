// SleepMonitor.hpp - logind sleep watcher with a delay inhibitor.
//
// Lets the lock screen quiesce work that must not be mid-operation when the
// machine suspends — chiefly the fingerprint scan, which fprintd otherwise
// aborts underneath us ("Cannot run while suspended").
//
// The standard logind sequence (hyprlock, gnome-shell, and the
// org.freedesktop.login1 inhibitor documentation all follow it):
//   * While awake, hold a `delay` inhibitor on "sleep". logind then emits
//     PrepareForSleep(true) and waits — at most InhibitDelayMaxSec, 5 s by
//     default — for every delay inhibitor fd to close before suspending.
//   * PrepareForSleep(true): onSuspending(ready) runs; the inhibitor is closed
//     when it calls `ready`, or after kQuiesceTimeoutMs if it never does.
//   * PrepareForSleep(false): take a fresh inhibitor, then onAwake().
//   * At startup, read PreparingForSleep first. hypridle's before_sleep_cmd
//     locks the session, so qypr-lock is often launched *while* a suspend is
//     already under way; it must then neither start a scan nor take an
//     inhibitor until the machine has resumed.
//
// onAwake() also announces the initial awake state (once known), so callers
// start their work from it rather than unconditionally at launch. When the
// bus or logind is unavailable, onAwake() runs straight from start() and
// nothing else ever fires.

#pragma once

#include <systemd/sd-bus.h>

#include <cstdint>
#include <functional>

namespace qypr {

class EventLoop;
class SystemBus;

class SleepMonitor {
public:
    using Ready = std::function<void()>;

    SleepMonitor(EventLoop& loop, SystemBus& bus);
    ~SleepMonitor();

    SleepMonitor(const SleepMonitor&) = delete;
    SleepMonitor& operator=(const SleepMonitor&) = delete;

    // Both run on the loop thread. `ready` may be called synchronously or later;
    // a call after the deadline, or after resume, is ignored.
    void setHandlers(std::function<void(Ready ready)> onSuspending, std::function<void()> onAwake) {
        onSuspending_ = std::move(onSuspending);
        onAwake_ = std::move(onAwake);
    }

    // Subscribe, then learn the current state. False if the bus is unavailable
    // or the subscription failed (onAwake() has then already run).
    bool start();

    bool sleeping() const { return sleeping_; }
    bool holdingInhibitor() const { return inhibitFd_ >= 0; }

    // Upper bound on onSuspending's quiesce, well inside logind's 5 s default.
    static constexpr int kQuiesceTimeoutMs = 2000;

private:
    static int onPrepareForSleep(sd_bus_message* message, void* userdata, sd_bus_error* error);
    static int onPreparingReply(sd_bus_message* message, void* userdata, sd_bus_error* error);
    static int onInhibitReply(sd_bus_message* message, void* userdata, sd_bus_error* error);

    void handle(bool sleeping);
    void finishQuiesce(uint64_t sequence, bool timedOut);
    void takeInhibitor();
    void releaseInhibitor();

    EventLoop& loop_;
    SystemBus& bus_;
    sd_bus_slot* signalSlot_ = nullptr;
    sd_bus_slot* propertySlot_ = nullptr;  // pending PreparingForSleep read
    sd_bus_slot* inhibitSlot_ = nullptr;   // pending async Inhibit() call
    int inhibitFd_ = -1;
    int quiesceTimer_ = -1;
    bool sleeping_ = false;
    bool stateKnown_ = false;
    uint64_t sleepSequence_ = 0;  // identifies the current suspend for `ready`
    std::function<void(Ready)> onSuspending_;
    std::function<void()> onAwake_;
};

}  // namespace qypr
