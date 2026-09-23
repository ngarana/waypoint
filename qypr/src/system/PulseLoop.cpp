// PulseLoop.cpp - pa_mainloop_api over EventLoop implementation.
//
// The three PA event structs are opaque to libpulse; we define them here.
// Lifetime notes:
//  - io events: libpulse may free an event from inside its own callback.
//    EventLoop's dispatch copies the handler before invoking it, so
//    removeFd() from within the handler is safe.
//  - time events: EventLoop one-shot timers remove themselves before the
//    callback runs, so the PA callback may freely restart or free the event.
//  - defer events: implemented as self-rearming post()s; the posted lambda
//    re-checks liveness so enable(0)/free() between post and dispatch is safe.
#include "system/PulseLoop.hpp"

#include <sys/epoll.h>
#include <sys/time.h>

#include <algorithm>
#include <cstdio>

#include "core/EventLoop.hpp"

struct pa_io_event {
    qypr::EventLoop* loop = nullptr;
    pa_mainloop_api* api = nullptr;
    int fd = -1;
    pa_io_event_cb_t cb = nullptr;
    void* userdata = nullptr;
    pa_io_event_destroy_cb_t destroyCb = nullptr;
};

struct pa_time_event {
    qypr::EventLoop* loop = nullptr;
    pa_mainloop_api* api = nullptr;
    int timerFd = -1;  // -1 while unarmed
    struct timeval tv{};
    pa_time_event_cb_t cb = nullptr;
    void* userdata = nullptr;
    pa_time_event_destroy_cb_t destroyCb = nullptr;
};

struct pa_defer_event {
    qypr::EventLoop* loop = nullptr;
    pa_mainloop_api* api = nullptr;
    bool enabled = true;
    bool scheduled = false;
    bool dead = false;
    pa_defer_event_cb_t cb = nullptr;
    void* userdata = nullptr;
    pa_defer_event_destroy_cb_t destroyCb = nullptr;
};

