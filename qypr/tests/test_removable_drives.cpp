// test_removable_drives.cpp - UDisks2 reduction, safe-removal plan, module.
#include "test_framework.hpp"

#include <xkbcommon/xkbcommon-keysyms.h>

#include "system/RemovableDrives.hpp"
#include "system/UDisksBackend.hpp"
#include "ui/indicators/RemovableDevicesIndicator.hpp"
#include "ui/statusbar/ListFlyout.hpp"

namespace {

// Object paths no real UDisks2 publishes (see OfflineSystemBus below).
constexpr const char* kWd = "/org/freedesktop/UDisks2/drives/qypr_test_WDC_WD40NPZZ";
constexpr const char* kStick = "/org/freedesktop/UDisks2/drives/qypr_test_SanDisk_Ultra";
constexpr const char* kNvme = "/org/freedesktop/UDisks2/drives/qypr_test_WD_PC_SN740";
constexpr const char* kBlocks = "/org/freedesktop/UDisks2/block_devices/qypr_test_";

qypr::UDisksDrive drive(const std::string& path, const std::string& bus, bool removable) {
    qypr::UDisksDrive d;
    d.path = path;
    d.connectionBus = bus;
    d.removable = removable;
    d.canPowerOff = removable;
    d.size = 4000787030016;
    d.sortKey = "01hotplug/" + path;
    return d;
}

qypr::UDisksBlock block(const std::string& name, const std::string& drivePath) {
    qypr::UDisksBlock b;
    b.path = std::string(kBlocks) + name;
    b.drive = drivePath;
    b.device = "/dev/" + name;
    return b;
}

qypr::UDisksBlock filesystem(const std::string& name, const std::string& drivePath,
                             const std::string& label, std::vector<std::string> mounts) {
    auto b = block(name, drivePath);
    b.label = label;
    b.idUsage = "filesystem";
    b.idType = "exfat";
    b.hasFilesystem = true;
    b.mountPoints = std::move(mounts);
    return b;
}

// The machine this was written on: an fstab-mounted USB disk, a hot-plugged
// stick, and an internal NVMe disk.
qypr::UDisksObjects sampleTree() {
    qypr::UDisksObjects o;
    auto wd = drive(kWd, "usb", true);
    wd.model = "WDC WD40NPZZ-00PDPT0";
    wd.sortKey = "00coldplug/12removable/sd____a";
    auto stick = drive(kStick, "usb", true);
    stick.vendor = "SanDisk";
    stick.model = "Ultra";
    stick.size = 32'000'000'000;
    auto nvme = drive(kNvme, "", false);
    nvme.model = "WD PC SN740";
    o.drives = {nvme, wd, stick};

    auto sda = block("sda", kWd);  // partition table only
    auto sda1 = filesystem("sda1", kWd, "SharedDrive", {"/mnt/shared"});
    sda1.configured = true;  // fstab
    auto sdb1 = filesystem("sdb1", kStick, "STICK", {});
    auto root = filesystem("nvme0n1p2", kNvme, "root", {"/"});
    o.blocks = {sda, sda1, sdb1, root};
    return o;
}

// sd_bus_open_system() is not mocked: under test it opens the machine's real
// system bus. A test must never ask the real UDisks2 to unmount or power off
// anything, so the connection is dropped before the backend sees it and every
// call fails fast, as it would with UDisks2 absent.
struct OfflineSystemBus : qypr::SystemBus {
    explicit OfflineSystemBus(qypr::EventLoop& loop) : SystemBus(loop) { teardown(); }
};

}  // namespace

TEST(RemovableDrivesFollowUDisksNotSysfs) {
    const auto drives = qypr::reduceRemovableDrives(sampleTree());
    // The internal disk is left out; the hot-plugged stick sorts first.
    EXPECT_EQ(drives.size(), 2U);
    EXPECT_EQ(drives[0].path, std::string(kStick));
    EXPECT_FALSE(drives[0].fixed);
    EXPECT_EQ(drives[0].name, std::string("SanDisk Ultra"));
    EXPECT_EQ(drives[0].title(), std::string("STICK"));
    EXPECT_FALSE(drives[0].anyMounted());

    // The WD disk: sysfs calls it non-removable, UDisks2 removable over USB.
    // Its fstab entry makes it fixed (passive), and the bare partition-table
    // block is not a volume.
    const auto& wd = drives[1];
    EXPECT_TRUE(wd.fixed);
    EXPECT_EQ(wd.volumes.size(), 1U);
    EXPECT_EQ(wd.title(), std::string("SharedDrive"));
    EXPECT_TRUE(wd.volumes[0].mounted());
    EXPECT_EQ(wd.volumes[0].filesystemPath, std::string(kBlocks) + "sda1");
    EXPECT_EQ(std::string(qypr::describeConnection(wd)), std::string("USB drive"));
}

