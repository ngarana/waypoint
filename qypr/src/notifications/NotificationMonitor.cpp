// NotificationMonitor.cpp - See the header for the design.

#include "notifications/NotificationMonitor.hpp"

#include <algorithm>
#include <fstream>
#include <cstdio>
#include <cstring>
#include <ranges>
#include <string>

#include "core/Config.hpp"
#include "core/EventLoop.hpp"
#include "notifications/NotificationLog.hpp"

namespace qypr {

namespace {

constexpr size_t kMaxHeld = 8;      // buffer beyond what the view shows
constexpr size_t kMaxPending = 32;  // unanswered Notify calls to remember

// Desktop Notifications spec: urgency hint levels.
constexpr uint8_t kUrgencyCritical = 2;

// Desktop Notifications spec: NotificationClosed reasons.
constexpr uint32_t kClosedDismissed = 2;  // dismissed by the user
constexpr uint32_t kClosedByCall = 3;     // a CloseNotification call

// Accent palette cycled per card (Catppuccin Mocha); critical is always red.
// Reads the OWNER's live theme (never globals): the monitor is ThemeAware,
// bound by its host alongside every other UI object.
Color accentFor(const theme::State& theme, uint64_t key, uint8_t urgency) {
    if (urgency >= kUrgencyCritical) { return theme.colors.red; }
    const Color accents[] = {
        theme.colors.blue, theme.colors.green, theme.colors.mauve, theme.colors.peach,
        theme.colors.teal, theme.colors.sky,   theme.colors.pink,  theme.colors.yellow,
    };
    return accents[key % (sizeof(accents) / sizeof(accents[0]))];
}

std::vector<std::string> sensitiveApps;

void loadSensitiveApps() {
    if (!sensitiveApps.empty()) { return; }

    // XDG-resolved, not a hardcoded home: this file shipped with an absolute
    // /home/arch path, which silently fell back to the defaults for any other
    // user.
    std::ifstream f(Config::configDir() + "/sensitive_apps.conf");
    if (!f.is_open()) {
        sensitiveApps = {"signal",      "telegram",  "whatsapp",  "discord",
                         "thunderbird", "evolution", "messenger", "org.telegram.desktop",
                         "slack",       "element"};
        return;
    }
    std::string line;
    while (std::getline(f, line)) {
        line.erase(line.begin(),
                   std::ranges::find_if(line, [](unsigned char ch) { return !std::isspace(ch); }));
        line.erase(std::ranges::find_if(std::views::reverse(line),
                                        [](unsigned char ch) { return !std::isspace(ch); })
                       .base(),
                   line.end());
        if (!line.empty() && line[0] != '#') {
            std::ranges::transform(line, line.begin(), ::tolower);
            sensitiveApps.push_back(line);
        }
    }
}

bool isAppSensitive(const std::string& appName) {
    loadSensitiveApps();
    std::string app = appName;
    std::ranges::transform(app, app.begin(), ::tolower);
    for (const auto& sensitive : sensitiveApps) {
        if (app.find(sensitive) != std::string::npos) { return true; }
    }
    return false;
}

// Read a variant holding any integer/boolean type (1) or a string type (2) into out
// parameters; consumes the variant.
int readVariant(sd_bus_message* m, uint64_t& numOut, std::string& strOut) {
    const char* contents = nullptr;
    if (sd_bus_message_peek_type(m, nullptr, &contents) < 0 || (contents == nullptr)) {
        sd_bus_message_skip(m, "v");
        return 0;
    }
    if (contents[1] == '\0' && (std::strchr("ybnqiuxt", contents[0]) != nullptr)) {
        sd_bus_message_enter_container(m, SD_BUS_TYPE_VARIANT, contents);
        bool ok = true;
        switch (contents[0]) {
            case 'y': {
                uint8_t v = 0;
                ok = sd_bus_message_read(m, "y", &v) >= 0;
                numOut = v;
                break;
            }
            case 'b': {
                int v = 0;
                ok = sd_bus_message_read(m, "b", &v) >= 0;
                numOut = static_cast<uint64_t>(v != 0);
                break;
            }
            case 'n': {
                int16_t v = 0;
                ok = sd_bus_message_read(m, "n", &v) >= 0;
                numOut = static_cast<uint64_t>(v);
                break;
            }
            case 'q': {
                uint16_t v = 0;
                ok = sd_bus_message_read(m, "q", &v) >= 0;
                numOut = v;
                break;
            }
            case 'i': {
                int32_t v = 0;
                ok = sd_bus_message_read(m, "i", &v) >= 0;
                numOut = static_cast<uint64_t>(v);
                break;
            }
            case 'u': {
                uint32_t v = 0;
                ok = sd_bus_message_read(m, "u", &v) >= 0;
                numOut = v;
                break;
            }
            case 'x': {
                int64_t v = 0;
                ok = sd_bus_message_read(m, "x", &v) >= 0;
                numOut = static_cast<uint64_t>(v);
                break;
            }
            case 't':
                ok = sd_bus_message_read(m, "t", &numOut) >= 0;
                break;
        }
        sd_bus_message_exit_container(m);
        return ok ? 1 : 0;
    }
    if (std::strcmp(contents, "s") == 0) {
        sd_bus_message_enter_container(m, SD_BUS_TYPE_VARIANT, "s");
        const char* val = nullptr;
        bool const ok = sd_bus_message_read(m, "s", &val) >= 0;
        if (ok && (val != nullptr)) { strOut = val; }
        sd_bus_message_exit_container(m);
        return ok ? 2 : 0;
    }
    sd_bus_message_skip(m, "v");
    return 0;
}

// Ceiling for calls on the monitor connection. This one is short because the
// only call made here is BecomeMonitor, which the bus broker answers itself —
// no service activation to wait on, unlike SystemBus::kCallTimeoutUs.
constexpr uint64_t kCallTimeoutUs = 2'000'000;

}  // namespace

NotificationMonitor::NotificationMonitor(EventLoop& loop) : loop_(loop) {
    notes_.reserve(kMaxHeld);
    pending_.reserve(kMaxPending);
}

NotificationMonitor::~NotificationMonitor() {
    teardown();
}

bool NotificationMonitor::start(bool seedFromLog) {
    sd_bus* bus = nullptr;
    if (sd_bus_open_user(&bus) < 0) {
        std::fprintf(stderr, "qypr-lock: no session bus; notifications disabled\n");
        return false;
    }

    // Pre-lock backlog from a running --record service, if any. Fetched over
    // its own connection, so it is independent of this monitor connection.
    if (seedFromLog) { fetchBacklog(); }

    // Spec: the match rules are the *argument* of BecomeMonitor. Once the
    // reply arrives this connection is receive-only.
    //
    // Bounded rather than left at sd-bus's 25-second default: this call blocks
    // the event loop, and at login the bar comes up alongside the notification
    // daemon and the bus itself. BecomeMonitor is answered by the broker
    // directly, so it normally returns instantly — waiting much longer means
    // something is wrong, and losing notifications beats freezing the bar.
    sd_bus_set_method_call_timeout(bus, kCallTimeoutUs);

    sd_bus_error err = SD_BUS_ERROR_NULL;
    int const r = sd_bus_call_method(
        bus, "org.freedesktop.DBus", "/org/freedesktop/DBus", "org.freedesktop.DBus.Monitoring",
        "BecomeMonitor", &err, nullptr, "asu", 3U,
        "type='method_call',interface='org.freedesktop.Notifications',member='Notify'",
        "type='method_return',sender='org.freedesktop.Notifications'",
        "type='signal',interface='org.freedesktop.Notifications',member='NotificationClosed'", 0U);
    if (r < 0) {
        std::fprintf(stderr, "qypr-lock: BecomeMonitor failed (%s); notifications disabled\n",
                     (err.message != nullptr) ? err.message : strerror(-r));
        sd_bus_error_free(&err);
        sd_bus_unref(bus);
        return false;
    }

    if (sd_bus_add_filter(bus, &filter_, &NotificationMonitor::onMessage, this) < 0) {
        sd_bus_unref(bus);
        return false;
    }

    bus_ = bus;
    fd_ = sd_bus_get_fd(bus_);
    loop_.addFd(fd_, [this](uint32_t) { drain(); });
    drain();  // messages may already sit in sd-bus's queue from the call above
    return true;
}

void NotificationMonitor::fetchBacklog() {
    // Use a dedicated, short-lived connection for the List() call. A previous
    // regression showed that a failed/version-skewed deserialization on the
    // monitor connection could cascade into BecomeMonitor failing and killing
    // all notifications; isolating the fetch here makes that impossible.
    sd_bus* bus = nullptr;
    if (sd_bus_open_user(&bus) < 0) { return; }

    sd_bus_message* reply = nullptr;
    sd_bus_error err = SD_BUS_ERROR_NULL;
    if (sd_bus_call_method(bus, notiflog::kBusName, notiflog::kObjectPath, notiflog::kInterface,
                           "List", &err, &reply, "") < 0) {
        std::fprintf(stderr,
                     "qypr-lock: no notification log service (%s); "
                     "pre-lock backlog unavailable\n",
                     (err.message != nullptr) ? err.message : "?");
        sd_bus_error_free(&err);
        sd_bus_unref(bus);
        return;
    }
    // enter_container only succeeds when the element type matches exactly, so an
    // older record service (7-field records without an icon) is skipped cleanly
    // rather than mis-parsed.
    if (sd_bus_message_enter_container(reply, SD_BUS_TYPE_ARRAY, notiflog::kRecord) > 0) {
        int64_t postedAt = 0;
        const char* app = nullptr;
        const char* title = nullptr;
        const char* body = nullptr;
        const char* icon = nullptr;
        uint32_t daemonId = 0;
        uint8_t urgency = 1;
        int sensitive = 0;
        while (sd_bus_message_read(reply, notiflog::kRecord, &postedAt, &app, &title, &body, &icon,
                                   &daemonId, &urgency, &sensitive) > 0) {
            Notification n;
            n.id = nextKey_++;
            n.postedAt = postedAt;
            n.app = (app != nullptr) ? app : "";
            n.title = (title != nullptr) ? title : "";
            n.body = (body != nullptr) ? body : "";
            n.icon = (icon != nullptr) ? icon : "";
            n.daemonId = daemonId;
            n.urgency = urgency;
            n.sensitive = sensitive != 0;
            n.accent = accentFor(theme(), n.id, urgency);
            notes_.push_back(std::move(n));
        }
        sd_bus_message_exit_container(reply);
        while (notes_.size() > kMaxHeld) { notes_.erase(notes_.begin()); }
        if (!notes_.empty()) { changed(); }
    }
    sd_bus_message_unref(reply);
    sd_bus_unref(bus);
}

void NotificationMonitor::drain() {
    int r = 0;
    while ((r = sd_bus_process(bus_, nullptr)) > 0) {}
    if (r < 0) {
        std::fprintf(stderr, "qypr-lock: notification monitor lost (%s)\n", strerror(-r));
        // Deferred: this runs from the fd callback removeFd would destroy.
        loop_.post([this] { teardown(); });
    }
}

void NotificationMonitor::teardown() {
    if (fd_ >= 0) {
        loop_.removeFd(fd_);
        fd_ = -1;
    }
    if (filter_ != nullptr) {
        sd_bus_slot_unref(filter_);
        filter_ = nullptr;
    }
    if (bus_ != nullptr) {
        sd_bus_unref(bus_);
        bus_ = nullptr;
    }
}

int NotificationMonitor::onMessage(sd_bus_message* m, void* userdata, sd_bus_error* /*unused*/) {
    auto* self = static_cast<NotificationMonitor*>(userdata);
    uint8_t type = 0;
    sd_bus_message_get_type(m, &type);
    if (type == SD_BUS_MESSAGE_METHOD_CALL &&
        (sd_bus_message_is_method_call(m, "org.freedesktop.Notifications", "Notify") != 0)) {
        self->handleNotify(m);
    } else if (type == SD_BUS_MESSAGE_METHOD_RETURN) {
        self->handleReturn(m);
    } else if (sd_bus_message_is_signal(m, "org.freedesktop.Notifications", "NotificationClosed") !=
               0) {
        self->handleClosed(m);
    }
    return 1;  // consumed: nothing else may react to monitored traffic
}

// Notify(s app_name, u replaces_id, s app_icon, s summary, s body,
//        as actions, a{sv} hints, i expire_timeout) → u id
void NotificationMonitor::handleNotify(sd_bus_message* m) {
    const char* app = nullptr;
    const char* icon = nullptr;
    const char* summary = nullptr;
    const char* body = nullptr;
    uint32_t replaces = 0;
    if (sd_bus_message_read(m, "susss", &app, &replaces, &icon, &summary, &body) < 0) { return; }
    std::vector<std::pair<std::string, std::string>> actions;
    if (sd_bus_message_enter_container(m, SD_BUS_TYPE_ARRAY, "s") >= 0) {
        const char* actKey = nullptr;
        while (sd_bus_message_read(m, "s", &actKey) > 0) {
            const char* actLabel = nullptr;
            if (sd_bus_message_read(m, "s", &actLabel) > 0) {
                actions.emplace_back((actKey != nullptr) ? actKey : "",
                                     (actLabel != nullptr) ? actLabel : "");
            } else {
                break;
            }
        }
        sd_bus_message_exit_container(m);
    } else {
        if (sd_bus_message_skip(m, "as") < 0) { return; }
    }

    uint8_t urgency = 1;  // normal
    bool transient = false;
    bool sensitive = isAppSensitive((app != nullptr) ? app : "");
    std::string desktopEntry;

    if (sd_bus_message_enter_container(m, SD_BUS_TYPE_ARRAY, "{sv}") > 0) {
        while (sd_bus_message_enter_container(m, SD_BUS_TYPE_DICT_ENTRY, "sv") > 0) {
            const char* key = nullptr;
            if (sd_bus_message_read(m, "s", &key) < 0) { break; }

            uint64_t valNum = 0;
            std::string valStr;
            int const type = readVariant(m, valNum, valStr);
            if (type == 1) {  // numeric
                if ((key != nullptr) && std::strcmp(key, "urgency") == 0) {
                    urgency = static_cast<uint8_t>(valNum);
                } else if ((key != nullptr) && std::strcmp(key, "transient") == 0) {
                    transient = valNum != 0;
                } else if ((key != nullptr) && (std::strcmp(key, "sensitive") == 0 ||
                                                std::strcmp(key, "x-kde-privacy") == 0)) {
                    sensitive = sensitive || (valNum != 0);
                } else if ((key != nullptr) && std::strcmp(key, "visibility") == 0) {
                    sensitive = sensitive || (valNum < 2);
                }
            } else if (type == 2) {  // string
                if ((key != nullptr) && std::strcmp(key, "visibility") == 0) {
                    if (valStr == "private" || valStr == "secret") { sensitive = true; }
                } else if ((key != nullptr) && (std::strcmp(key, "desktop-entry") == 0 ||
                                                std::strcmp(key, "desktop_entry") == 0)) {
                    // The .desktop id of the sending app — used to launch/focus
                    // it when the user clicks the card.
                    desktopEntry = valStr;
                }
            }
            sd_bus_message_exit_container(m);
        }
        sd_bus_message_exit_container(m);
    }

    if (transient) {
        return;  // volume OSDs and the like: never queued
    }
    if (((summary == nullptr) || ((*summary) == 0)) && ((body == nullptr) || ((*body) == 0))) {
        return;
    }

    Notification n;
    n.daemonId = replaces;
    n.postedAt = nowMs();
    n.app = (app != nullptr) ? app : "";
    n.title = (summary != nullptr) ? summary : "";
    n.body = (body != nullptr) ? body : "";
    n.icon = (icon != nullptr) ? icon : "";
    n.desktopEntry = std::move(desktopEntry);
    n.urgency = urgency;
    n.sensitive = sensitive;
    n.actions = std::move(actions);

    // replaces_id: update the existing card in place (same key, so the view
    // reconciles without re-animating).
    if (replaces != 0) {
        for (auto& existing : notes_) {
            if (existing.daemonId == replaces) {
                n.id = existing.id;
                n.accent = accentFor(theme(), n.id, urgency);
                existing = std::move(n);
                changed();
                return;
            }
        }
    }

    n.id = nextKey_++;
    n.accent = accentFor(theme(), n.id, urgency);

    // The daemon's reply to this call carries the assigned notification id;
    // remember the call so handleReturn() can attach it.
    uint64_t cookie = 0;
    if (sd_bus_message_get_cookie(m, &cookie) >= 0) {
        if (pending_.size() >= kMaxPending) {
            pending_.clear();  // stale, unanswered
        }
        const char* sender = sd_bus_message_get_sender(m);
        pending_.push_back(PendingCall{
            .sender = (sender != nullptr) ? sender : "?", .cookie = cookie, .id = n.id});
    }

    notes_.push_back(std::move(n));
    while (notes_.size() > kMaxHeld) { notes_.erase(notes_.begin()); }
    changed();
}

void NotificationMonitor::handleReturn(sd_bus_message* m) {
    if (pending_.empty()) { return; }
    uint64_t replyCookie = 0;
    if (sd_bus_message_get_reply_cookie(m, &replyCookie) < 0) { return; }
    const char* dest = sd_bus_message_get_destination(m);
    std::string destStr = (dest != nullptr) ? dest : "?";

    auto it = std::ranges::find_if(pending_, [&](const PendingCall& pc) {
        return pc.cookie == replyCookie && pc.sender == destStr;
    });
    if (it == pending_.end()) { return; }
    const uint64_t key = it->id;
    pending_.erase(it);

    const char* sig = sd_bus_message_get_signature(m, 1);
    uint32_t daemonId = 0;
    if ((sig == nullptr) || std::strcmp(sig, "u") != 0 ||
        sd_bus_message_read(m, "u", &daemonId) < 0 || daemonId == 0) {
        return;
    }
    for (auto& n : notes_) {
        if (n.id == key) {
            n.daemonId = daemonId;
            break;
        }
    }
}

// NotificationClosed(u id, u reason)
void NotificationMonitor::handleClosed(sd_bus_message* m) {
    uint32_t id = 0;
    uint32_t reason = 0;
    if (sd_bus_message_read(m, "uu", &id, &reason) < 0 || id == 0) { return; }
    // Expired popups (reason 1) stay — while locked, this stack is the user's
    // queue. Explicit dismissal or an app's CloseNotification removes the card.
    if (reason != kClosedDismissed && reason != kClosedByCall) { return; }
    const size_t before = notes_.size();
    std::erase_if(notes_, [id](const Notification& n) { return n.daemonId == id; });
    if (notes_.size() != before) { changed(); }
}

}  // namespace qypr
