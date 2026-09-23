// ConfigWatcher.cpp - inotify watcher for live config reload.
#include "core/ConfigWatcher.hpp"

#include <cstdio>
#include <cstring>
#include <sys/inotify.h>
#include <unistd.h>

#include <array>

#include "core/EventLoop.hpp"

namespace qypr {

ConfigWatcher::ConfigWatcher(EventLoop& loop) : loop_(loop) {}

ConfigWatcher::~ConfigWatcher() {
    stop();
}

bool ConfigWatcher::watch(const std::string& path, OnChange cb) {
    stop();  // clean up any previous watch

    onChange_ = std::move(cb);

    // Split path into directory + basename. Editors like vim and nano write to
    // a temp file and then rename it over the target, which deletes the old
    // inode. Watching the *directory* for IN_CLOSE_WRITE | IN_MOVED_TO lets us
    // catch both direct writes and atomic save-and-rename.
    const size_t slash = path.rfind('/');
    if (slash == std::string::npos) {
        dir_ = ".";
        name_ = path;
    } else {
        dir_ = path.substr(0, slash);
        name_ = path.substr(slash + 1);
    }

    inotifyFd_ = inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
    if (inotifyFd_ < 0) {
        std::fprintf(stderr, "qypr: inotify_init1 failed: %s\n", std::strerror(errno));
        return false;
    }

    watchFd_ =
        inotify_add_watch(inotifyFd_, dir_.c_str(), IN_CLOSE_WRITE | IN_MOVED_TO | IN_CREATE);
    if (watchFd_ < 0) {
        std::fprintf(stderr, "qypr: inotify_add_watch(%s) failed: %s\n", dir_.c_str(),
                     std::strerror(errno));
        ::close(inotifyFd_);
        inotifyFd_ = -1;
        return false;
    }

    loop_.addFd(inotifyFd_, [this](uint32_t) { drain(); });
    return true;
}

void ConfigWatcher::stop() {
    if (inotifyFd_ < 0) { return; }

    loop_.removeFd(inotifyFd_);
    if (watchFd_ >= 0) {
        inotify_rm_watch(inotifyFd_, watchFd_);
        watchFd_ = -1;
    }
    ::close(inotifyFd_);
    inotifyFd_ = -1;
    onChange_ = nullptr;
}

void ConfigWatcher::drain() {
    // Read and discard all pending events; we only care about the names.
    alignas(struct inotify_event) std::array<char, 4096> buf{};
    bool matched = false;

    for (;;) {
        const ssize_t n = ::read(inotifyFd_, buf.data(), buf.size());
        if (n <= 0) { break; }

        for (ssize_t off = 0; off < n;) {
            const auto* ev = reinterpret_cast<const struct inotify_event*>(buf.data() + off);
            if (ev->len > 0 && name_ == ev->name) { matched = true; }
            off += static_cast<ssize_t>(sizeof(struct inotify_event) + ev->len);
        }
    }

    if (matched && onChange_) {
        // Move the callback out of the member before running it: the handler
        // may re-arm this watcher from inside the call (BarApp::watchPalette
        // does stop() + watch() on every config reload), and stop()/watch()
        // destroy or replace onChange_ — which would otherwise pull the
        // std::function out from under its own invocation (UB; observed as a
        // dead or crashing watcher on the second event).
        auto cb = std::move(onChange_);
        cb();
        // The handler re-armed (watch() installed a new callback): keep it.
        // Otherwise restore ours so later events still fire.
        if (!onChange_) { onChange_ = std::move(cb); }
    }
}

}  // namespace qypr
