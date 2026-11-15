// RemovableDrives.hpp - What the removable-devices module shows, from UDisks2.
//
// Two layers, so the policy is testable without a bus:
//
//   UDisksObjects        the subset of UDisks2's object tree the bar reads
//                        (Drive, Block, Filesystem, Encrypted), decoded by
//                        UDisksBackend from GetManagedObjects;
//   RemovableDrive list  reduceRemovableDrives(): which drives count as
//                        removable, their volumes, and whether each is
//                        permanently attached.
//
// "Removable" follows what UDisks2 itself reports rather than sysfs: a USB
// hard disk has /sys/block/sdX/removable = 0 (the medium can't be taken out
// of the enclosure), yet UDisks2 marks the drive Removable because the drive
// can be unplugged. That is the property a desktop cares about.
//
// A drive with a volume listed in /etc/fstab or /etc/crypttab (UDisks2's
// Block.Configuration) is *fixed*: the user has set it up as a permanent part
// of the system (a backup disk, a shared data drive). It is still listed and
// can still be removed safely, but it is passive — it doesn't take a place in
// the bar the way a freshly inserted stick does.
#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace qypr {

// ── UDisks2 object tree (decoded subset) ────────────────────────────────────

struct UDisksDrive {
    std::string path;  // /org/freedesktop/UDisks2/drives/…
    std::string vendor;
    std::string model;
    std::string connectionBus;  // "usb", "sdio", "ieee1394", "" (internal)
    std::string sortKey;
    uint64_t size = 0;
    bool removable = false;
    bool mediaRemovable = false;
    bool mediaAvailable = true;
    bool ejectable = false;
    bool canPowerOff = false;
    bool optical = false;
};

struct UDisksBlock {
    std::string path;           // /org/freedesktop/UDisks2/block_devices/…
    std::string drive;          // Block.Drive ("/" for none: dm, loop)
    std::string device;         // PreferredDevice, else Device ("/dev/sda1")
    std::string label;          // IdLabel
    std::string hintName;       // HintName (udev UDISKS_NAME)
    std::string idUsage;        // "filesystem", "crypto", "other", ""
    std::string idType;         // "exfat", "crypto_LUKS", …
    std::string cryptoBacking;  // CryptoBackingDevice ("/" when not a cleartext device)
    uint64_t size = 0;
    bool hintIgnore = false;
    bool hintSystem = false;
    bool configured = false;  // Block.Configuration has an fstab or crypttab entry
    // Filesystem interface
    bool hasFilesystem = false;
    std::vector<std::string> mountPoints;
    // Encrypted interface
    bool hasEncrypted = false;
    std::string cleartext;  // Encrypted.CleartextDevice ("/" while locked)
};

struct UDisksObjects {
    std::vector<UDisksDrive> drives;
    std::vector<UDisksBlock> blocks;
};

// ── What the module shows ───────────────────────────────────────────────────

struct RemovableVolume {
    std::string label;   // IdLabel → HintName → device name
    std::string device;  // "/dev/sda1"
    std::string fsType;  // "exfat", "" for a locked container
    uint64_t size = 0;
    // The object that carries the filesystem (the cleartext device of an
    // unlocked container); "" when there is none to mount (locked).
    std::string filesystemPath;
    std::vector<std::string> mountPoints;
    // The LUKS container, when encrypted ("" otherwise).
    std::string encryptedPath;
    bool locked = false;

    [[nodiscard]] bool mounted() const { return !mountPoints.empty(); }
    [[nodiscard]] bool encrypted() const { return !encryptedPath.empty(); }
    bool operator==(const RemovableVolume&) const = default;
};

// In-flight or finished "safely remove" / mount / unmount on one drive.
struct DriveOperation {
    enum class Phase : uint8_t { None, Mounting, Unmounting, Locking, PoweringOff, Done, Failed };
    Phase phase = Phase::None;
    std::string error;                  // Failed: one line, user-facing
    std::vector<std::string> blockers;  // Failed while busy: process names holding it open

    [[nodiscard]] bool busy() const {
        return phase == Phase::Mounting || phase == Phase::Unmounting || phase == Phase::Locking ||
               phase == Phase::PoweringOff;
    }
    bool operator==(const DriveOperation&) const = default;
};

struct RemovableDrive {
    std::string path;           // UDisks2 drive object
    std::string name;           // "WDC WD40NPZZ-00PDPT0", "SanDisk Ultra"
    std::string connectionBus;  // "usb", "sdio", …
    uint64_t size = 0;
    bool canPowerOff = false;
    bool ejectable = false;
    bool optical = false;
    bool fixed = false;  // configured in fstab/crypttab: listed, but passive
    std::vector<RemovableVolume> volumes;
    DriveOperation op;

    [[nodiscard]] bool anyMounted() const;
    // Title for the drive: its only volume's label when it has exactly one
    // ("SharedDrive"), else the drive name.
    [[nodiscard]] std::string title() const;
    bool operator==(const RemovableDrive&) const = default;
};

struct RemovableDrivesSnapshot {
    bool available = false;  // UDisks2 answered
    std::vector<RemovableDrive> drives;

    [[nodiscard]] const RemovableDrive* find(const std::string& path) const;
    bool operator==(const RemovableDrivesSnapshot&) const = default;
};

// Removable drives with media, hot-plugged ones first. Drives backing a
// system mount ("/", "/boot", "/usr", …) are left out: they can't be removed
// from a running system.
std::vector<RemovableDrive> reduceRemovableDrives(const UDisksObjects& objects);

// "4.0 TB", "512 MB" — decimal units, like file managers and drive labels.
std::string formatDriveSize(uint64_t bytes);

// "USB drive", "SD card", "Disc", "Drive".
std::string_view describeConnection(const RemovableDrive& drive);

}  // namespace qypr
