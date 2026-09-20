#include "core/EventLoop.hpp"

#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <sys/timerfd.h>
#include <unistd.h>

#include <array>
#include <cerrno>
#include <cstdint>
#include <cstring>

namespace qypr {

EventLoop::EventLoop()
    : epollFd_(epoll_create1(EPOLL_CLOEXEC)),
      wakeFd_(eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK)) {

    addFd(wakeFd_, [this](uint32_t) {
        uint64_t v = 0;
        while (read(wakeFd_, &v, sizeof(v)) > 0) {}
        dispatchPosted();
    });
}

EventLoop::~EventLoop() {
    for (auto& [fd, _] : timers_) { close(fd); }
    if (wakeFd_ >= 0) { close(wakeFd_); }
    if (epollFd_ >= 0) { close(epollFd_); }
}

void EventLoop::addFd(int fd, FdCallback cb) {
    addFd(fd, EPOLLIN, std::move(cb));
}

void EventLoop::addFd(int fd, uint32_t events, FdCallback cb) {
    fds_[fd] = std::move(cb);
    epoll_event ev{};
    ev.events = events;
    ev.data.fd = fd;
    epoll_ctl(epollFd_, EPOLL_CTL_ADD, fd, &ev);
}

void EventLoop::modifyFd(int fd, uint32_t events) {
    if (static_cast<unsigned int>(fds_.contains(fd)) == 0U) { return; }
    epoll_event ev{};
    ev.events = events;
    ev.data.fd = fd;
    epoll_ctl(epollFd_, EPOLL_CTL_MOD, fd, &ev);
}

void EventLoop::removeFd(int fd) {
    if (fds_.erase(fd) != 0U) { epoll_ctl(epollFd_, EPOLL_CTL_DEL, fd, nullptr); }
}

int EventLoop::addTimer(int64_t intervalMs, bool repeat, Callback cb) {
    int const tfd = timerfd_create(CLOCK_MONOTONIC, TFD_CLOEXEC | TFD_NONBLOCK);
    itimerspec its{};
    its.it_value.tv_sec = intervalMs / 1000;
    its.it_value.tv_nsec = (intervalMs % 1000) * 1'000'000;
    // An all-zero it_value DISARMS a timerfd: clamp "fire now" to 1ns.
    if (its.it_value.tv_sec == 0 && its.it_value.tv_nsec == 0) { its.it_value.tv_nsec = 1; }
    if (repeat) { its.it_interval = its.it_value; }
    timerfd_settime(tfd, 0, &its, nullptr);

    timers_[tfd] = Timer{.cb = std::move(cb), .repeat = repeat};
    addFd(tfd, [this, tfd](uint32_t) {
        uint64_t expirations = 0;
        while (read(tfd, &expirations, sizeof(expirations)) > 0) {}
        auto it = timers_.find(tfd);
        if (it == timers_.end()) { return; }
        bool const repeat = it->second.repeat;
        auto cb = it->second.cb;  // copy: cb may remove this timer
        if (!repeat) { removeTimer(tfd); }
        cb();
    });
    return tfd;
}

void EventLoop::resetTimer(int timerFd, int64_t delayMs) {
    if (!timers_.contains(timerFd)) { return; }
    itimerspec its{};  // all-zero it_value disarms
    if (delayMs >= 0) {
        its.it_value.tv_sec = delayMs / 1000;
        its.it_value.tv_nsec = (delayMs % 1000) * 1'000'000;
        // An all-zero it_value would disarm instead of firing: clamp to 1ns.
        if (its.it_value.tv_sec == 0 && its.it_value.tv_nsec == 0) { its.it_value.tv_nsec = 1; }
    }
    timerfd_settime(timerFd, 0, &its, nullptr);  // it_interval stays 0: one-shot
}

void EventLoop::removeTimer(int timerFd) {
    if (timers_.erase(timerFd) != 0U) {
        removeFd(timerFd);
        close(timerFd);
    }
}

void EventLoop::post(Callback cb) {
    {
        std::scoped_lock const lk(postMutex_);
        posted_.push_back(std::move(cb));
    }
    uint64_t one = 1;
    ssize_t const r = write(wakeFd_, &one, sizeof(one));
    (void)r;
}

void EventLoop::dispatchPosted() {
    std::vector<Callback> batch;
    {
        std::scoped_lock const lk(postMutex_);
        batch.swap(posted_);
    }
    for (auto& cb : batch) { cb(); }
}

void EventLoop::quit() {
    running_ = false;
    uint64_t one = 1;
    ssize_t const r = write(wakeFd_, &one, sizeof(one));  // wake epoll_wait
    (void)r;
}

void EventLoop::run() {
    running_ = true;
    std::array<epoll_event, 16> events{};
    while (running_) {
        for (auto& p : prepares_) { p(); }

        int const n = epoll_wait(epollFd_, events.data(), events.size(), -1);
        if (n < 0) {
            if (errno == EINTR) { continue; }
            break;
        }
        for (int i = 0; i < n && running_; ++i) {
            int const fd = events[i].data.fd;
            auto it = fds_.find(fd);
            if (it == fds_.end()) { continue; }
            auto cb = it->second;  // copy: the handler may remove its own fd
            cb(events[i].events);
        }
    }
}

}  // namespace qypr
