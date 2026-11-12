// SleepMonitor.cpp - logind sleep watcher implementation.
#include "system/SleepMonitor.hpp"

#include <fcntl.h>
#include <unistd.h>

#include <cstdio>

#include "core/EventLoop.hpp"
#include "system/SystemBus.hpp"

namespace qypr {

namespace {
constexpr const char* kLogind = "org.freedesktop.login1";
constexpr const char* kLogindPath = "/org/freedesktop/login1";
constexpr const char* kManagerIface = "org.freedesktop.login1.Manager";
}  // namespace

SleepMonitor::SleepMonitor(EventLoop& loop, SystemBus& bus) : loop_(loop), bus_(bus) {}

SleepMonitor::~SleepMonitor() {
    if (quiesceTimer_ >= 0) { loop_.removeTimer(quiesceTimer_); }
    sd_bus_slot_unref(signalSlot_);
    sd_bus_slot_unref(propertySlot_);
    sd_bus_slot_unref(inhibitSlot_);
    releaseInhibitor();
}

bool SleepMonitor::start() {
    if (signalSlot_ != nullptr) { return true; }
    if (bus_.available()) {
        // Subscribe before reading the property, so a transition in the gap is
        // seen as a signal (the signal then wins over the stale reply).
        signalSlot_ = bus_.addMatch(
            "type='signal',sender='org.freedesktop.login1',path='/org/freedesktop/login1',"
            "interface='org.freedesktop.login1.Manager',member='PrepareForSleep'",
            &SleepMonitor::onPrepareForSleep, this);
    }
    if (signalSlot_ == nullptr) {
        stateKnown_ = true;
        if (onAwake_) { onAwake_(); }
        return false;
    }
    int const r = sd_bus_call_method_async(
        bus_.get(), &propertySlot_, kLogind, kLogindPath, "org.freedesktop.DBus.Properties", "Get",
        &SleepMonitor::onPreparingReply, this, "ss", kManagerIface, "PreparingForSleep");
    if (r < 0) {
        propertySlot_ = nullptr;
        handle(false);  // cannot tell: assume awake, as before this existed
    }
    return true;
}

int SleepMonitor::onPreparingReply(sd_bus_message* message, void* userdata,
                                   sd_bus_error* /*error*/) {
    auto* self = static_cast<SleepMonitor*>(userdata);
    sd_bus_slot_unref(self->propertySlot_);
    self->propertySlot_ = nullptr;
    if (self->stateKnown_) { return 0; }  // a PrepareForSleep signal got here first

    int preparing = 0;
    if (sd_bus_message_is_method_error(message, nullptr) == 0 &&
        sd_bus_message_enter_container(message, 'v', "b") >= 0) {
        sd_bus_message_read(message, "b", &preparing);
        sd_bus_message_exit_container(message);
    }
    if (preparing != 0) {
        // Launched mid-suspend (hypridle before_sleep_cmd). Stay idle; the
        // PrepareForSleep(false) after resume announces the awake state.
        self->stateKnown_ = true;
        self->sleeping_ = true;
        std::fprintf(stderr, "qypr-lock: started during suspend; fingerprint waits for resume\n");
        return 0;
    }
    self->handle(false);
    return 0;
}

int SleepMonitor::onPrepareForSleep(sd_bus_message* message, void* userdata,
                                    sd_bus_error* /*error*/) {
    int sleeping = 0;
    if (sd_bus_message_read(message, "b", &sleeping) >= 0) {
        static_cast<SleepMonitor*>(userdata)->handle(sleeping != 0);
    }
    return 0;  // let other matches see it too
}

void SleepMonitor::handle(bool sleeping) {
    bool const wasKnown = stateKnown_;
    stateKnown_ = true;
    if (wasKnown && sleeping == sleeping_) { return; }
    sleeping_ = sleeping;

    if (sleeping) {
        uint64_t const sequence = ++sleepSequence_;
        if (quiesceTimer_ >= 0) { loop_.removeTimer(quiesceTimer_); }
        quiesceTimer_ = loop_.addTimer(kQuiesceTimeoutMs, false, [this, sequence] {
            quiesceTimer_ = -1;
            finishQuiesce(sequence, true);
        });
        if (onSuspending_) {
            onSuspending_([this, sequence] { finishQuiesce(sequence, false); });
        } else {
            finishQuiesce(sequence, false);
        }
        return;
    }

    if (quiesceTimer_ >= 0) {
        loop_.removeTimer(quiesceTimer_);
        quiesceTimer_ = -1;
    }
    takeInhibitor();
    if (onAwake_) { onAwake_(); }
}

void SleepMonitor::finishQuiesce(uint64_t sequence, bool timedOut) {
    if (sequence != sleepSequence_ || !sleeping_) { return; }  // stale `ready`
    if (quiesceTimer_ >= 0) {
        loop_.removeTimer(quiesceTimer_);
        quiesceTimer_ = -1;
    }
    if (timedOut && inhibitFd_ >= 0) {
        std::fprintf(stderr,
                     "qypr-lock: suspend quiesce timed out after %d ms; letting it proceed\n",
                     kQuiesceTimeoutMs);
    }
    releaseInhibitor();  // logind may suspend now
}

// Asynchronous: the loop thread must not block on logind, and a missed lock is
// harmless (suspend simply is not delayed for us).
void SleepMonitor::takeInhibitor() {
    if (inhibitFd_ >= 0 || inhibitSlot_ != nullptr || !bus_.available()) { return; }
    int const r = sd_bus_call_method_async(
        bus_.get(), &inhibitSlot_, kLogind, kLogindPath, kManagerIface, "Inhibit",
        &SleepMonitor::onInhibitReply, this, "ssss", "sleep", "qypr-lock",
        "Releasing the fingerprint reader before suspend", "delay");
    if (r < 0) {
        std::fprintf(stderr, "qypr-lock: sleep inhibitor request failed (%d)\n", r);
        inhibitSlot_ = nullptr;
    }
}

int SleepMonitor::onInhibitReply(sd_bus_message* message, void* userdata, sd_bus_error* /*error*/) {
    auto* self = static_cast<SleepMonitor*>(userdata);
    // The async slot is single-shot; drop our reference to it.
    sd_bus_slot_unref(self->inhibitSlot_);
    self->inhibitSlot_ = nullptr;

    if (sd_bus_message_is_method_error(message, nullptr) != 0) {
        const sd_bus_error* e = sd_bus_message_get_error(message);
        std::fprintf(stderr, "qypr-lock: sleep inhibitor refused: %s\n",
                     e != nullptr && e->message != nullptr ? e->message : "?");
        return 0;
    }
    int fd = -1;
    if (sd_bus_message_read(message, "h", &fd) < 0 || fd < 0) { return 0; }
    // The fd belongs to the message; keep our own. If a suspend raced the reply
    // there is nothing to hold, so just let it go with the message.
    if (self->sleeping_ || self->inhibitFd_ >= 0) { return 0; }
    self->inhibitFd_ = fcntl(fd, F_DUPFD_CLOEXEC, 3);
    return 0;
}

void SleepMonitor::releaseInhibitor() {
    if (inhibitFd_ >= 0) {
        close(inhibitFd_);
        inhibitFd_ = -1;
    }
}

}  // namespace qypr