namespace qypr {

namespace {

uint32_t toEpoll(pa_io_event_flags_t f) {
    uint32_t e = 0;
    if (f & PA_IO_EVENT_INPUT) e |= EPOLLIN;
    if (f & PA_IO_EVENT_OUTPUT) e |= EPOLLOUT;
    return e;
}

pa_io_event_flags_t fromEpoll(uint32_t e) {
    int f = PA_IO_EVENT_NULL;
    if (e & EPOLLIN) f |= PA_IO_EVENT_INPUT;
    if (e & EPOLLOUT) f |= PA_IO_EVENT_OUTPUT;
    if (e & EPOLLHUP) f |= PA_IO_EVENT_HANGUP;
    if (e & EPOLLERR) f |= PA_IO_EVENT_ERROR;
    // NOLINTNEXTLINE(clang-analyzer-optin.core.EnumCastOutOfRange) // PA flag composition via int
    return static_cast<pa_io_event_flags_t>(f);
}

int64_t delayMsUntil(const struct timeval* tv) {
    struct timeval now{};
    gettimeofday(&now, nullptr);
    int64_t ms = (static_cast<int64_t>(tv->tv_sec) - now.tv_sec) * 1000 +
                 (static_cast<int64_t>(tv->tv_usec) - now.tv_usec) / 1000;
    return std::max<int64_t>(ms, 1);
}

// --- io events ---------------------------------------------------------

pa_io_event* ioNew(pa_mainloop_api* a, int fd, pa_io_event_flags_t events, pa_io_event_cb_t cb,
                   void* userdata) {
    auto* loop = static_cast<PulseLoop*>(a->userdata);
    // NOLINTNEXTLINE(cppcoreguidelines-owning-memory) // opaque C handle, see file top
    auto* ev = new pa_io_event{&loop->loop(), a, fd, cb, userdata};
    ev->loop->addFd(fd, toEpoll(events), [ev](uint32_t epollEvents) {
        ev->cb(ev->api, ev, ev->fd, fromEpoll(epollEvents), ev->userdata);
    });
    return ev;
}

void ioEnable(pa_io_event* ev, pa_io_event_flags_t events) {
    ev->loop->modifyFd(ev->fd, toEpoll(events));
}

void ioFree(pa_io_event* ev) {
    ev->loop->removeFd(ev->fd);
    if (ev->destroyCb) ev->destroyCb(ev->api, ev, ev->userdata);
    // NOLINTNEXTLINE(cppcoreguidelines-owning-memory) // opaque C handle, see file top
    delete ev;
}

void ioSetDestroy(pa_io_event* ev, pa_io_event_destroy_cb_t cb) {
    ev->destroyCb = cb;
}

// --- time events -------------------------------------------------------

void timeArm(pa_time_event* ev) {
    ev->timerFd = ev->loop->addTimer(delayMsUntil(&ev->tv), false, [ev] {
        ev->timerFd = -1;  // the one-shot removed itself before this runs
        ev->cb(ev->api, ev, &ev->tv, ev->userdata);
    });
}

pa_time_event* timeNew(pa_mainloop_api* a, const struct timeval* tv, pa_time_event_cb_t cb,
                       void* userdata) {
    auto* loop = static_cast<PulseLoop*>(a->userdata);
    // NOLINTNEXTLINE(cppcoreguidelines-owning-memory) // opaque C handle, see file top
    auto* ev = new pa_time_event{&loop->loop(), a, -1, *tv, cb, userdata};
    timeArm(ev);
    return ev;
}

void timeRestart(pa_time_event* ev, const struct timeval* tv) {
    if (ev->timerFd >= 0) {
        ev->loop->removeTimer(ev->timerFd);
        ev->timerFd = -1;
    }
    ev->tv = *tv;
    timeArm(ev);
}

void timeFree(pa_time_event* ev) {
    if (ev->timerFd >= 0) ev->loop->removeTimer(ev->timerFd);
    if (ev->destroyCb) ev->destroyCb(ev->api, ev, ev->userdata);
    // NOLINTNEXTLINE(cppcoreguidelines-owning-memory) // opaque C handle, see file top
    delete ev;
}

void timeSetDestroy(pa_time_event* ev, pa_time_event_destroy_cb_t cb) {
    ev->destroyCb = cb;
}

// --- defer events ------------------------------------------------------

void deferSchedule(pa_defer_event* ev) {
    if (ev->scheduled) return;
    ev->scheduled = true;
    ev->loop->post([ev] {
        ev->scheduled = false;
        if (ev->dead) {
            if (ev->destroyCb) ev->destroyCb(ev->api, ev, ev->userdata);
            // NOLINTNEXTLINE(cppcoreguidelines-owning-memory) // opaque C handle, see file top
            delete ev;
            return;
        }
        if (!ev->enabled) return;
        ev->cb(ev->api, ev, ev->userdata);
        // Defer events run every iteration while enabled (and still alive).
        if (ev->enabled && !ev->dead) deferSchedule(ev);
    });
}

pa_defer_event* deferNew(pa_mainloop_api* a, pa_defer_event_cb_t cb, void* userdata) {
    auto* loop = static_cast<PulseLoop*>(a->userdata);
    // NOLINTNEXTLINE(cppcoreguidelines-owning-memory) // opaque C handle, see file top
    auto* ev = new pa_defer_event{&loop->loop(), a, true, false, false, cb, userdata};
    deferSchedule(ev);
    return ev;
}

void deferEnable(pa_defer_event* ev, int b) {
    ev->enabled = b != 0;
    if (ev->enabled) deferSchedule(ev);
}

void deferFree(pa_defer_event* ev) {
    ev->enabled = false;
    ev->dead = true;
    if (!ev->scheduled) {
        if (ev->destroyCb) ev->destroyCb(ev->api, ev, ev->userdata);
        // NOLINTNEXTLINE(cppcoreguidelines-owning-memory) // opaque C handle, see file top
        delete ev;
    }
    // else: the posted lambda sees dead and deletes it.
}

void deferSetDestroy(pa_defer_event* ev, pa_defer_event_destroy_cb_t cb) {
    ev->destroyCb = cb;
}

// --- misc --------------------------------------------------------------

void apiQuit(pa_mainloop_api*, int retval) {
    // Never forward to EventLoop::quit(): the loop is shared with the whole
    // lockscreen; a PulseAudio failure must not bring the UI down.
    std::fprintf(stderr, "qypr: libpulse requested mainloop quit (%d); ignored\n", retval);
}

}  // namespace

PulseLoop::PulseLoop(EventLoop& loop) : loop_(loop), api_{} {
    api_.userdata = this;
    api_.io_new = ioNew;
    api_.io_enable = ioEnable;
    api_.io_free = ioFree;
    api_.io_set_destroy = ioSetDestroy;
    api_.time_new = timeNew;
    api_.time_restart = timeRestart;
    api_.time_free = timeFree;
    api_.time_set_destroy = timeSetDestroy;
    api_.defer_new = deferNew;
    api_.defer_enable = deferEnable;
    api_.defer_free = deferFree;
    api_.defer_set_destroy = deferSetDestroy;
    api_.quit = apiQuit;
}

}  // namespace qypr
