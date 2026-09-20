// PulseLoop.hpp - pa_mainloop_api adapter over the epoll EventLoop.
//
// libpulse is loop-agnostic: it drives all its sockets and timeouts through
// a pa_mainloop_api vtable. This adapter maps that vtable onto EventLoop —
// io events become fd watches (including write watches), time events become
// one-shot timers, defer events become self-rearming post()s — so PulseAudio
// traffic runs on the same single thread as everything else. No pa_mainloop,
// no pa_threaded_mainloop, no polling.

#pragma once

#include <pulse/mainloop-api.h>

namespace qypr {

class EventLoop;

class PulseLoop {
public:
    explicit PulseLoop(EventLoop& loop);

    PulseLoop(const PulseLoop&) = delete;
    PulseLoop& operator=(const PulseLoop&) = delete;

    // The vtable to hand to pa_context_new(). Valid for this object's life;
    // all events created through it must be freed (by libpulse) before this
    // adapter is destroyed — pa_context_disconnect() does exactly that.
    pa_mainloop_api* api() { return &api_; }

    EventLoop& loop() { return loop_; }

private:
    EventLoop& loop_;
    pa_mainloop_api api_;
};

}  // namespace qypr
