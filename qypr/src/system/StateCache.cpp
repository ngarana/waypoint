// StateCache.cpp - See the header for why this exists.
#include "system/StateCache.hpp"

#include <sys/stat.h>
#include <unistd.h>

#include <cstdio>
#include <cstdlib>
#include <string>

#include "core/Config.hpp"
#include "core/EventLoop.hpp"

namespace qypr {

namespace {

// $XDG_CACHE_HOME/qypr, else $HOME/.cache/qypr. No trailing slash.
std::string cacheDir() {
    if (const char* xdg = std::getenv("XDG_CACHE_HOME"); xdg != nullptr && *xdg != '\0') {
        return std::string(xdg) + "/qypr";
    }
    const char* home = std::getenv("HOME");
    return std::string(home != nullptr ? home : ".") + "/.cache/qypr";
}

}  // namespace

std::string StateCache::defaultPath() {
    return cacheDir() + "/bar-state";
}

// -----------------------------------------------------------------------------
// Read + seed
// -----------------------------------------------------------------------------
void StateCache::load() {
    Config c;
    if (!c.load(path_)) { return; }  // absent or unreadable: everything stays default
    loaded_ = true;

    battery_.percentage = c.getInt("battery", "percentage", 0);
    battery_.present = c.getBool("battery", "present", false);
    // Deliberately not restored: charge/discharge state and the time-to-*
    // estimates. They are the values most likely to have changed while the
    // machine was off (the user very possibly plugged it in), and a wrong
    // charging arrow is more misleading than no arrow. Percentage alone is the
    // useful, slow-moving part.

    volume_.available = c.getBool("volume", "available", false);
    volume_.level = c.getDouble("volume", "level", 0.0);
    volume_.muted = c.getBool("volume", "muted", false);
    volume_.sinkName = c.getString("volume", "sink");

    wifi_.available = c.getBool("wifi", "available", false);
    wifi_.enabled = c.getBool("wifi", "enabled", false);
    wifi_.connected = c.getBool("wifi", "connected", false);
    wifi_.ssid = c.getString("wifi", "ssid");
    wifi_.strength = c.getInt("wifi", "strength", 0);

    bluetooth_.available = c.getBool("bluetooth", "available", false);
    bluetooth_.powered = c.getBool("bluetooth", "powered", false);
    // Connected devices are not restored: a headset that was connected last
    // session is very unlikely to be connected again this early, and claiming
    // otherwise would be a lie the user acts on.

    brightness_.available = c.getBool("brightness", "available", false);
    brightness_.current = c.getInt("brightness", "current", 0);
    brightness_.max = c.getInt("brightness", "max", 0);
    brightness_.device = c.getString("brightness", "device");
}

void StateCache::seed(const SystemBackends& backends) {
    if (!loaded_) { return; }
    if (backends.battery != nullptr && battery_.present) { backends.battery->seed(battery_); }
    if (backends.volume != nullptr && volume_.available) { backends.volume->seed(volume_); }
    if (backends.wifi != nullptr && wifi_.available) { backends.wifi->seed(wifi_); }
    if (backends.bluetooth != nullptr && bluetooth_.available) {
        backends.bluetooth->seed(bluetooth_);
    }
    if (backends.brightness != nullptr && brightness_.available && brightness_.max > 0) {
        backends.brightness->seed(brightness_);
    }
}

// -----------------------------------------------------------------------------
// Write
// -----------------------------------------------------------------------------
std::string StateCache::serialize() const {
    if (backends_ == nullptr) { return {}; }
    std::string out = "# qypr bar state cache — written automatically, safe to delete.\n";

    auto section = [&out](const char* name) {
        out += "\n[" + std::string(name) + "]\n";
    };
    auto putBool = [&out](const char* k, bool v) {
        out += std::string(k) + " = " + (v ? "true" : "false") + "\n";
    };
    auto putInt = [&out](const char* k, int v) {
        out += std::string(k) + " = " + std::to_string(v) + "\n";
    };
    auto putStr = [&out](const char* k, const std::string& v) {
        // A newline in a value would forge a second key on re-read; the values
        // here are device/SSID names, which can be arbitrary bytes.
        if (v.find('\n') != std::string::npos) { return; }
        out += std::string(k) + " = " + v + "\n";
    };

    if (backends_->battery != nullptr && backends_->battery->ready()) {
        const auto& s = backends_->battery->snapshot();
        section("battery");
        putBool("present", s.present);
        putInt("percentage", s.percentage);
    }
    if (backends_->volume != nullptr && backends_->volume->ready()) {
        const auto& s = backends_->volume->snapshot();
        section("volume");
        putBool("available", s.available);
        // Two decimals: enough to redraw the same icon tier and slider position,
        // and it keeps the file byte-stable against float jitter so the
        // unchanged-content check actually suppresses writes.
        char buf[32];
        std::snprintf(buf, sizeof buf, "%.2f", s.level);
        out += "level = " + std::string(buf) + "\n";
        putBool("muted", s.muted);
        putStr("sink", s.sinkName);
    }
    if (backends_->wifi != nullptr && backends_->wifi->ready()) {
        const auto& s = backends_->wifi->snapshot();
        section("wifi");
        putBool("available", s.available);
        putBool("enabled", s.enabled);
        putBool("connected", s.connected);
        putStr("ssid", s.ssid);
        putInt("strength", s.strength);
    }
    if (backends_->bluetooth != nullptr && backends_->bluetooth->ready()) {
        const auto& s = backends_->bluetooth->snapshot();
        section("bluetooth");
        putBool("available", s.available);
        putBool("powered", s.powered);
    }
    if (backends_->brightness != nullptr && backends_->brightness->ready()) {
        const auto& s = backends_->brightness->snapshot();
        section("brightness");
        putBool("available", s.available);
        putInt("current", s.current);
        putInt("max", s.max);
        putStr("device", s.device);
    }
    return out;
}

void StateCache::track(EventLoop& loop, const SystemBackends& backends) {
    loop_ = &loop;
    backends_ = &backends;
}

void StateCache::noteChanged() {
    if (loop_ == nullptr || timer_ >= 0) { return; }  // no tracking, or already armed
    timer_ = loop_->addTimer(kDebounceMs, /*repeat=*/false, [this] {
        timer_ = -1;
        flush();
    });
}

void StateCache::flush() {
    const std::string body = serialize();
    if (body.empty() || body == lastWritten_) { return; }

    // mkdir -p on the parent; an existing directory is not an error.
    const std::string dir = cacheDir();
    ::mkdir(dir.c_str(), 0700);

    // Write-and-rename: the bar can be killed (or the machine lose power) at any
    // moment, and a half-written cache that still parses would seed the next
    // start with nonsense. rename(2) within one filesystem is atomic, so a
    // reader sees either the old file or the new one.
    const std::string tmp = path_ + ".tmp";
    std::FILE* f = std::fopen(tmp.c_str(), "we");
    if (f == nullptr) { return; }
    const size_t written = std::fwrite(body.data(), 1, body.size(), f);
    const bool ok = written == body.size() && std::fflush(f) == 0;
    std::fclose(f);
    if (!ok) {
        ::unlink(tmp.c_str());
        return;
    }
    if (::rename(tmp.c_str(), path_.c_str()) != 0) {
        ::unlink(tmp.c_str());
        return;
    }
    lastWritten_ = body;
}

}  // namespace qypr
