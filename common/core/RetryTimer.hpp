// RetryTimer.hpp - Backoff timer for transient backend failures.
//
// The timer is created only when schedule() is called. Healthy backends remain
// push-driven and incur no periodic timer or polling traffic.

#pragma once

#include <algorithm>
#include <cstdint>
#include <functional>
#include <utility>

#include "core/EventLoop.hpp"

namespace qypr {

class RetryTimer {
public:
  RetryTimer(EventLoop &loop, EventLoop::Callback callback,
             int64_t initialDelayMs = 1000, int64_t maximumDelayMs = 30000)
      : loop_(loop), callback_(std::move(callback)),
        initialDelayMs_(initialDelayMs), maximumDelayMs_(maximumDelayMs),
        nextDelayMs_(initialDelayMs) {}

  ~RetryTimer() { cancel(); }

  RetryTimer(const RetryTimer &) = delete;
  RetryTimer &operator=(const RetryTimer &) = delete;

  void schedule() {
    if (timerId_ >= 0) {
      return;
    }
    const int64_t delayMs = nextDelayMs_;
    nextDelayMs_ = std::min(nextDelayMs_ * 2, maximumDelayMs_);
    timerId_ = loop_.addTimer(delayMs, /*repeat=*/false, [this] {
      // EventLoop removes one-shot timers before invoking their callback.
      timerId_ = -1;
      callback_();
    });
  }

  // Cancel without resetting backoff (useful when a signal supersedes a retry).
  void cancel() {
    if (timerId_ < 0) {
      return;
    }
    loop_.removeTimer(timerId_);
    timerId_ = -1;
  }

  // A successful read clears any pending timer and restores the first delay.
  void reset() {
    cancel();
    nextDelayMs_ = initialDelayMs_;
  }

private:
  EventLoop &loop_;
  EventLoop::Callback callback_;
  const int64_t initialDelayMs_;
  const int64_t maximumDelayMs_;
  int64_t nextDelayMs_;
  int timerId_ = -1;
};

} // namespace qypr
