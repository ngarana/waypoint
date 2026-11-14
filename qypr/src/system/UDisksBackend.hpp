// UDisksBackend.hpp - Removable drives over UDisks2 (system bus).
//
// Push-driven like every other backend: one GetManagedObjects at start, then
// a re-read whenever UDisks2 reports a change (InterfacesAdded/Removed,
// PropertiesChanged on a Drive, Block, Filesystem or Encrypted interface;
// SMART/NVMe health updates are ignored). Re-reads are coalesced: at most
// one in flight plus one pending.
//
// Safely removing a drive is the same sequence desktop file managers run:
//
//   1. Filesystem.Unmount on every mounted volume (flushes pending writes,
//      which on a slow USB disk can take minutes — hence the long ceiling);
//   2. Encrypted.Lock on every unlocked LUKS container;
//   3. Drive.PowerOff (spins the disk down and detaches it from the bus),
//      else Drive.Eject for media that can only be ejected, else nothing —
//      the drive is then simply safe to unplug.
//
// The first failure stops the chain and is reported on the drive. When the
// failure is "device busy", the backend lists the processes holding files
// open under the mount point so the user knows what to close. UDisks2 asks
// polkit for anything the session may not do on its own (unmounting an fstab
// mount, for one); the session's polkit agent shows that prompt.
//
// Nothing here elevates privileges or runs a helper: every action is a
// UDisks2 method call that polkit authorizes.
#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <vector>

#include "core/RetryTimer.hpp"
#include "system/RemovableDrives.hpp"

struct sd_bus_message;
struct sd_bus_slot;
#include <systemd/sd-bus.h>  // sd_bus_error is a typedef here, not a struct

namespace qypr {

class EventLoop;
class SystemBus;

class UDisksBackend {
public:
    UDisksBackend(EventLoop& loop, SystemBus& systemBus);
    ~UDisksBackend();

    UDisksBackend(const UDisksBackend&) = delete;
    UDisksBackend& operator=(const UDisksBackend&) = delete;
    UDisksBackend(UDisksBackend&&) = delete;
    UDisksBackend& operator=(UDisksBackend&&) = delete;

    // Subscribe and issue the first read. False when the system bus is down.
    bool start();

    [[nodiscard]] const RemovableDrivesSnapshot& snapshot() const { return snap_; }
    void setOnChange(std::function<void()> cb) { onChange_ = std::move(cb); }

    // Unmount, lock and power off a drive (see above). No-op while another
    // operation on the same drive is running.
    void safelyRemove(const std::string& drivePath);
    // Mount one volume; `onMounted` receives the mount point on success.
    void mount(const std::string& drivePath, const std::string& filesystemPath,
               std::function<void(const std::string& mountPoint)> onMounted);
    void unmount(const std::string& drivePath, const std::string& filesystemPath);
    // Forget a finished operation's result (Done / Failed).
    void clearResult(const std::string& drivePath);

    // Process names (comm) with a file, working directory or root under
    // `mountPoint`. Readable processes only — the session's own. Sorted,
    // unique, at most `limit`.
    static std::vector<std::string> processesUsing(const std::string& mountPoint,
                                                   const std::string& procRoot = "/proc",
                                                   size_t limit = 6);
    // User-facing text for a failed UDisks2 call.
    static std::string describeError(const std::string& name, const std::string& message);

    // Ceilings for the user's own actions. Unmount waits for pending writes
    // to reach the disk; PowerOff for the drive to spin down.
    static constexpr uint64_t kUnmountTimeoutUs = 300'000'000;
    static constexpr uint64_t kMountTimeoutUs = 60'000'000;
    static constexpr uint64_t kLockTimeoutUs = 60'000'000;
    static constexpr uint64_t kPowerOffTimeoutUs = 120'000'000;

private:
    struct Step {
        enum class Kind : uint8_t { Unmount, Lock, PowerOff, Eject };
        Kind kind;
        std::string object;      // object path the method is called on
        std::string mountPoint;  // Unmount: where to look for blockers
    };
    struct Operation {
        DriveOperation state;
        std::vector<Step> steps;  // remaining, front first
    };
    // Unmount every mounted volume, lock every unlocked container, then power
    // off (or eject) the drive.
    static std::vector<Step> planRemoval(const RemovableDrive& drive);

    static int onSignal(sd_bus_message* m, void* userdata, sd_bus_error* err);
    static int onNameOwnerChanged(sd_bus_message* m, void* userdata, sd_bus_error* err);
    static int onObjectsReply(sd_bus_message* m, void* userdata, sd_bus_error* err);
    void refresh();
    void endRefresh(bool success);
    void apply(const UDisksObjects& objects);
    void publish();

    void runNextStep(const std::string& drivePath);
    void fail(const std::string& drivePath, const std::string& error,
              std::vector<std::string> blockers = {});
    // Async call with an empty a{sv} options argument and its own timeout.
    using Reply = std::function<void(sd_bus_message* reply)>;
    bool call(const std::string& object, const char* iface, const char* member, uint64_t timeoutUs,
              Reply reply);
    static int onCallReply(sd_bus_message* m, void* userdata, sd_bus_error* err);

    SystemBus& bus_;
    RetryTimer retry_;
    sd_bus_slot* objectsSlot_ = nullptr;
    sd_bus_slot* propsSlot_ = nullptr;
    sd_bus_slot* ownerSlot_ = nullptr;
    bool started_ = false;
    bool fetchInFlight_ = false;
    bool pendingRefresh_ = false;

    std::vector<RemovableDrive> drives_;    // last reduction
    std::map<std::string, Operation> ops_;  // by drive path
    RemovableDrivesSnapshot snap_;
    std::function<void()> onChange_;
};

}  // namespace qypr
