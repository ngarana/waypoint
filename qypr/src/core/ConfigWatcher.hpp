// ConfigWatcher.hpp - inotify watcher for live config reload.
//
// Watches bar.conf for writes/renames and fires a user-supplied callback so
// the bar can re-read its Config and re-apply theme::loadTheme() without a
// restart. Integrates with EventLoop via addFd.

#pragma once

#include <functional>
#include <string>

namespace qypr {

class EventLoop;

class ConfigWatcher {
public:
    using OnChange = std::function<void()>;

    explicit ConfigWatcher(EventLoop& loop);
    ~ConfigWatcher();

    ConfigWatcher(const ConfigWatcher&) = delete;
    ConfigWatcher& operator=(const ConfigWatcher&) = delete;

    // Start watching `path`. The callback fires on the event-loop thread
    // whenever the file is written or replaced (editor save-and-rename).
    // Returns false if inotify_init1 or inotify_add_watch fails.
    bool watch(const std::string& path, OnChange cb);

    // Stop watching (also called by the destructor).
    void stop();

private:
    void drain();

    EventLoop& loop_;
    int inotifyFd_ = -1;
    int watchFd_ = -1;
    OnChange onChange_;
    std::string dir_;   // watched directory (editors rename temp files)
    std::string name_;  // basename of the config file
};

}  // namespace qypr
