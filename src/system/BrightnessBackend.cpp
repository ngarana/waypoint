// BrightnessBackend.cpp - sysfs read, udev push, logind write.
#include "system/BrightnessBackend.hpp"

#include <libudev.h>
#include <systemd/sd-bus.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>

#include "core/EventLoop.hpp"
#include "system/SystemBus.hpp"

namespace qypr {

namespace {
constexpr const char* kBacklightDir = "/sys/class/backlight";

// logind: the calling session's owner may set brightness unprivileged.
constexpr const char* kLogind = "org.freedesktop.login1";
constexpr const char* kSessionAuto = "/org/freedesktop/login1/session/auto";
constexpr const char* kSessionIface = "org.freedesktop.login1.Session";

int readIntFile(const std::string& path) {
    std::ifstream f(path);
    int v = -1;
    f >> v;
    return f ? v : -1;
}
}  // namespace

BrightnessBackend::BrightnessBackend(EventLoop& loop, SystemBus& bus) : loop_(loop), bus_(bus) {}

BrightnessBackend::~BrightnessBackend() {
    if (monFd_ >= 0) { loop_.removeFd(monFd_); }
    if (mon_ != nullptr) { udev_monitor_unref(mon_); }
    if (udev_ != nullptr) { udev_unref(udev_); }
}

bool BrightnessBackend::start() {
    // Pick the backlight device with the largest range (usually the panel;
    // skips dim keyboard/LED-style entries when both exist).
    std::error_code ec;
    std::filesystem::directory_iterator it(kBacklightDir, ec);
    std::filesystem::directory_iterator const end;
    int bestMax = 0;
    for (; it != end && !ec; it.increment(ec)) {
        const std::string base = it->path().string();
        int const max = readIntFile(base + "/max_brightness");
        if (max > bestMax) {
            bestMax = max;
            snap_.device = it->path().filename().string();
        }
    }
    if (snap_.device.empty() || !readSysfs()) {
        std::fprintf(stderr, "qypr: no backlight device; brightness indicator disabled\n");
        snap_.available = false;
        notifyReady();  // hide the placeholder
        return false;
    }

    // Push: the kernel emits a "change" uevent whenever brightness changes
    // (keys, other tools) — one netlink fd in the loop, zero polling.
    udev_ = udev_new();
    if (udev_ != nullptr) {
        mon_ = udev_monitor_new_from_netlink(udev_, "udev");
        if (mon_ != nullptr) {
            udev_monitor_filter_add_match_subsystem_devtype(mon_, "backlight", nullptr);
            udev_monitor_enable_receiving(mon_);
            monFd_ = udev_monitor_get_fd(mon_);
            loop_.addFd(monFd_, [this](uint32_t) { onUdevEvent(); });
        }
    }
    if (monFd_ < 0) {
        std::fprintf(stderr, "qypr: udev monitor unavailable; external brightness "
                             "changes will not refresh the indicator\n");
    }

    notifyReady();
    return true;
}

bool BrightnessBackend::readSysfs() {
    const std::string base = std::string(kBacklightDir) + "/" + snap_.device;
    int const cur = readIntFile(base + "/brightness");
    int const max = readIntFile(base + "/max_brightness");
    if (cur < 0 || max <= 0) {
        snap_.available = false;
        return false;
    }
    snap_.current = cur;
    snap_.max = max;
    snap_.available = true;
    return true;
}

void BrightnessBackend::onUdevEvent() {
    // Drain all queued events; one re-read covers them.
    bool sawEvent = false;
    while (udev_device* dev = udev_monitor_receive_device(mon_)) {
        sawEvent = true;
        udev_device_unref(dev);
    }
    if (!sawEvent) { return; }
    if (readSysfs()) { notifyReady(); }
}

void BrightnessBackend::setFraction(double frac) {
    if (!snap_.available || !bus_.available()) { return; }

    frac = std::clamp(frac, 0.0, 1.0);
    // Never fully off — a black panel on a lockscreen looks like a crash.
    int const raw = std::max(1, static_cast<int>(std::lround(frac * snap_.max)));

    // Optimistic local update for immediate slider/icon feedback; the udev
    // change event confirms (or corrects) it.
    snap_.current = raw;
    notifyReady();

    // Fire-and-forget async call: never blocks the render path.
    sd_bus_call_method_async(bus_.get(), nullptr, kLogind, kSessionAuto, kSessionIface,
                             "SetBrightness", nullptr, nullptr, "ssu", "backlight",
                             snap_.device.c_str(), static_cast<uint32_t>(raw));
}

}  // namespace qypr
