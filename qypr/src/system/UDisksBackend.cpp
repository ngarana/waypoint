// UDisksBackend.cpp - UDisks2 client: object tree, safe removal, mount.
#include "system/UDisksBackend.hpp"

#include <systemd/sd-bus.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <set>
#include <string_view>
#include <utility>

#include "system/DbusRead.hpp"
#include "system/SystemBus.hpp"

namespace qypr {

namespace {

constexpr const char* kSvc = "org.freedesktop.UDisks2";
constexpr const char* kRoot = "/org/freedesktop/UDisks2";
constexpr const char* kObjectManager = "org.freedesktop.DBus.ObjectManager";
constexpr const char* kDriveIface = "org.freedesktop.UDisks2.Drive";
constexpr const char* kBlockIface = "org.freedesktop.UDisks2.Block";
constexpr const char* kFilesystemIface = "org.freedesktop.UDisks2.Filesystem";
constexpr const char* kEncryptedIface = "org.freedesktop.UDisks2.Encrypted";

bool interesting(std::string_view iface) {
    return iface == kDriveIface || iface == kBlockIface || iface == kFilesystemIface ||
           iface == kEncryptedIface;
}

using dbus::readBool;
using dbus::readString;
using dbus::readU64;

// UDisks2 passes paths as NUL-terminated byte strings (ay).
std::string bytesToString(const void* data, size_t size) {
    const auto* p = static_cast<const char*>(data);
    const auto len = static_cast<size_t>(std::find(p, p + size, '\0') - p);
    return {p, len};
}

bool readByteString(sd_bus_message* m, std::string* out) {
    if (sd_bus_message_enter_container(m, 'v', "ay") <= 0) {
        sd_bus_message_skip(m, "v");
        return false;
    }
    const void* data = nullptr;
    size_t size = 0;
    const int r = sd_bus_message_read_array(m, 'y', &data, &size);
    sd_bus_message_exit_container(m);
    if (r < 0) { return false; }
    *out = bytesToString(data, size);
    return true;
}

bool readByteStrings(sd_bus_message* m, std::vector<std::string>* out) {
    if (sd_bus_message_enter_container(m, 'v', "aay") <= 0) {
        sd_bus_message_skip(m, "v");
        return false;
    }
    out->clear();
    if (sd_bus_message_enter_container(m, 'a', "ay") > 0) {
        const void* data = nullptr;
        size_t size = 0;
        while (sd_bus_message_read_array(m, 'y', &data, &size) > 0) {
            std::string s = bytesToString(data, size);
            if (!s.empty()) { out->push_back(std::move(s)); }
        }
        sd_bus_message_exit_container(m);
    }
    sd_bus_message_exit_container(m);
    return true;
}

// Block.Configuration: a(sa{sv}); true when any entry is fstab or crypttab.
bool readConfigured(sd_bus_message* m, bool* out) {
    if (sd_bus_message_enter_container(m, 'v', "a(sa{sv})") <= 0) {
        sd_bus_message_skip(m, "v");
        return false;
    }
    *out = false;
    if (sd_bus_message_enter_container(m, 'a', "(sa{sv})") > 0) {
        while (sd_bus_message_enter_container(m, 'r', "sa{sv}") > 0) {
            const char* type = nullptr;
            if (sd_bus_message_read(m, "s", &type) >= 0 && type != nullptr &&
                (std::strcmp(type, "fstab") == 0 || std::strcmp(type, "crypttab") == 0)) {
                *out = true;
            }
            sd_bus_message_skip(m, "a{sv}");
            sd_bus_message_exit_container(m);
        }
        sd_bus_message_exit_container(m);
    }
    sd_bus_message_exit_container(m);
    return true;
}

void readDriveProp(sd_bus_message* m, std::string_view key, UDisksDrive* d) {
    if (key == "Vendor") {
        readString(m, 's', &d->vendor);
    } else if (key == "Model") {
        readString(m, 's', &d->model);
    } else if (key == "ConnectionBus") {
        readString(m, 's', &d->connectionBus);
    } else if (key == "SortKey") {
        readString(m, 's', &d->sortKey);
    } else if (key == "Size") {
        readU64(m, &d->size);
    } else if (key == "Removable") {
        readBool(m, &d->removable);
    } else if (key == "MediaRemovable") {
        readBool(m, &d->mediaRemovable);
    } else if (key == "MediaAvailable") {
        readBool(m, &d->mediaAvailable);
    } else if (key == "Ejectable") {
        readBool(m, &d->ejectable);
    } else if (key == "CanPowerOff") {
        readBool(m, &d->canPowerOff);
    } else if (key == "Optical") {
        readBool(m, &d->optical);
    } else {
        sd_bus_message_skip(m, "v");
    }
}

void readBlockProp(sd_bus_message* m, std::string_view key, UDisksBlock* b) {
    if (key == "Drive") {
        readString(m, 'o', &b->drive);
    } else if (key == "PreferredDevice") {
        readByteString(m, &b->device);
    } else if (key == "Device") {
        std::string dev;
        if (readByteString(m, &dev) && b->device.empty()) { b->device = std::move(dev); }
    } else if (key == "IdLabel") {
        readString(m, 's', &b->label);
    } else if (key == "HintName") {
        readString(m, 's', &b->hintName);
    } else if (key == "IdUsage") {
        readString(m, 's', &b->idUsage);
    } else if (key == "IdType") {
        readString(m, 's', &b->idType);
    } else if (key == "CryptoBackingDevice") {
        readString(m, 'o', &b->cryptoBacking);
    } else if (key == "Size") {
        readU64(m, &b->size);
    } else if (key == "HintIgnore") {
        readBool(m, &b->hintIgnore);
    } else if (key == "HintSystem") {
        readBool(m, &b->hintSystem);
    } else if (key == "Configuration") {
        readConfigured(m, &b->configured);
    } else {
        sd_bus_message_skip(m, "v");
    }
}

// GetManagedObjects reply a{oa{sa{sv}}} → the subset the bar reads.
bool parseObjects(sd_bus_message* m, UDisksObjects* out) {
    if (sd_bus_message_enter_container(m, 'a', "{oa{sa{sv}}}") <= 0) { return false; }
    while (sd_bus_message_enter_container(m, 'e', "oa{sa{sv}}") > 0) {
        const char* path = nullptr;
        if (sd_bus_message_read(m, "o", &path) < 0 || path == nullptr) { return false; }
        UDisksDrive drive;
        UDisksBlock block;
        bool isDrive = false;
        bool isBlock = false;
        if (sd_bus_message_enter_container(m, 'a', "{sa{sv}}") > 0) {
            while (sd_bus_message_enter_container(m, 'e', "sa{sv}") > 0) {
                const char* iface = nullptr;
                sd_bus_message_read(m, "s", &iface);
                const std::string_view name = iface != nullptr ? iface : "";
                if (name == kDriveIface) {
                    isDrive = true;
                    dbus::forEachProperty(m,
                                          [&](std::string_view k) { readDriveProp(m, k, &drive); });
                } else if (name == kBlockIface) {
                    isBlock = true;
                    dbus::forEachProperty(m,
                                          [&](std::string_view k) { readBlockProp(m, k, &block); });
                } else if (name == kFilesystemIface) {
                    block.hasFilesystem = true;
                    dbus::forEachProperty(m, [&](std::string_view k) {
                        if (k == "MountPoints") {
                            readByteStrings(m, &block.mountPoints);
                        } else {
                            sd_bus_message_skip(m, "v");
                        }
                    });
                } else if (name == kEncryptedIface) {
                    block.hasEncrypted = true;
                    dbus::forEachProperty(m, [&](std::string_view k) {
                        if (k == "CleartextDevice") {
                            readString(m, 'o', &block.cleartext);
                        } else {
                            sd_bus_message_skip(m, "v");
                        }
                    });
                } else {
                    sd_bus_message_skip(m, "a{sv}");
                }
                sd_bus_message_exit_container(m);
            }
            sd_bus_message_exit_container(m);
        }
        sd_bus_message_exit_container(m);
        if (isDrive) {
            drive.path = path;
            out->drives.push_back(std::move(drive));
        } else if (isBlock) {
            block.path = path;
            out->blocks.push_back(std::move(block));
        }
    }
    sd_bus_message_exit_container(m);
    return true;
}

std::string firstLine(const std::string& s) {
    const auto nl = s.find('\n');
    return nl == std::string::npos ? s : s.substr(0, nl);
}

bool isUnder(const std::string& target, const std::string& mountPoint) {
    if (target == mountPoint) { return true; }
    return target.size() > mountPoint.size() && target.starts_with(mountPoint) &&
           target[mountPoint.size()] == '/';
}

// The CallThunk owns the reply callback until sd-bus hands back the reply.
struct CallThunk {
    std::function<void(sd_bus_message*)> reply;
};

}  // namespace

UDisksBackend::UDisksBackend(EventLoop& loop, SystemBus& systemBus)
    : bus_(systemBus),
      retry_(loop, [this] { refresh(); }) {}

UDisksBackend::~UDisksBackend() {
    retry_.cancel();
    sd_bus_slot_unref(objectsSlot_);
    sd_bus_slot_unref(propsSlot_);
    sd_bus_slot_unref(ownerSlot_);
}

bool UDisksBackend::start() {
    if (!bus_.available()) {
        snap_ = {};
        return false;
    }
    if (started_) {
        refresh();
        return true;
    }
    started_ = true;
    objectsSlot_ = bus_.addMatch(
        "type='signal',sender='org.freedesktop.UDisks2',"
        "interface='org.freedesktop.DBus.ObjectManager',path='/org/freedesktop/UDisks2'",
        &UDisksBackend::onSignal, this);
    propsSlot_ =
        bus_.addMatch("type='signal',sender='org.freedesktop.UDisks2',"
                      "interface='org.freedesktop.DBus.Properties',member='PropertiesChanged',"
                      "path_namespace='/org/freedesktop/UDisks2'",
                      &UDisksBackend::onSignal, this);
    ownerSlot_ = bus_.addMatch(
        "type='signal',sender='org.freedesktop.DBus',interface='org.freedesktop.DBus',"
        "member='NameOwnerChanged',arg0='org.freedesktop.UDisks2'",
        &UDisksBackend::onNameOwnerChanged, this);
    refresh();
    return true;
}

int UDisksBackend::onSignal(sd_bus_message* m, void* userdata, sd_bus_error*) {
    auto* self = static_cast<UDisksBackend*>(userdata);
    // PropertiesChanged carries the interface first: SMART and NVMe health
    // refreshes (Drive.Ata, NVMe.Controller) change nothing shown here.
    if (sd_bus_message_has_signature(m, "sa{sv}as") != 0) {
        const char* iface = nullptr;
        if (sd_bus_message_read(m, "s", &iface) >= 0 && iface != nullptr && !interesting(iface)) {
            return 0;
        }
    }
    self->refresh();
    return 0;
}

int UDisksBackend::onNameOwnerChanged(sd_bus_message* m, void* userdata, sd_bus_error*) {
    auto* self = static_cast<UDisksBackend*>(userdata);
    const char* name = nullptr;
    const char* oldOwner = nullptr;
    const char* newOwner = nullptr;
    if (sd_bus_message_read(m, "sss", &name, &oldOwner, &newOwner) < 0) { return 0; }
    if (newOwner == nullptr || *newOwner == '\0') {
        // Daemon gone: every in-flight call has failed with it.
        self->ops_.clear();
        self->apply({});
        self->snap_.available = false;
        self->publish();
        return 0;
    }
    self->refresh();
    return 0;
}

// NOLINTNEXTLINE(misc-no-recursion) Async D-Bus replies resume on the event loop.
void UDisksBackend::refresh() {
    sd_bus* bus = bus_.get();
    if (bus == nullptr) { return; }
    retry_.cancel();
    if (fetchInFlight_) {
        pendingRefresh_ = true;
        return;
    }
    fetchInFlight_ = true;
    const int r =
        sd_bus_call_method_async(bus, nullptr, kSvc, kRoot, kObjectManager, "GetManagedObjects",
                                 &UDisksBackend::onObjectsReply, this, "");
    if (r < 0) { endRefresh(false); }
}

// NOLINTNEXTLINE(misc-no-recursion) Async D-Bus replies resume on the event loop.
int UDisksBackend::onObjectsReply(sd_bus_message* m, void* userdata, sd_bus_error*) {
    auto* self = static_cast<UDisksBackend*>(userdata);
    UDisksObjects objects;
    const bool ok = sd_bus_message_is_method_error(m, nullptr) == 0 && parseObjects(m, &objects);
    if (ok) {
        self->snap_.available = true;
        self->apply(objects);
    } else if (self->snap_.available) {
        self->snap_.available = false;
        self->apply({});
    }
    self->endRefresh(ok);
    return 0;
}

// NOLINTNEXTLINE(misc-no-recursion) Async D-Bus replies resume on the event loop.
void UDisksBackend::endRefresh(bool success) {
    fetchInFlight_ = false;
    if (success) {
        retry_.reset();
    } else if (started_) {
        retry_.schedule();
    }
    if (pendingRefresh_) {
        pendingRefresh_ = false;
        refresh();
    }
}

void UDisksBackend::apply(const UDisksObjects& objects) {
    drives_ = reduceRemovableDrives(objects);
    // A drive that went away takes its operation with it: powered off (the
    // goal), unplugged mid-way, or gone with the daemon.
    std::erase_if(ops_, [&](const auto& entry) {
        return std::ranges::find(drives_, entry.first, &RemovableDrive::path) == drives_.end();
    });
    publish();
}

void UDisksBackend::publish() {
    RemovableDrivesSnapshot next;
    next.available = snap_.available;
    next.drives = drives_;
    for (auto& d : next.drives) {
        const auto it = ops_.find(d.path);
        if (it != ops_.end()) { d.op = it->second.state; }
    }
    if (next == snap_) { return; }
    snap_ = std::move(next);
    if (onChange_) { onChange_(); }
}

// ── Operations ──────────────────────────────────────────────────────────────

void UDisksBackend::safelyRemove(const std::string& drivePath) {
    const auto it = std::ranges::find(drives_, drivePath, &RemovableDrive::path);
    if (it == drives_.end()) { return; }
    if (const auto op = ops_.find(drivePath); op != ops_.end() && op->second.state.busy()) {
        return;
    }
    ops_[drivePath] = {.state = {}, .steps = planRemoval(*it)};
    runNextStep(drivePath);
}

std::vector<UDisksBackend::Step> UDisksBackend::planRemoval(const RemovableDrive& drive) {
    std::vector<Step> steps;
    for (const auto& v : drive.volumes) {
        if (v.mounted() && !v.filesystemPath.empty()) {
            steps.push_back({Step::Kind::Unmount, v.filesystemPath, v.mountPoints.front()});
        }
    }
    for (const auto& v : drive.volumes) {
        if (v.encrypted() && !v.locked) {
            steps.push_back({Step::Kind::Lock, v.encryptedPath, {}});
        }
    }
    if (drive.canPowerOff) {
        steps.push_back({Step::Kind::PowerOff, drive.path, {}});
    } else if (drive.ejectable) {
        steps.push_back({Step::Kind::Eject, drive.path, {}});
    }
    return steps;
}

// NOLINTNEXTLINE(misc-no-recursion) Async D-Bus replies resume on the event loop.
void UDisksBackend::runNextStep(const std::string& drivePath) {
    const auto it = ops_.find(drivePath);
    if (it == ops_.end()) { return; }
    Operation& op = it->second;
    if (op.steps.empty()) {
        op.state = {.phase = DriveOperation::Phase::Done};
        publish();
        return;
    }
    const Step step = op.steps.front();
    op.steps.erase(op.steps.begin());

    const char* iface = kFilesystemIface;
    const char* member = "Unmount";
    uint64_t timeout = kUnmountTimeoutUs;
    DriveOperation::Phase phase = DriveOperation::Phase::Unmounting;
    switch (step.kind) {
        case Step::Kind::Unmount:
            break;
        case Step::Kind::Lock:
            iface = kEncryptedIface;
            member = "Lock";
            timeout = kLockTimeoutUs;
            phase = DriveOperation::Phase::Locking;
            break;
        case Step::Kind::PowerOff:
        case Step::Kind::Eject:
            iface = kDriveIface;
            member = step.kind == Step::Kind::PowerOff ? "PowerOff" : "Eject";
            timeout = kPowerOffTimeoutUs;
            phase = DriveOperation::Phase::PoweringOff;
            break;
    }
    op.state = {.phase = phase};
    publish();

    const bool sent = call(
        step.object, iface, member, timeout,
        [this, drivePath, step](sd_bus_message* reply) {  // NOLINT(misc-no-recursion)
            const sd_bus_error* e = sd_bus_message_get_error(reply);
            if (e == nullptr) {
                runNextStep(drivePath);
                return;
            }
            const std::string name = e->name != nullptr ? e->name : "";
            // Unmounted behind our back (another app, or the automounter):
            // the goal of this step is met.
            if (step.kind == Step::Kind::Unmount &&
                name == "org.freedesktop.UDisks2.Error.NotMounted") {
                runNextStep(drivePath);
                return;
            }
            std::vector<std::string> blockers;
            if (name == "org.freedesktop.UDisks2.Error.DeviceBusy" && !step.mountPoint.empty()) {
                blockers = processesUsing(step.mountPoint);
            }
            fail(drivePath, describeError(name, e->message != nullptr ? e->message : ""),
                 std::move(blockers));
        });
    if (!sent) { fail(drivePath, "Couldn't reach UDisks2"); }
}

void UDisksBackend::mount(const std::string& drivePath, const std::string& filesystemPath,
                          std::function<void(const std::string& mountPoint)> onMounted) {
    if (const auto op = ops_.find(drivePath); op != ops_.end() && op->second.state.busy()) {
        return;
    }
    ops_[drivePath] = {.state = {.phase = DriveOperation::Phase::Mounting}};
    publish();
    const bool sent =
        call(filesystemPath, kFilesystemIface, "Mount", kMountTimeoutUs,
             [this, drivePath, cb = std::move(onMounted)](sd_bus_message* reply) {
                 const sd_bus_error* e = sd_bus_message_get_error(reply);
                 if (e != nullptr) {
                     fail(drivePath, describeError(e->name != nullptr ? e->name : "",
                                                   e->message != nullptr ? e->message : ""));
                     return;
                 }
                 const char* where = nullptr;
                 sd_bus_message_read(reply, "s", &where);
                 ops_.erase(drivePath);
                 publish();
                 if (cb && where != nullptr) { cb(where); }
             });
    if (!sent) { fail(drivePath, "Couldn't reach UDisks2"); }
}

void UDisksBackend::unmount(const std::string& drivePath, const std::string& filesystemPath) {
    const auto drive = std::ranges::find(drives_, drivePath, &RemovableDrive::path);
    if (drive == drives_.end()) { return; }
    if (const auto op = ops_.find(drivePath); op != ops_.end() && op->second.state.busy()) {
        return;
    }
    std::string mountPoint;
    for (const auto& v : drive->volumes) {
        if (v.filesystemPath == filesystemPath && v.mounted()) {
            mountPoint = v.mountPoints.front();
        }
    }
    ops_[drivePath] = {.state = {.phase = DriveOperation::Phase::Unmounting}};
    publish();
    const bool sent =
        call(filesystemPath, kFilesystemIface, "Unmount", kUnmountTimeoutUs,
             [this, drivePath, mountPoint](sd_bus_message* reply) {
                 const sd_bus_error* e = sd_bus_message_get_error(reply);
                 const std::string name = e != nullptr && e->name != nullptr ? e->name : "";
                 if (e == nullptr || name == "org.freedesktop.UDisks2.Error.NotMounted") {
                     ops_.erase(drivePath);
                     publish();
                     return;
                 }
                 std::vector<std::string> blockers;
                 if (name == "org.freedesktop.UDisks2.Error.DeviceBusy" && !mountPoint.empty()) {
                     blockers = processesUsing(mountPoint);
                 }
                 fail(drivePath, describeError(name, e->message != nullptr ? e->message : ""),
                      std::move(blockers));
             });
    if (!sent) { fail(drivePath, "Couldn't reach UDisks2"); }
}

void UDisksBackend::clearResult(const std::string& drivePath) {
    const auto it = ops_.find(drivePath);
    if (it == ops_.end() || it->second.state.busy()) { return; }
    ops_.erase(it);
    publish();
}

void UDisksBackend::fail(const std::string& drivePath, const std::string& error,
                         std::vector<std::string> blockers) {
    auto& op = ops_[drivePath];
    op.steps.clear();
    op.state = {
        .phase = DriveOperation::Phase::Failed, .error = error, .blockers = std::move(blockers)};
    publish();
}

bool UDisksBackend::call(const std::string& object, const char* iface, const char* member,
                         uint64_t timeoutUs, Reply reply) {
    sd_bus* bus = bus_.get();
    if (bus == nullptr) { return false; }
    sd_bus_message* msg = nullptr;
    // Built by hand so the call carries its own timeout, and allows polkit to
    // ask the user (unmounting an fstab mount needs an administrator).
    int r = sd_bus_message_new_method_call(bus, &msg, kSvc, object.c_str(), iface, member);
    if (r >= 0) { r = sd_bus_message_set_allow_interactive_authorization(msg, 1); }
    if (r >= 0) { r = sd_bus_message_open_container(msg, 'a', "{sv}"); }
    if (r >= 0) { r = sd_bus_message_close_container(msg); }
    // NOLINTNEXTLINE(cppcoreguidelines-owning-memory) // sd-bus userdata; freed on reply
    auto* thunk = new CallThunk{std::move(reply)};
    if (r >= 0) {
        r = sd_bus_call_async(bus, nullptr, msg, &UDisksBackend::onCallReply, thunk, timeoutUs);
    }
    sd_bus_message_unref(msg);
    if (r < 0) {
        // NOLINTNEXTLINE(cppcoreguidelines-owning-memory) // never handed to sd-bus
        delete thunk;
        return false;
    }
    return true;
}

int UDisksBackend::onCallReply(sd_bus_message* m, void* userdata, sd_bus_error*) {
    // NOLINTNEXTLINE(cppcoreguidelines-owning-memory) // allocated in call()
    auto* thunk = static_cast<CallThunk*>(userdata);
    if (thunk->reply) { thunk->reply(m); }
    // NOLINTNEXTLINE(cppcoreguidelines-owning-memory) // allocated in call()
    delete thunk;
    return 0;
}

// ── Helpers ─────────────────────────────────────────────────────────────────

std::vector<std::string> UDisksBackend::processesUsing(const std::string& mountPoint,
                                                       const std::string& procRoot, size_t limit) {
    std::set<std::string> names;
    if (mountPoint.empty() || mountPoint == "/") { return {}; }
    namespace fs = std::filesystem;
    std::error_code ec;
    for (const auto& proc : fs::directory_iterator(procRoot, ec)) {
        const std::string pid = proc.path().filename().string();
        if (pid.empty() ||
            !std::ranges::all_of(pid, [](unsigned char c) { return std::isdigit(c); })) {
            continue;
        }
        bool uses = false;
        for (const char* link : {"cwd", "root", "exe"}) {
            const auto target = fs::read_symlink(proc.path() / link, ec);
            if (!ec && isUnder(target.string(), mountPoint)) { uses = true; }
        }
        if (!uses) {
            for (const auto& fd : fs::directory_iterator(proc.path() / "fd", ec)) {
                const auto target = fs::read_symlink(fd.path(), ec);
                if (!ec && isUnder(target.string(), mountPoint)) {
                    uses = true;
                    break;
                }
            }
        }
        if (!uses) { continue; }
        std::ifstream comm(proc.path() / "comm");
        std::string name;
        if (std::getline(comm, name) && !name.empty()) { names.insert(name); }
        if (names.size() >= limit) { break; }
    }
    return {names.begin(), names.end()};
}

std::string UDisksBackend::describeError(const std::string& name, const std::string& message) {
    if (name == "org.freedesktop.UDisks2.Error.DeviceBusy") { return "Still in use"; }
    if (name == "org.freedesktop.UDisks2.Error.NotAuthorizedDismissed") {
        return "Authentication was cancelled";
    }
    if (name.starts_with("org.freedesktop.UDisks2.Error.NotAuthorized")) { return "Not allowed"; }
    if (name == "org.freedesktop.DBus.Error.NoReply" ||
        name == "org.freedesktop.DBus.Error.Timeout") {
        return "Timed out — the drive may still be writing";
    }
    if (name == "org.freedesktop.DBus.Error.ServiceUnknown") { return "UDisks2 is not running"; }
    if (!message.empty()) { return firstLine(message); }
    return name.empty() ? std::string("Failed") : name;
}

}  // namespace qypr
