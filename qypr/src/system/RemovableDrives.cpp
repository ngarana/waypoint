// RemovableDrives.cpp - UDisks2 object tree → removable drives.
#include "system/RemovableDrives.hpp"

#include <algorithm>
#include <array>
#include <cstdio>
#include <string_view>

namespace qypr {

namespace {

bool isHotplugBus(std::string_view bus) {
    return bus == "usb" || bus == "sdio" || bus == "ieee1394";
}

// Mount points the running system depends on.
bool isSystemMount(std::string_view mp) {
    constexpr std::array<std::string_view, 7> kSystem{"/",    "/boot", "/efi", "/usr",
                                                      "/var", "/home", "/opt"};
    if (std::ranges::find(kSystem, mp) != kSystem.end()) { return true; }
    return mp.starts_with("/boot/") || mp.starts_with("/usr/") || mp.starts_with("/var/");
}

std::string trim(const std::string& s) {
    const auto first = s.find_first_not_of(" \t");
    if (first == std::string::npos) { return {}; }
    const auto last = s.find_last_not_of(" \t");
    return s.substr(first, last - first + 1);
}

std::string driveName(const UDisksDrive& d) {
    const std::string vendor = trim(d.vendor);
    const std::string model = trim(d.model);
    if (vendor.empty()) { return model.empty() ? std::string("Drive") : model; }
    if (model.empty() || model.starts_with(vendor)) { return model.empty() ? vendor : model; }
    return vendor + " " + model;
}

std::string volumeLabel(const UDisksBlock& b) {
    if (!trim(b.label).empty()) { return trim(b.label); }
    if (!trim(b.hintName).empty()) { return trim(b.hintName); }
    const auto slash = b.device.rfind('/');
    return slash == std::string::npos ? b.device : b.device.substr(slash + 1);
}

const UDisksBlock* cleartextOf(const UDisksObjects& objects, const UDisksBlock& container) {
    for (const auto& b : objects.blocks) {
        if (b.cryptoBacking == container.path) { return &b; }
    }
    return nullptr;
}

}  // namespace

bool RemovableDrive::anyMounted() const {
    return std::ranges::any_of(volumes, [](const RemovableVolume& v) { return v.mounted(); });
}

std::string RemovableDrive::title() const {
    if (volumes.size() == 1 && !volumes.front().label.empty()) { return volumes.front().label; }
    return name;
}

const RemovableDrive* RemovableDrivesSnapshot::find(const std::string& path) const {
    const auto it = std::ranges::find(drives, path, &RemovableDrive::path);
    return it == drives.end() ? nullptr : &*it;
}

std::vector<RemovableDrive> reduceRemovableDrives(const UDisksObjects& objects) {
    std::vector<std::pair<std::string, RemovableDrive>> keyed;  // sort key, drive
    for (const auto& d : objects.drives) {
        if (!(d.removable || d.mediaRemovable || d.ejectable || isHotplugBus(d.connectionBus))) {
            continue;
        }
        std::vector<const UDisksBlock*> blocks;
        for (const auto& b : objects.blocks) {
            if (b.drive == d.path) { blocks.push_back(&b); }
        }
        std::ranges::sort(blocks, {}, &UDisksBlock::path);
        // A card reader or optical drive without media has no blocks worth
        // listing; a drive udev asked desktops to ignore is ignored.
        if (blocks.empty() ||
            std::ranges::all_of(blocks, [](const UDisksBlock* b) { return b->hintIgnore; })) {
            continue;
        }

        RemovableDrive out;
        out.path = d.path;
        out.name = driveName(d);
        out.connectionBus = d.connectionBus;
        out.size = d.size;
        out.canPowerOff = d.canPowerOff;
        out.ejectable = d.ejectable;
        out.optical = d.optical;
        bool system = false;
        for (const UDisksBlock* b : blocks) {
            if (b->configured) { out.fixed = true; }
            if (b->hintIgnore) { continue; }
            RemovableVolume v;
            v.label = volumeLabel(*b);
            v.device = b->device;
            v.size = b->size;
            if (b->hasFilesystem) {
                v.fsType = b->idType;
                v.filesystemPath = b->path;
                v.mountPoints = b->mountPoints;
            } else if (b->hasEncrypted || b->idUsage == "crypto") {
                v.encryptedPath = b->path;
                const UDisksBlock* clear = cleartextOf(objects, *b);
                v.locked = clear == nullptr;
                if (clear != nullptr) {
                    if (clear->configured) { out.fixed = true; }
                    if (!trim(clear->label).empty()) { v.label = trim(clear->label); }
                    if (clear->hasFilesystem) {
                        v.fsType = clear->idType;
                        v.filesystemPath = clear->path;
                        v.mountPoints = clear->mountPoints;
                    }
                }
            } else {
                continue;  // partition table, swap, extended partition, unknown
            }
            if (std::ranges::any_of(v.mountPoints, isSystemMount)) { system = true; }
            out.volumes.push_back(std::move(v));
        }
        if (system || out.volumes.empty()) { continue; }
        // Hot-plugged before fixed, then UDisks2's own order (bus, then name).
        std::string key = (out.fixed ? "1" : "0") + d.sortKey + d.path;
        keyed.emplace_back(std::move(key), std::move(out));
    }
    std::ranges::sort(keyed, {}, &std::pair<std::string, RemovableDrive>::first);
    std::vector<RemovableDrive> drives;
    drives.reserve(keyed.size());
    for (auto& [key, drive] : keyed) { drives.push_back(std::move(drive)); }
    return drives;
}

std::string formatDriveSize(uint64_t bytes) {
    constexpr std::array<const char*, 5> kUnits{"B", "kB", "MB", "GB", "TB"};
    auto value = static_cast<double>(bytes);
    size_t unit = 0;
    while (value >= 1000.0 && unit + 1 < kUnits.size()) {
        value /= 1000.0;
        ++unit;
    }
    std::array<char, 32> buf{};
    if (unit == 0 || value >= 10.0) {
        std::snprintf(buf.data(), buf.size(), "%.0f %s", value, kUnits.at(unit));
    } else {
        std::snprintf(buf.data(), buf.size(), "%.1f %s", value, kUnits.at(unit));
    }
    return buf.data();
}

std::string_view describeConnection(const RemovableDrive& drive) {
    if (drive.optical) { return "Disc"; }
    if (drive.connectionBus == "sdio") { return "SD card"; }
    if (drive.connectionBus == "usb") { return "USB drive"; }
    return "Drive";
}

}  // namespace qypr
