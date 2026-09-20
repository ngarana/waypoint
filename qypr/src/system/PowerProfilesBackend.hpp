// PowerProfilesBackend.hpp - net.hadess.PowerProfiles (power-profiles-daemon).
//
// Reads the available power profiles (power-saver / balanced / performance) and
// the active one, pushes changes via PropertiesChanged, and switches the active
// profile. Standard cross-desktop service (the same one GNOME/KDE drive), not a
// private interface. Degrades to unavailable when the daemon is absent.
#pragma once

#include <functional>
#include <string>
#include <vector>

struct sd_bus_message;
struct sd_bus_slot;
struct sd_bus_error;

namespace qypr {

class SystemBus;

struct PowerProfilesSnapshot {
    bool available = false;
    std::string active;                 // e.g. "balanced"
    std::vector<std::string> profiles;  // daemon order: power-saver → performance

    bool operator==(const PowerProfilesSnapshot&) const = default;
};

class PowerProfilesBackend {
public:
    explicit PowerProfilesBackend(SystemBus& systemBus) : bus_(systemBus) {}
    ~PowerProfilesBackend();

    PowerProfilesBackend(const PowerProfilesBackend&) = delete;
    PowerProfilesBackend& operator=(const PowerProfilesBackend&) = delete;

    // One startup read + subscribe to PropertiesChanged. Returns false when the
    // daemon is not on the bus (the UI then omits the profile selector).
    bool start();

    const PowerProfilesSnapshot& snapshot() const { return snap_; }
    void setOnChange(std::function<void()> cb) { onChange_ = std::move(cb); }

    // Switch the active profile (async, optimistic).
    void setActiveProfile(const std::string& name);

private:
    static int onPropsChanged(sd_bus_message*, void*, sd_bus_error*);
    void refresh();  // re-read Profiles + ActiveProfile

    SystemBus& bus_;
    sd_bus_slot* slot_ = nullptr;
    PowerProfilesSnapshot snap_;
    std::function<void()> onChange_;
};

}  // namespace qypr
