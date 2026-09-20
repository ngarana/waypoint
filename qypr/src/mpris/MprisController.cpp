#include "mpris/MprisController.hpp"

#include <sdbus-c++/sdbus-c++.h>

#include <algorithm>
#include <map>
#include <optional>

#include "core/EventLoop.hpp"

namespace qypr {

#ifndef TESTING
namespace {
constexpr const char* kObjectPath = "/org/mpris/MediaPlayer2";
constexpr const char* kPlayerIface = "org.mpris.MediaPlayer2.Player";
constexpr const char* kAppIface = "org.mpris.MediaPlayer2";
constexpr const char* kPrefix = "org.mpris.MediaPlayer2.";

const std::vector<std::string> kPriority = {"org.mpris.MediaPlayer2.mpv",
                                            "org.mpris.MediaPlayer2.mpd"};
const std::vector<std::string> kPrefixes = {"org.mpris.MediaPlayer2.firefox",
                                            "org.mpris.MediaPlayer2.chromium",
                                            "org.mpris.MediaPlayer2.spotify"};

bool startsWith(const std::string& s, const std::string& p) {
    return s.size() >= p.size() && s.starts_with(p);
}

bool matchesPriority(const std::string& name) {
    if (std::ranges::find(kPriority, name) != kPriority.end()) { return true; }
    for (const auto& p : kPrefixes) {
        if (startsWith(name, p)) { return true; }
    }
    return false;
}

// Ceiling for the synchronous reads below.
//
// These block the event loop, and an MPRIS peer is an arbitrary application: a
// browser still restoring its session at login owns its bus name long before it
// is answering calls on it. Left at D-Bus's 25-second default that is up to
// 25 seconds *per property*, with the whole bar frozen — no repaints, no input.
// The media applet is a nicety, so failing fast and showing nothing is the
// right trade; the next PropertiesChanged signal fills it in anyway.
constexpr uint64_t kCallTimeoutUs = 1'000'000;

template <typename T>
std::optional<T> getProp(sdbus::IProxy& proxy, const char* iface, const char* name) {
    try {
        // Properties.Get by hand rather than getProperty(): sdbus-c++'s
        // PropertyGetter has no withTimeout(), and a bounded wait is the whole
        // point here.
        sdbus::Variant v;
        proxy.callMethod("Get")
            .onInterface("org.freedesktop.DBus.Properties")
            .withTimeout(kCallTimeoutUs)
            .withArguments(std::string(iface), std::string(name))
            .storeResultsTo(v);
        return v.get<T>();
    } catch (...) { return std::nullopt; }
}
}  // namespace
#endif

MprisController::MprisController() {
#ifdef TESTING
    // Mock connection setup: does nothing, just lets available() return true
#else
    try {
        conn_ = sdbus::createSessionBusConnection();
        dbusProxy_ = sdbus::createProxy(*conn_, sdbus::ServiceName{"org.freedesktop.DBus"},
                                        sdbus::ObjectPath{"/org/freedesktop/DBus"});
    } catch (...) {
        conn_.reset();  // no session bus: controller stays inert
    }
#endif
}

MprisController::~MprisController() = default;

void MprisController::refreshAndNotify() {
    const Snapshot before = snap_;
    refresh();
    // Position advances constantly; comparing whole snapshots would fire every
    // signal. Only notify on state the bar actually renders.
    if (!(before == snap_) && onChange_) { onChange_(); }
}

void MprisController::enablePush(EventLoop& loop) {
#ifdef TESTING
    (void)loop;
    pushEnabled_ = true;
#else
    if (pushEnabled_ || !conn_) { return; }
    pushEnabled_ = true;

    try {
        // One match for every MPRIS player's property changes — cheaper and
        // simpler than a proxy per player that must be torn down and rebuilt as
        // the active player changes.
        conn_->addMatch("type='signal',interface='org.freedesktop.DBus.Properties',"
                        "member='PropertiesChanged',path='/org/mpris/MediaPlayer2'",
                        [this](const sdbus::Message&) { refreshAndNotify(); });

        // Players appearing/quitting: the active player may change entirely.
        conn_->addMatch("type='signal',sender='org.freedesktop.DBus',"
                        "interface='org.freedesktop.DBus',member='NameOwnerChanged',"
                        "arg0namespace='org.mpris.MediaPlayer2'",
                        [this](const sdbus::Message&) { refreshAndNotify(); });
    } catch (...) {
        pushEnabled_ = false;  // no matches: caller keeps polling
        return;
    }

    // Dispatch the connection from our loop — never enterEventLoop(), which
    // would spawn a thread (principle 2: single-threaded).
    const int fd = conn_->getEventLoopPollData().fd;
    loop.addFd(fd, [this](uint32_t) {
        try {
            while (conn_->processPendingEvent()) {}
        } catch (...) {
            // A broken session bus must not take the bar down; the media applet
            // simply stops updating.
        }
    });

    refreshAndNotify();  // seed once; everything after this is pushed
#endif
}

#ifndef TESTING
std::unique_ptr<sdbus::IProxy> MprisController::playerProxy(const std::string& name) {
    return sdbus::createProxy(*conn_, sdbus::ServiceName{name}, sdbus::ObjectPath{kObjectPath});
}

std::vector<std::string> MprisController::listPlayers() {
    std::vector<std::string> players;
    if (!dbusProxy_) { return players; }
    try {
        std::vector<std::string> names;
        dbusProxy_->callMethod("ListNames")
            .onInterface("org.freedesktop.DBus")
            .withTimeout(kCallTimeoutUs)
            .storeResultsTo(names);
        for (auto& n : names) {
            if (startsWith(n, kPrefix)) { players.push_back(n); }
        }
    } catch (...) {}
    return players;
}

std::string MprisController::pickActive(const std::vector<std::string>& players) {
    // Preference order: a player with *content* always beats one without, and
    // the priority list only breaks ties within a tier. Previously a stopped
    // prioritized player (an idle browser) outranked a playing one elsewhere
    // (e.g. a phone via kdeconnect), so the UI showed nothing while music was
    // audible — status is what the user is looking at, so it leads.
    std::vector<std::string> prioritized;
    std::string firstPlaying;
    std::string firstPaused;
    std::string prioPlaying;
    std::string prioPaused;

    for (const auto& name : players) {
        const bool prio = matchesPriority(name);
        if (prio) { prioritized.push_back(name); }

        auto proxy = playerProxy(name);
        auto status = getProp<std::string>(*proxy, kPlayerIface, "PlaybackStatus");
        if (!status) { continue; }
        if (*status == "Playing") {
            if (firstPlaying.empty()) { firstPlaying = name; }
            if (prio && prioPlaying.empty()) { prioPlaying = name; }
        } else if (*status != "Stopped") {  // Paused
            if (firstPaused.empty()) { firstPaused = name; }
            if (prio && prioPaused.empty()) { prioPaused = name; }
        }
    }

    if (!prioPlaying.empty()) {
        return prioPlaying;  // playing, preferred app
    }
    if (!firstPlaying.empty()) {
        return firstPlaying;  // playing anywhere
    }
    if (!prioPaused.empty()) {
        return prioPaused;  // paused, preferred app
    }
    if (!firstPaused.empty()) {
        return firstPaused;  // paused anywhere
    }
    if (!prioritized.empty()) {
        return prioritized.front();  // stopped, preferred
    }
    if (!players.empty()) { return players.front(); }
    return "";
}

MprisController::Snapshot MprisController::readSnapshot(const std::string& name) {
    Snapshot s;
    auto proxy = playerProxy(name);
    s.dbusName = name;

    if (auto v = getProp<std::string>(*proxy, kPlayerIface, "PlaybackStatus")) { s.status = *v; }
    if (auto v = getProp<std::string>(*proxy, kAppIface, "Identity")) { s.identity = *v; }
    if (auto v = getProp<bool>(*proxy, kPlayerIface, "CanControl")) { s.canControl = *v; }
    if (auto v = getProp<bool>(*proxy, kPlayerIface, "CanGoNext")) { s.canGoNext = *v; }
    if (auto v = getProp<bool>(*proxy, kPlayerIface, "CanGoPrevious")) { s.canGoPrevious = *v; }
    if (auto v = getProp<int64_t>(*proxy, kPlayerIface, "Position")) { s.positionUs = *v; }
    if (auto v = getProp<double>(*proxy, kPlayerIface, "Volume")) {
        s.volume = *v;
        s.volumeSupported = true;
    }

    // Metadata (a{sv}): title, artist(s), album, length.
    if (auto md =
            getProp<std::map<std::string, sdbus::Variant>>(*proxy, kPlayerIface, "Metadata")) {
        auto& m = *md;
        auto strOf = [&](const char* key) -> std::string {
            auto it = m.find(key);
            if (it == m.end()) { return ""; }
            try {
                return it->second.get<std::string>();
            } catch (...) {}
            try {
                auto arr = it->second.get<std::vector<std::string>>();
                std::string out;
                for (size_t i = 0; i < arr.size(); ++i) { out += (i ? ", " : "") + arr[i]; }
                return out;
            } catch (...) {}
            return "";
        };
        s.title = strOf("xesam:title");
        s.artist = strOf("xesam:artist");
        s.album = strOf("xesam:album");
        auto it = m.find("mpris:length");
        if (it != m.end()) {
            try {
                s.lengthUs = it->second.get<int64_t>();
            } catch (...) {}
        }
    }

    s.valid = true;
    return s;
}
#endif

void MprisController::refresh() {
#ifdef TESTING
    // Populate fake snapshot data
    snap_.valid = true;
    snap_.dbusName = "org.mpris.MediaPlayer2.mock";
    snap_.identity = "Mock Player";
    snap_.title = "Mock Song";
    snap_.artist = "Mock Artist";
    snap_.album = "Mock Album";
    if (snap_.status.empty()) { snap_.status = "Playing"; }
    snap_.volume = 0.8;
    snap_.positionUs = 60 * 1000000;
    snap_.lengthUs = 180 * 1000000;
    snap_.canControl = true;
    snap_.canGoNext = true;
    snap_.canGoPrevious = true;
    snap_.volumeSupported = true;
#else
    if (!conn_) {
        snap_ = Snapshot{};
        return;
    }
    std::string const name = pickActive(listPlayers());
    snap_ = name.empty() ? Snapshot{} : readSnapshot(name);
#endif
}

void MprisController::togglePlaying() {
#ifdef TESTING
    if (!snap_.valid || !snap_.canControl) { return; }
    if (snap_.status == "Playing") {
        snap_.status = "Paused";
    } else {
        snap_.status = "Playing";
    }
#else
    if (!snap_.valid || !snap_.canControl) { return; }
    try {
        playerProxy(snap_.dbusName)
            ->callMethod("PlayPause")
            .onInterface(kPlayerIface)
            .dontExpectReply();
    } catch (...) {}
    refresh();
#endif
}

void MprisController::next() {
#ifdef TESTING
    if (!snap_.valid || !snap_.canGoNext) { return; }
    snap_.title = "Next Song";
    snap_.positionUs = 0;
#else
    if (!snap_.valid || !snap_.canGoNext) { return; }
    try {
        playerProxy(snap_.dbusName)->callMethod("Next").onInterface(kPlayerIface).dontExpectReply();
    } catch (...) {}
    refresh();
#endif
}

void MprisController::previous() {
#ifdef TESTING
    if (!snap_.valid || !snap_.canGoPrevious) { return; }
    snap_.title = "Previous Song";
    snap_.positionUs = 0;
#else
    if (!snap_.valid || !snap_.canGoPrevious) { return; }
    try {
        playerProxy(snap_.dbusName)
            ->callMethod("Previous")
            .onInterface(kPlayerIface)
            .dontExpectReply();
    } catch (...) {}
    refresh();
#endif
}

void MprisController::setVolume(double level) {
#ifdef TESTING
    if (!snap_.valid || !snap_.volumeSupported) { return; }
    snap_.volume = std::clamp(level, 0.0, 1.0);
#else
    if (!snap_.valid || !snap_.volumeSupported) { return; }
    double const clamped = std::clamp(level, 0.0, 1.0);
    try {
        playerProxy(snap_.dbusName)
            ->setProperty("Volume")
            .onInterface(kPlayerIface)
            .toValue(clamped);
        snap_.volume = clamped;
    } catch (...) {}
#endif
}

}  // namespace qypr
