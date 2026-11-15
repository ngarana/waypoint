// RemovableDevicesIndicator.cpp - Removable drives module.
#include "ui/indicators/RemovableDevicesIndicator.hpp"

#include <algorithm>
#include <utility>

#include "system/DesktopIndex.hpp"
#include "system/RemovableDrives.hpp"
#include "system/UDisksBackend.hpp"
#include "ui/Theme.hpp"
#include "ui/statusbar/IndicatorRegistry.hpp"
#include "ui/statusbar/ListFlyout.hpp"
#include "ui/statusbar/MenuSource.hpp"

namespace qypr {

namespace {

// Nerd Font v3 Material Design glyphs (nf-md-*, MDI codepoints).
constexpr const char* kGlyphUsb = "\U000F0553";     // usb
constexpr const char* kGlyphDisk = "\U000F02CA";    // harddisk
constexpr const char* kGlyphSd = "\U000F0479";      // sd
constexpr const char* kGlyphDisc = "\U000F0025";    // album (a disc)
constexpr const char* kGlyphEject = "\U000F01EA";   // eject
constexpr const char* kGlyphAlert = "\U000F0028";   // alert_circle
constexpr const char* kGlyphFolder = "\U000F0770";  // folder_open
constexpr const char* kGlyphLock = "\U000F033E";    // lock

const char* glyphFor(const RemovableDrive& d) {
    if (d.optical) { return kGlyphDisc; }
    if (d.connectionBus == "sdio") { return kGlyphSd; }
    return d.fixed ? kGlyphDisk : kGlyphUsb;
}

std::string shellQuote(const std::string& s) {
    std::string out = "'";
    for (const char c : s) {
        if (c == '\'') {
            out += "'\\''";
        } else {
            out += c;
        }
    }
    return out + "'";
}

void openPath(EventLoop* loop, const std::string& where) {
    if (loop != nullptr && !where.empty()) {
        launchDetached(*loop, "xdg-open " + shellQuote(where), /*terminal=*/false);
    }
}

// Open a volume: straight to the file manager when mounted, else mount first.
std::function<void()> openAction(UDisksBackend* backend, EventLoop* loop,
                                 const std::string& drivePath, const RemovableVolume& v) {
    if (v.mounted()) {
        return [loop, where = v.mountPoints.front()] {
            openPath(loop, where);
        };
    }
    return [backend, loop, drivePath, fs = v.filesystemPath] {
        if (backend != nullptr) {
            backend->mount(drivePath, fs,
                           [loop](const std::string& where) { openPath(loop, where); });
        }
    };
}

FlyoutAction openButton(UDisksBackend* backend, EventLoop* loop, const std::string& drivePath,
                        const RemovableVolume& v, bool busy) {
    FlyoutAction a;
    a.label = "Open";
    a.run = openAction(backend, loop, drivePath, v);
    a.enabled = !busy;
    a.closes = v.mounted();  // hands off to the file manager; a mount stays to show its result
    return a;
}

std::string volumeStatus(const RemovableVolume& v) {
    if (v.locked) { return "Encrypted · locked"; }
    if (v.mounted()) { return "Mounted at " + v.mountPoints.front(); }
    return "Not mounted";
}

bool shown(const RemovableDrive& d) {
    return !d.fixed || d.op.phase != DriveOperation::Phase::None;
}

std::string joined(const std::vector<std::string>& items) {
    std::string out;
    for (const auto& s : items) {
        if (!out.empty()) { out += ", "; }
        out += s;
    }
    return out;
}

}  // namespace

RemovableDevicesIndicator::RemovableDevicesIndicator(const SystemBackends& backends)
    : StatusIndicator("removable-devices", Zone::Right, 260),
      backend_(backends.udisks),
      loop_(backends.loop),
      tray_(backends, "removable-devices") {
    refreshVisibility();
}

const RemovableDrivesSnapshot* RemovableDevicesIndicator::snap() const {
    return backend_ != nullptr ? &backend_->snapshot() : nullptr;
}

void RemovableDevicesIndicator::onBackendUpdate() {
    refreshVisibility();
}

void RemovableDevicesIndicator::refreshVisibility() {
    const auto* s = snap();
    visible = s != nullptr && std::ranges::any_of(s->drives, shown);
}

std::string RemovableDevicesIndicator::driveStatus(const RemovableDrive& drive) {
    using Phase = DriveOperation::Phase;
    switch (drive.op.phase) {
        case Phase::Mounting:
            return "Mounting…";
        case Phase::Unmounting:
            return "Unmounting…";
        case Phase::Locking:
            return "Locking…";
        case Phase::PoweringOff:
            return "Powering off…";
        case Phase::Done:
            return "Safe to unplug";
        case Phase::Failed:
            return drive.op.blockers.empty()
                       ? drive.op.error
                       : drive.op.error + " — used by " + joined(drive.op.blockers);
        case Phase::None:
            break;
    }
    const auto mounted = std::ranges::count_if(drive.volumes, &RemovableVolume::mounted);
    if (drive.volumes.size() == 1) { return volumeStatus(drive.volumes.front()); }
    if (mounted == 0) { return "Not mounted"; }
    return std::to_string(mounted) + " of " + std::to_string(drive.volumes.size()) +
           " volumes mounted";
}

std::string RemovableDevicesIndicator::icon() const {
    const auto* s = snap();
    if (s == nullptr) { return kGlyphUsb; }
    for (const auto& d : s->drives) {
        if (d.op.phase == DriveOperation::Phase::Failed) { return kGlyphAlert; }
    }
    for (const auto& d : s->drives) {
        if (d.op.busy()) { return kGlyphEject; }
    }
    for (const auto& d : s->drives) {
        if (shown(d)) { return glyphFor(d); }
    }
    return kGlyphUsb;
}

std::string RemovableDevicesIndicator::themedIcon() const {
    const auto* s = snap();
    if (s != nullptr) {
        for (const auto& d : s->drives) {
            if (d.op.phase == DriveOperation::Phase::Failed) { return "dialog-warning-symbolic"; }
            if (d.op.busy()) { return "media-eject-symbolic"; }
        }
    }
    return "drive-removable-media-symbolic";
}

Color RemovableDevicesIndicator::iconColor() const {
    const auto* s = snap();
    if (s != nullptr && std::ranges::any_of(s->drives, [](const RemovableDrive& d) {
            return d.op.phase == DriveOperation::Phase::Failed;
        })) {
        return theme().colors.error;
    }
    return StatusIndicator::iconColor();
}

std::string RemovableDevicesIndicator::tooltip() const {
    const auto* s = snap();
    if (s == nullptr || s->drives.empty()) { return "No removable drives"; }
    if (s->drives.size() == 1) {
        const auto& d = s->drives.front();
        return d.title() + " · " + driveStatus(d);
    }
    std::string out = std::to_string(s->drives.size()) + " drives";
    for (const auto& d : s->drives) {
        if (d.op.phase != DriveOperation::Phase::None) {
            out += "\n" + d.title() + ": " + driveStatus(d);
        }
    }
    return out;
}

bool RemovableDevicesIndicator::hasDetailedView() const {
    const auto* s = snap();
    return s != nullptr && !s->drives.empty();
}

FlyoutModel RemovableDevicesIndicator::buildFlyout(const RemovableDrivesSnapshot& snap,
                                                   UDisksBackend* backend, EventLoop* loop) {
    FlyoutModel m;
    m.title = "Drives";
    m.empty = snap.available ? "No removable drives" : "UDisks2 is not available";
    for (const auto& d : snap.drives) {
        const bool busy = d.op.busy();
        // One volume named like the drive: a single row carries both.
        const bool single = d.volumes.size() == 1;

        FlyoutRow head;
        head.glyph = glyphFor(d);
        head.title = d.title();
        head.subtitle = std::string(describeConnection(d)) + " · " + formatDriveSize(d.size);
        head.busy = busy;
        if (busy || d.op.phase == DriveOperation::Phase::Done) {
            head.subtitle = driveStatus(d);
            head.tone = d.op.phase == DriveOperation::Phase::Done ? FlyoutRow::Tone::Success
                                                                  : FlyoutRow::Tone::Subtle;
        } else if (single) {
            head.subtitle += " · " + volumeStatus(d.volumes.front());
        }
        if (single && !d.volumes.front().locked && d.op.phase != DriveOperation::Phase::Done) {
            head.actions.push_back(openButton(backend, loop, d.path, d.volumes.front(), busy));
        }
        if (d.op.phase != DriveOperation::Phase::Done) {
            head.actions.push_back({.label = "Eject",
                                    .run =
                                        [backend, path = d.path] {
                                            if (backend != nullptr) { backend->safelyRemove(path); }
                                        },
                                    .enabled = !busy});
        }
        m.rows.push_back(std::move(head));

        if (d.op.phase == DriveOperation::Phase::Failed) {
            FlyoutRow err;
            err.glyph = kGlyphAlert;
            err.title = d.op.error;
            err.tone = FlyoutRow::Tone::Error;
            if (!d.op.blockers.empty()) { err.subtitle = "Used by " + joined(d.op.blockers); }
            err.actions.push_back({.label = "Dismiss", .run = [backend, path = d.path] {
                                       if (backend != nullptr) { backend->clearResult(path); }
                                   }});
            m.rows.push_back(std::move(err));
        }

        if (single) { continue; }
        for (const auto& v : d.volumes) {
            FlyoutRow row;
            row.glyph = v.locked ? kGlyphLock : kGlyphFolder;
            row.title = v.label;
            row.subtitle = volumeStatus(v);
            if (!v.locked && !v.filesystemPath.empty()) {
                row.actions.push_back(openButton(backend, loop, d.path, v, busy));
            }
            m.rows.push_back(std::move(row));
        }
    }
    return m;
}

std::unique_ptr<DetailedPopover> RemovableDevicesIndicator::createDetailedView() {
    if (backend_ == nullptr) { return nullptr; }
    return std::make_unique<ListFlyout>([backend = backend_, loop = loop_] {
        return buildFlyout(backend->snapshot(), backend, loop);
    });
}

bool RemovableDevicesIndicator::hasContextMenu() const {
    return hasDetailedView() || tray_.bound();
}

std::unique_ptr<DetailedPopover> RemovableDevicesIndicator::createContextMenu(double x, double y) {
    auto native = std::make_unique<NativeMenu>();
    if (const auto* s = snap()) {
        for (const auto& d : s->drives) {
            const bool busy = d.op.busy();
            for (const auto& v : d.volumes) {
                if (v.locked || v.filesystemPath.empty()) { continue; }
                native->add("Open " + v.label, openAction(backend_, loop_, d.path, v), !busy);
            }
            if (d.op.phase != DriveOperation::Phase::Done) {
                native->add(
                    "Eject " + d.title(),
                    [backend = backend_, path = d.path] { backend->safelyRemove(path); }, !busy);
            }
        }
    }
    return tray_.contextMenu(x, y, std::move(native), "Drives");
}

OverflowEntry RemovableDevicesIndicator::overflowEntry() const {
    const auto* s = snap();
    if (visible || s == nullptr || s->drives.empty()) { return {}; }
    OverflowEntry e;
    e.id = id();
    e.iconName = "drive-harddisk-symbolic";
    e.glyph = kGlyphDisk;
    if (s->drives.size() == 1) {
        const auto& d = s->drives.front();
        e.title = d.title();
        e.subtitle = std::string(describeConnection(d)) + " · " + driveStatus(d);
    } else {
        e.title = "Drives";
        e.subtitle = std::to_string(s->drives.size()) + " drives attached";
    }
    return e;
}

// The old ids each built a separate copy of a polling module; they now name
// this one. The registry keeps only the first instance per id.
using UsbIndicator = RemovableDevicesIndicator;
using UsbEjectIndicator = RemovableDevicesIndicator;
REGISTER_SESSION_INDICATOR("removable-devices", Zone::Right, 260, RemovableDevicesIndicator)
REGISTER_SESSION_INDICATOR("usb", Zone::Right, 260, UsbIndicator)
REGISTER_SESSION_INDICATOR("usb-eject", Zone::Right, 260, UsbEjectIndicator)

}  // namespace qypr