TEST(RemovableDrivesSkipEmptyIgnoredAndSystemDrives) {
    qypr::UDisksObjects o;
    auto reader = drive("/drives/reader", "usb", true);  // card reader, no card
    reader.mediaRemovable = true;
    auto ignored = drive("/drives/ignored", "usb", true);
    auto boot = drive("/drives/boot", "usb", true);  // running system on a stick
    o.drives = {reader, ignored, boot};
    auto ig = filesystem("sdc1", "/drives/ignored", "EFI", {});
    ig.hintIgnore = true;
    o.blocks = {ig, filesystem("sdd1", "/drives/boot", "root", {"/"}),
                filesystem("sdd2", "/drives/boot", "data", {})};
    EXPECT_TRUE(qypr::reduceRemovableDrives(o).empty());
}

TEST(RemovableDrivesResolveEncryptedVolumes) {
    qypr::UDisksObjects o;
    o.drives = {drive("/drives/luks", "usb", true)};
    auto locked = block("sde1", "/drives/luks");
    locked.idUsage = "crypto";
    locked.idType = "crypto_LUKS";
    locked.hasEncrypted = true;
    auto container = block("sde2", "/drives/luks");
    container.idUsage = "crypto";
    container.hasEncrypted = true;
    // The cleartext device hangs off no drive: it is found through its
    // CryptoBackingDevice.
    auto clear = filesystem("dm_2d0", "/", "Backup", {"/run/media/me/Backup"});
    clear.cryptoBacking = container.path;
    o.blocks = {locked, container, clear};

    const auto drives = qypr::reduceRemovableDrives(o);
    EXPECT_EQ(drives.size(), 1U);
    const auto& v = drives[0].volumes;
    EXPECT_EQ(v.size(), 2U);
    EXPECT_TRUE(v[0].locked);
    EXPECT_TRUE(v[0].filesystemPath.empty());
    EXPECT_FALSE(v[1].locked);
    EXPECT_EQ(v[1].label, std::string("Backup"));
    EXPECT_EQ(v[1].filesystemPath, clear.path);
    EXPECT_EQ(v[1].encryptedPath, container.path);
    EXPECT_TRUE(v[1].mounted());

    // Unmount the open volume, lock its container, then power the drive off.
    const auto plan = qypr::UDisksBackend::planRemoval(drives[0]);
    EXPECT_EQ(plan.size(), 3U);
    EXPECT_TRUE(plan[0].kind == qypr::UDisksBackend::Step::Kind::Unmount);
    EXPECT_EQ(plan[0].object, clear.path);
    EXPECT_EQ(plan[0].mountPoint, std::string("/run/media/me/Backup"));
    EXPECT_TRUE(plan[1].kind == qypr::UDisksBackend::Step::Kind::Lock);
    EXPECT_EQ(plan[1].object, container.path);
    EXPECT_TRUE(plan[2].kind == qypr::UDisksBackend::Step::Kind::PowerOff);

    // Media that can only be ejected (a card in a reader) is ejected.
    auto card = drives[0];
    card.canPowerOff = false;
    card.ejectable = true;
    card.volumes.clear();
    const auto ejectPlan = qypr::UDisksBackend::planRemoval(card);
    EXPECT_EQ(ejectPlan.size(), 1U);
    EXPECT_TRUE(ejectPlan[0].kind == qypr::UDisksBackend::Step::Kind::Eject);
}

TEST(RemovableDrivesSizesAndErrors) {
    EXPECT_EQ(qypr::formatDriveSize(4000787030016), std::string("4.0 TB"));
    EXPECT_EQ(qypr::formatDriveSize(512110190592), std::string("512 GB"));
    EXPECT_EQ(qypr::formatDriveSize(0), std::string("0 B"));
    using B = qypr::UDisksBackend;
    EXPECT_EQ(B::describeError("org.freedesktop.UDisks2.Error.DeviceBusy", "target is busy"),
              std::string("Still in use"));
    EXPECT_EQ(B::describeError("org.freedesktop.UDisks2.Error.NotAuthorizedDismissed", ""),
              std::string("Authentication was cancelled"));
    EXPECT_EQ(B::describeError("org.freedesktop.UDisks2.Error.NotAuthorizedCanObtain", ""),
              std::string("Not allowed"));
    EXPECT_EQ(B::describeError("org.freedesktop.UDisks2.Error.Failed", "Error: x\ndetail"),
              std::string("Error: x"));
}

