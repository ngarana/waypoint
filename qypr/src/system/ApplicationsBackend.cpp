// ApplicationsBackend.cpp - LauncherEntry signals and org.freedesktop.Application.
#include "system/ApplicationsBackend.hpp"

#include <algorithm>
#include <memory>
#include <string_view>

#include "core/EventLoop.hpp"
#include "system/DbusRead.hpp"
#include "system/DesktopIndex.hpp"
#include "system/SystemBus.hpp"

namespace qypr {

namespace {

constexpr const char* kUnityName = "com.canonical.Unity";
constexpr const char* kAppIface = "org.freedesktop.Application";

// A number in a variant, whatever integer or floating type the app chose
// (the spec says x for count and d for progress; not every app agrees).
bool readNumber(sd_bus_message* m, double* out) {
    char type = 0;
    const char* contents = nullptr;
    if (sd_bus_message_peek_type(m, &type, &contents) <= 0 || type != 'v' || contents == nullptr ||
        sd_bus_message_enter_container(m, 'v', contents) <= 0) {
        sd_bus_message_skip(m, "v");
        return false;
    }
    bool ok = true;
    switch (contents[0]) {
        case 'x': {
            int64_t v = 0;
            ok = sd_bus_message_read_basic(m, 'x', &v) > 0;
            *out = static_cast<double>(v);
            break;
        }
        case 't': {
            uint64_t v = 0;
            ok = sd_bus_message_read_basic(m, 't', &v) > 0;
            *out = static_cast<double>(v);
            break;
        }
        case 'i': {
            int32_t v = 0;
            ok = sd_bus_message_read_basic(m, 'i', &v) > 0;
            *out = v;
            break;
        }
        case 'u': {
            uint32_t v = 0;
            ok = sd_bus_message_read_basic(m, 'u', &v) > 0;
            *out = v;
            break;
        }
        case 'd': {
            double v = 0;
            ok = sd_bus_message_read_basic(m, 'd', &v) > 0;
            *out = v;
            break;
        }
        default:
            sd_bus_message_skip(m, contents);
            ok = false;
            break;
    }
    sd_bus_message_exit_container(m);
    return ok;
}

// A string or object path in a variant (the quicklist is a string in the
// spec; some apps send an object path).
bool readPath(sd_bus_message* m, std::string* out) {
    char type = 0;
    const char* contents = nullptr;
    if (sd_bus_message_peek_type(m, &type, &contents) > 0 && type == 'v' && contents != nullptr &&
        (contents[0] == 's' || contents[0] == 'o') && contents[1] == '\0') {
        return dbus::readString(m, contents[0], out);
    }
    sd_bus_message_skip(m, "v");
    return false;
}

// What the activation reply needs to fall back to a spawn.
struct ActivateFallback {
    EventLoop* loop = nullptr;
    std::string exec;
    bool terminal = false;
};

}  // namespace

double LauncherEntry::shownProgress() const {
    return progressVisible ? std::clamp(progress, 0.0, 1.0) : -1.0;
}

ApplicationsBackend::ApplicationsBackend(EventLoop& loop, SystemBus& sessionBus)
    : loop_(loop),
      bus_(sessionBus) {}

ApplicationsBackend::~ApplicationsBackend() {
    if (updateSlot_ != nullptr) { sd_bus_slot_unref(updateSlot_); }
    if (nameSlot_ != nullptr) { sd_bus_slot_unref(nameSlot_); }
    for (auto& [_, slot] : senderSlots_) { sd_bus_slot_unref(slot); }
}

bool ApplicationsBackend::start() {
    if (!bus_.available() || updateSlot_ != nullptr) { return updateSlot_ != nullptr; }
    updateSlot_ =
        bus_.addMatch("type='signal',interface='com.canonical.Unity.LauncherEntry',member='Update'",
                      &ApplicationsBackend::onUpdate, this);
    // Owned by Plasma's task manager or a dock when one runs; then we only
    // listen. Without an owner some apps never send their counts. A shell
    // that starts later may take the name over; the signals still arrive.
    sd_bus_request_name_async(bus_.get(), &nameSlot_, kUnityName, SD_BUS_NAME_ALLOW_REPLACEMENT,
                              nullptr, nullptr);
    return updateSlot_ != nullptr;
}

std::string ApplicationsBackend::desktopIdFromUri(const std::string& uri) {
    static constexpr std::string_view kScheme = "application://";
    static constexpr std::string_view kSuffix = ".desktop";
    if (!uri.starts_with(kScheme)) { return {}; }
    std::string id = uri.substr(kScheme.size());
    if (id.ends_with(kSuffix)) { id.resize(id.size() - kSuffix.size()); }
    return id;
}

std::string ApplicationsBackend::objectPathFor(const std::string& desktopId) {
    std::string path = "/";
    for (const char c : desktopId) {
        if (c == '.') {
            path += '/';
        } else if (c == '-') {
            path += '_';
        } else {
            path += c;
        }
    }
    return path;
}

void ApplicationsBackend::applyUpdate(std::vector<LauncherEntry>& entries,
                                      const std::string& sender, const std::string& desktopId,
                                      const LauncherUpdate& u) {
    if (desktopId.empty()) { return; }
    auto it = std::ranges::find(entries, desktopId, &LauncherEntry::desktopId);
    if (it == entries.end()) {
        entries.push_back({.desktopId = desktopId});
        it = entries.end() - 1;
    }
    LauncherEntry& e = *it;
    e.sender = sender;
    if (u.count) { e.count = *u.count; }
    if (u.countVisible) { e.countVisible = *u.countVisible; }
    if (u.progress) { e.progress = *u.progress; }
    if (u.progressVisible) { e.progressVisible = *u.progressVisible; }
    if (u.urgent) { e.urgent = *u.urgent; }
    if (u.quicklist) { e.quicklist = *u.quicklist == "/" ? std::string() : *u.quicklist; }
    if (e.badge() == 0 && e.shownProgress() < 0 && !e.urgent && e.quicklist.empty()) {
        entries.erase(it);
    }
}

bool ApplicationsBackend::dropSender(std::vector<LauncherEntry>& entries,
                                     const std::string& sender) {
    return std::erase_if(entries, [&](const LauncherEntry& e) { return e.sender == sender; }) > 0;
}

int ApplicationsBackend::onUpdate(sd_bus_message* m, void* userdata, sd_bus_error*) {
    auto* self = static_cast<ApplicationsBackend*>(userdata);
    const char* sender = sd_bus_message_get_sender(m);
    const char* uri = nullptr;
    if (sender == nullptr || sd_bus_message_read(m, "s", &uri) < 0 || uri == nullptr) { return 0; }
    LauncherUpdate u;
    dbus::forEachProperty(m, [&](std::string_view key) {
        double num = 0;
        bool flag = false;
        std::string path;
        if (key == "count") {
            if (readNumber(m, &num)) { u.count = static_cast<int64_t>(num); }
        } else if (key == "progress") {
            if (readNumber(m, &num)) { u.progress = num; }
        } else if (key == "count-visible") {
            if (dbus::readBool(m, &flag)) { u.countVisible = flag; }
        } else if (key == "progress-visible") {
            if (dbus::readBool(m, &flag)) { u.progressVisible = flag; }
        } else if (key == "urgent") {
            if (dbus::readBool(m, &flag)) { u.urgent = flag; }
        } else if (key == "quicklist") {
            if (readPath(m, &path)) { u.quicklist = path; }
        } else {
            sd_bus_message_skip(m, "v");
        }
    });
    const std::string id = desktopIdFromUri(uri);
    const auto before = self->entries_;
    applyUpdate(self->entries_, sender, id, u);
    if (self->entries_ != before) {
        self->watchSender(sender);
        self->changed();
    }
    return 0;
}

void ApplicationsBackend::watchSender(const std::string& sender) {
    if (std::ranges::any_of(senderSlots_, [&](const auto& s) { return s.first == sender; })) {
        return;
    }
    const std::string rule =
        "type='signal',sender='org.freedesktop.DBus',interface='org.freedesktop.DBus',"
        "member='NameOwnerChanged',arg0='" +
        sender + "'";
    if (sd_bus_slot* slot =
            bus_.addMatch(rule.c_str(), &ApplicationsBackend::onNameOwnerChanged, this)) {
        senderSlots_.emplace_back(sender, slot);
    }
}

int ApplicationsBackend::onNameOwnerChanged(sd_bus_message* m, void* userdata, sd_bus_error*) {
    auto* self = static_cast<ApplicationsBackend*>(userdata);
    const char* name = nullptr;
    const char* oldOwner = nullptr;
    const char* newOwner = nullptr;
    if (sd_bus_message_read(m, "sss", &name, &oldOwner, &newOwner) < 0 || name == nullptr) {
        return 0;
    }
    if (newOwner != nullptr && *newOwner != '\0') { return 0; }
    const std::string gone = name;
    // Release the match from the next loop turn: this callback runs inside it.
    self->loop_.post([self, gone] {
        auto it = std::ranges::find_if(self->senderSlots_,
                                       [&](const auto& s) { return s.first == gone; });
        if (it != self->senderSlots_.end()) {
            sd_bus_slot_unref(it->second);
            self->senderSlots_.erase(it);
        }
    });
    if (dropSender(self->entries_, gone)) { self->changed(); }
    return 0;
}

void ApplicationsBackend::changed() {
    if (onChange_) { onChange_(); }
}

bool ApplicationsBackend::activate(const DesktopEntry& app) {
    if (app.dbusActivatable && callActivate(app, nullptr)) { return true; }
    return launchDetached(loop_, app.exec, app.terminal);
}

bool ApplicationsBackend::activateAction(const DesktopEntry& app, const DesktopAction& action) {
    if (app.dbusActivatable && callActivate(app, &action)) { return true; }
    return launchDetached(loop_, action.exec, app.terminal);
}

bool ApplicationsBackend::callActivate(const DesktopEntry& app, const DesktopAction* action) {
    if (!bus_.available() || app.id.empty()) { return false; }
    auto fallback = std::make_unique<ActivateFallback>(
        ActivateFallback{.loop = &loop_,
                         .exec = action != nullptr ? action->exec : app.exec,
                         .terminal = app.terminal});
    const std::string path = objectPathFor(app.id);
    int r = 0;
    if (action == nullptr) {
        r = sd_bus_call_method_async(bus_.get(), nullptr, app.id.c_str(), path.c_str(), kAppIface,
                                     "Activate", &ApplicationsBackend::onActivateReply,
                                     fallback.get(), "a{sv}", 0);
    } else {
        r = sd_bus_call_method_async(bus_.get(), nullptr, app.id.c_str(), path.c_str(), kAppIface,
                                     "ActivateAction", &ApplicationsBackend::onActivateReply,
                                     fallback.get(), "sava{sv}", action->id.c_str(), 0, 0);
    }
    if (r < 0) { return false; }
    // Owned by the pending call from here on; the reply frees it.
    ActivateFallback* const pending = fallback.release();
    static_cast<void>(pending);
    return true;
}

int ApplicationsBackend::onActivateReply(sd_bus_message* m, void* userdata, sd_bus_error*) {
    const std::unique_ptr<ActivateFallback> fallback(static_cast<ActivateFallback*>(userdata));
    if (sd_bus_message_is_method_error(m, nullptr) && fallback->loop != nullptr) {
        launchDetached(*fallback->loop, fallback->exec, fallback->terminal);
    }
    return 0;
}

}  // namespace qypr
