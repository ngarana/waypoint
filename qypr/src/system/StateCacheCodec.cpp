// StateCacheCodec.cpp - Typed snapshot ↔ text conversion.
#include "system/StateCacheCodec.hpp"

#include <cstdio>
#include <string>

#include "core/Config.hpp"

namespace qypr {

void StateCacheCodec::load(const std::string& path) {
    Config c;
    if (!c.load(path)) { return; }
    loaded_ = true;

    battery_.percentage = c.getInt("battery", "percentage", 0);
    battery_.present = c.getBool("battery", "present", false);

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

    brightness_.available = c.getBool("brightness", "available", false);
    brightness_.current = c.getInt("brightness", "current", 0);
    brightness_.max = c.getInt("brightness", "max", 0);
    brightness_.device = c.getString("brightness", "device");
}

void StateCacheCodec::seed(const SystemBackends& backends) const {
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

std::string StateCacheCodec::serialize(const SystemBackends& backends) const {
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
        if (v.find('\n') != std::string::npos) { return; }
        out += std::string(k) + " = " + v + "\n";
    };

    if (backends.battery != nullptr && backends.battery->ready()) {
        const auto& s = backends.battery->snapshot();
        section("battery");
        putBool("present", s.present);
        putInt("percentage", s.percentage);
    }
    if (backends.volume != nullptr && backends.volume->ready()) {
        const auto& s = backends.volume->snapshot();
        section("volume");
        putBool("available", s.available);
        char buf[32];
        std::snprintf(buf, sizeof buf, "%.2f", s.level);
        out += "level = " + std::string(buf) + "\n";
        putBool("muted", s.muted);
        putStr("sink", s.sinkName);
    }
    if (backends.wifi != nullptr && backends.wifi->ready()) {
        const auto& s = backends.wifi->snapshot();
        section("wifi");
        putBool("available", s.available);
        putBool("enabled", s.enabled);
        putBool("connected", s.connected);
        putStr("ssid", s.ssid);
        putInt("strength", s.strength);
    }
    if (backends.bluetooth != nullptr && backends.bluetooth->ready()) {
        const auto& s = backends.bluetooth->snapshot();
        section("bluetooth");
        putBool("available", s.available);
        putBool("powered", s.powered);
    }
    if (backends.brightness != nullptr && backends.brightness->ready()) {
        const auto& s = backends.brightness->snapshot();
        section("brightness");
        putBool("available", s.available);
        putInt("current", s.current);
        putInt("max", s.max);
        putStr("device", s.device);
    }
    return out;
}

}  // namespace qypr