TEST(BusyDriveNamesTheProcessesUsingIt) {
    namespace fs = std::filesystem;
    const fs::path proc = fs::temp_directory_path() / ("qypr-proc-" + std::to_string(::getpid()));
    fs::remove_all(proc);
    auto process = [&](const std::string& pid, const std::string& comm) {
        fs::create_directories(proc / pid / "fd");
        std::ofstream(proc / pid / "comm") << comm << "\n";
        return proc / pid;
    };
    fs::create_directory_symlink("/mnt/x/photos", process("101", "zsh") / "cwd");
    fs::create_symlink("/mnt/x", process("202", "dolphin") / "fd" / "7");
    fs::create_symlink("/mnt/xy/file", process("303", "other") / "fd" / "1");  // sibling path
    fs::create_symlink("/home/me", process("404", "idle") / "cwd");
    process("self", "ignored");

    const auto users = qypr::UDisksBackend::processesUsing("/mnt/x", proc.string());
    EXPECT_EQ(users.size(), 2U);
    EXPECT_EQ(users[0], std::string("dolphin"));
    EXPECT_EQ(users[1], std::string("zsh"));
    EXPECT_TRUE(qypr::UDisksBackend::processesUsing("/", proc.string()).empty());
    fs::remove_all(proc);
}

TEST(RemovableDevicesModuleIsPassiveForFixedDrives) {
    qypr::EventLoop loop;
    OfflineSystemBus system(loop);
    EXPECT_FALSE(system.available());
    qypr::UDisksBackend udisks(loop, system);
    qypr::SystemBackends backends{};
    backends.udisks = &udisks;
    qypr::RemovableDevicesIndicator ind(backends);
    EXPECT_FALSE(ind.visible);
    EXPECT_FALSE(ind.hasDetailedView());
    EXPECT_EQ(ind.tooltip(), std::string("No removable drives"));

    // Only the fstab disk: off the bar, listed in the overflow.
    auto all = qypr::reduceRemovableDrives(sampleTree());
    udisks.snap_.available = true;
    udisks.drives_ = {all[1]};
    udisks.publish();
    ind.onBackendUpdate();
    EXPECT_FALSE(ind.visible);
    EXPECT_TRUE(ind.hasDetailedView());
    const auto entry = ind.overflowEntry();
    EXPECT_EQ(entry.id, std::string("removable-devices"));
    EXPECT_EQ(entry.title, std::string("SharedDrive"));
    EXPECT_EQ(entry.subtitle, std::string("USB drive · Mounted at /mnt/shared"));

    // A stick arrives: on the bar, no overflow row.
    udisks.drives_ = all;
    udisks.publish();
    ind.onBackendUpdate();
    EXPECT_TRUE(ind.visible);
    EXPECT_TRUE(ind.overflowEntry().id.empty());
    EXPECT_EQ(ind.tooltip(), std::string("2 drives"));

    // Ejecting the fixed disk with UDisks2 unreachable fails on the first
    // step, and the failure surfaces on the bar in the error colour.
    udisks.drives_ = {all[1]};
    udisks.publish();
    udisks.safelyRemove(kWd);
    ind.onBackendUpdate();
    EXPECT_TRUE(ind.visible);
    const auto* wd = udisks.snapshot().find(kWd);
    EXPECT_TRUE(wd != nullptr);
    EXPECT_TRUE(wd->op.phase == qypr::DriveOperation::Phase::Failed);
    EXPECT_EQ(wd->op.error, std::string("Couldn't reach UDisks2"));
    EXPECT_EQ(ind.icon(), std::string("\U000F0028"));
    EXPECT_EQ(ind.themedIcon(), std::string("dialog-warning-symbolic"));

    // Dismissing the result returns it to the overflow.
    udisks.clearResult(kWd);
    ind.onBackendUpdate();
    EXPECT_FALSE(ind.visible);

    // A drive that goes away takes its result with it.
    udisks.safelyRemove(kWd);
    udisks.apply({});
    EXPECT_TRUE(udisks.ops_.empty());
    EXPECT_TRUE(udisks.snapshot().drives.empty());
}

