// MprisController.hpp - MPRIS media state + control over the session bus.
//
// Port of AudioService.qml. Two modes:
//
//   * Poll (default) — `refresh()` re-queries synchronously. The lock screen
//     drives this from its 1s clock tick: far less machinery for a screen that
//     only samples state while visible (KISS).
//   * Push (`enablePush`) — subscribes to PropertiesChanged/NameOwnerChanged and
//     plugs the connection's fd into the EventLoop, so updates arrive as they
//     happen and nothing is polled. Required by the always-on qypr-bar, where a
//     wakeup every second forever is exactly the footprint cost principle 2
//     forbids (STATUS_BAR.md decision D4).
//
// Push is opt-in so the lock screen's proven path is untouched.

#pragma once

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace sdbus {
class IConnection;
class IProxy;
}  // namespace sdbus

namespace qypr {

class EventLoop;

class MprisController {
public:
    MprisController();
    ~MprisController();

    // Re-query the active player. Cheap no-op if the session bus is unavailable.
    void refresh();

    // Opt into event-driven updates: watch every MPRIS player's
    // PropertiesChanged plus name owner changes (players starting/quitting), and
    // dispatch the connection from `loop` — no thread, no timer. Safe to call
    // once; a no-op without a session bus.
    void enablePush(EventLoop& loop);

    // Fired on the loop thread whenever the snapshot changes (push mode only).
    void setOnChange(std::function<void()> cb) { onChange_ = std::move(cb); }

    // Snapshot accessors (mirror AudioService's read-only properties).
#ifdef TESTING
    bool available() const { return true; }
#else
    bool available() const { return conn_ != nullptr; }
#endif
    bool active() const { return snap_.valid && snap_.status != "Stopped"; }
    bool playing() const { return snap_.status == "Playing"; }
    const std::string& title() const { return snap_.title; }
    const std::string& artist() const { return snap_.artist; }
    const std::string& album() const { return snap_.album; }
    const std::string& sourceLabel() const { return snap_.identity; }
    double positionSeconds() const { return snap_.positionUs / 1'000'000.0; }
    double durationSeconds() const { return snap_.lengthUs / 1'000'000.0; }
    double volume() const { return snap_.volume; }
    bool canGoNext() const { return snap_.canGoNext; }
    bool canGoPrevious() const { return snap_.canGoPrevious; }
    bool canTogglePlaying() const { return snap_.canControl; }
    bool canSetVolume() const { return snap_.canControl && snap_.volumeSupported; }

    // Transport controls.
    void togglePlaying();
    void next();
    void previous();
    void setVolume(double level);

private:
    struct Snapshot {
        bool valid = false;
        std::string dbusName;
        std::string identity;
        std::string title, artist, album;
        std::string status;  // Playing / Paused / Stopped
        double volume = 1.0;
        int64_t positionUs = 0;
        int64_t lengthUs = 0;
        bool canControl = false;
        bool canGoNext = false;
        bool canGoPrevious = false;
        bool volumeSupported = false;

        bool operator==(const Snapshot&) const = default;
    };

    std::vector<std::string> listPlayers();
    std::string pickActive(const std::vector<std::string>& players);
    Snapshot readSnapshot(const std::string& name);
    std::unique_ptr<sdbus::IProxy> playerProxy(const std::string& name);
    // Re-read and notify only when something actually changed (push mode).
    void refreshAndNotify();

    std::unique_ptr<sdbus::IConnection> conn_;
    std::unique_ptr<sdbus::IProxy> dbusProxy_;
    Snapshot snap_;
    std::function<void()> onChange_;
    bool pushEnabled_ = false;
};

}  // namespace qypr