TEST(RemovableDevicesFlyoutRowsAndButtons) {
    qypr::EventLoop loop;
    OfflineSystemBus system(loop);
    EXPECT_FALSE(system.available());
    qypr::UDisksBackend udisks(loop, system);
    auto all = qypr::reduceRemovableDrives(sampleTree());
    // A second stick with two volumes, one of them locked.
    qypr::RemovableDrive multi = all[0];
    multi.path = "/drives/multi";
    multi.name = "Kingston DataTraveler";
    multi.volumes = {all[0].volumes[0], all[0].volumes[0]};
    multi.volumes[1].label = "Vault";
    multi.volumes[1].locked = true;
    multi.volumes[1].filesystemPath.clear();
    multi.volumes[1].encryptedPath = "/blocks/sdf2";
    all.push_back(multi);
    all[1].op = {
        .phase = qypr::DriveOperation::Phase::Failed, .error = "Still in use", .blockers = {"zsh"}};
    qypr::RemovableDrivesSnapshot snap{.available = true, .drives = all};

    const auto m = qypr::RemovableDevicesIndicator::buildFlyout(snap, &udisks, nullptr);
    EXPECT_EQ(m.title, std::string("Drives"));
    // STICK (Open, Eject) · SharedDrive (Open, Eject) · failure (Dismiss) ·
    // Kingston (Eject) · STICK volume (Open) · Vault (locked, no buttons)
    EXPECT_EQ(m.rows.size(), 6U);
    EXPECT_EQ(m.rows[0].title, std::string("STICK"));
    EXPECT_EQ(m.rows[0].subtitle, std::string("USB drive · 32 GB · Not mounted"));
    EXPECT_EQ(m.rows[0].actions.size(), 2U);
    EXPECT_EQ(m.rows[0].actions[0].label, std::string("Open"));
    EXPECT_FALSE(m.rows[0].actions[0].closes);  // mounts first; stays open for the result
    EXPECT_EQ(m.rows[1].actions[0].label, std::string("Open"));
    EXPECT_TRUE(m.rows[1].actions[0].closes);  // already mounted: hands off to the file manager
    EXPECT_EQ(m.rows[2].title, std::string("Still in use"));
    EXPECT_EQ(m.rows[2].subtitle, std::string("Used by zsh"));
    EXPECT_TRUE(m.rows[2].tone == qypr::FlyoutRow::Tone::Error);
    EXPECT_EQ(m.rows[2].actions[0].label, std::string("Dismiss"));
    EXPECT_EQ(m.rows[3].title, std::string("Kingston DataTraveler"));
    EXPECT_EQ(m.rows[3].actions.size(), 1U);
    EXPECT_EQ(m.rows[4].actions.size(), 1U);
    EXPECT_EQ(m.rows[5].title, std::string("Vault"));
    EXPECT_TRUE(m.rows[5].actions.empty());

    // A running operation disables the buttons and shows a spinner.
    snap.drives[0].op.phase = qypr::DriveOperation::Phase::Unmounting;
    const auto busy = qypr::RemovableDevicesIndicator::buildFlyout(snap, &udisks, nullptr);
    EXPECT_TRUE(busy.rows[0].busy);
    EXPECT_EQ(busy.rows[0].subtitle, std::string("Unmounting…"));
    EXPECT_FALSE(busy.rows[0].actions[1].enabled);

    // Keyboard: Tab reaches Eject on the first row, Enter presses it.
    udisks.snap_.available = true;
    udisks.drives_ = {all[0]};
    udisks.publish();
    qypr::ListFlyout fly([&] {
        return qypr::RemovableDevicesIndicator::buildFlyout(udisks.snapshot(), &udisks, nullptr);
    });
    fly.anchorX = 600;
    cairo_surface_t* surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 800, 600);
    cairo_t* cr = cairo_create(surface);
    {
        qypr::Painter painter{cr};
        fly.draw(painter, qypr::nowMs());
        EXPECT_TRUE(fly.handleKey(XKB_KEY_Tab));  // Open
        EXPECT_TRUE(fly.handleKey(XKB_KEY_Tab));  // Eject
        EXPECT_TRUE(fly.handleKey(XKB_KEY_Return));
        fly.draw(painter, qypr::nowMs());
    }
    cairo_destroy(cr);
    cairo_surface_destroy(surface);
    const auto* stick = udisks.snapshot().find(kStick);
    EXPECT_TRUE(stick != nullptr);
    EXPECT_TRUE(stick->op.phase == qypr::DriveOperation::Phase::Failed);  // no bus under test
    EXPECT_EQ(fly.model().rows[1].title, std::string("Couldn't reach UDisks2"));
}
