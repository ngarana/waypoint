// PhoneLinkIndicator.cpp - Paired phone module.
#include "ui/indicators/PhoneLinkIndicator.hpp"

#include <algorithm>
#include <array>
#include <utility>

#include "system/DesktopIndex.hpp"
#include "system/KdeConnectBackend.hpp"
#include "ui/Theme.hpp"
#include "ui/statusbar/IndicatorRegistry.hpp"
#include "ui/statusbar/ListFlyout.hpp"
#include "ui/statusbar/MenuSource.hpp"

namespace qypr {

namespace {

// Nerd Font v3 Material Design glyphs (nf-md-*, MDI codepoints).
constexpr const char* kGlyphPhone = "\U000F011C";     // cellphone
constexpr const char* kGlyphRing = "\U000F009E";      // bell_ring
constexpr const char* kGlyphBell = "\U000F009A";      // bell
constexpr const char* kGlyphShare = "\U000F0497";     // share_variant
constexpr const char* kGlyphFolder = "\U000F0770";    // folder_open
constexpr const char* kGlyphPair = "\U000F0337";      // link
constexpr const char* kGlyphAlert = "\U000F0028";     // alert_circle
constexpr const char* kGlyphSettings = "\U000F0493";  // cog

bool connected(const PhoneDevice& d) {
    return d.paired && d.reachable;
}

const PhoneDevice* primary(const PhoneLinkSnapshot& s) {
    for (const auto& d : s.devices) {
        if (connected(d)) { return &d; }
    }
    return nullptr;
}

bool pairingRequested(const PhoneLinkSnapshot& s) {
    return std::ranges::any_of(s.devices, &PhoneDevice::pairRequestedByPeer);
}

std::string signalBars(int strength) {
    constexpr std::array<const char*, 4> kBars{"▂", "▄", "▆", "█"};
    std::string out;
    for (int i = 0; i < std::clamp(strength, 0, 4); ++i) {
        out += kBars.at(static_cast<size_t>(i));
    }
    return out;
}

void launchSettings(EventLoop* loop) {
    if (loop != nullptr) { launchDetached(*loop, "kdeconnect-app", /*terminal=*/false); }
}

}  // namespace

PhoneLinkIndicator::PhoneLinkIndicator(const SystemBackends& backends)
    : StatusIndicator("phone-link", Zone::Right, 262),
      backend_(backends.kdeConnect),
      loop_(backends.loop),
      tray_(backends, "phone-link") {
    refreshVisibility();
}

const PhoneLinkSnapshot* PhoneLinkIndicator::snap() const {
    return backend_ != nullptr ? &backend_->snapshot() : nullptr;
}

void PhoneLinkIndicator::onBackendUpdate() {
    refreshVisibility();
}

void PhoneLinkIndicator::refreshVisibility() {
    const auto* s = snap();
    visible = s != nullptr && (primary(*s) != nullptr || pairingRequested(*s));
}

std::string PhoneLinkIndicator::deviceStatus(const PhoneDevice& d) {
    if (d.pairRequestedByPeer && !d.paired) { return "Wants to pair"; }
    if (!d.reachable) { return "Not connected"; }
    std::string out;
    if (d.charge >= 0) {
        out = std::to_string(d.charge) + "%";
        if (d.charging) { out += " · charging"; }
    }
    if (!d.network.empty() || d.signal >= 0) {
        if (!out.empty()) { out += " · "; }
        out += d.network;
        if (d.signal >= 0) { out += (d.network.empty() ? "" : " ") + signalBars(d.signal); }
    }
    return out.empty() ? std::string("Connected") : out;
}

std::string PhoneLinkIndicator::icon() const {
    return kGlyphPhone;
}

std::string PhoneLinkIndicator::themedIcon() const {
    return "phone-symbolic";
}

Color PhoneLinkIndicator::iconColor() const {
    const auto* s = snap();
    if (s != nullptr && pairingRequested(*s)) { return theme().colors.warning; }
    return StatusIndicator::iconColor();
}

std::string PhoneLinkIndicator::tooltip() const {
    const auto* s = snap();
    if (s == nullptr || !s->available) { return "KDE Connect is not running"; }
    for (const auto& d : s->devices) {
        if (d.pairRequestedByPeer && !d.paired) { return d.name + " wants to pair"; }
    }
    const PhoneDevice* p = primary(*s);
    if (p == nullptr) {
        return s->devices.empty() ? std::string("No paired phone")
                                  : s->devices.front().name + " · Not connected";
    }
    std::string out = p->name + " · " + deviceStatus(*p);
    if (!p->notifications.empty()) {
        const size_t n = p->notifications.size();
        out += "\n" + std::to_string(n) + (n == 1 ? " notification" : " notifications");
    }
    return out;
}

bool PhoneLinkIndicator::hasDetailedView() const {
    const auto* s = snap();
    return s != nullptr && s->available && !s->devices.empty();
}

FlyoutModel PhoneLinkIndicator::buildFlyout(const PhoneLinkSnapshot& snap,
                                            KdeConnectBackend* backend, EventLoop* loop) {
    FlyoutModel m;
    m.title = "Phone";
    m.empty = snap.available ? "No paired phone" : "KDE Connect is not running";
    auto call = [backend](void (KdeConnectBackend::*fn)(const std::string&), std::string id) {
        return [backend, fn, id = std::move(id)] {
            if (backend != nullptr) { (backend->*fn)(id); }
        };
    };

    for (const auto& d : snap.devices) {
        if (!d.pairRequestedByPeer || d.paired) { continue; }
        FlyoutRow row;
        row.glyph = kGlyphPair;
        row.title = d.name + " wants to pair";
        row.subtitle = "Check the request on the other device";
        row.tone = FlyoutRow::Tone::Normal;
        row.actions.push_back(
            {.label = "Accept", .run = call(&KdeConnectBackend::acceptPairing, d.id)});
        row.actions.push_back(
            {.label = "Reject", .run = call(&KdeConnectBackend::rejectPairing, d.id)});
        m.rows.push_back(std::move(row));
    }

    for (const auto& d : snap.devices) {
        if (!connected(d)) { continue; }
        m.rows.push_back({.glyph = kGlyphPhone, .title = d.name, .subtitle = deviceStatus(d)});
        if (d.has("kdeconnect_findmyphone")) {
            FlyoutRow ring{.glyph = kGlyphRing, .title = "Find my phone"};
            ring.actions.push_back({.label = "Ring", .run = call(&KdeConnectBackend::ring, d.id)});
            m.rows.push_back(std::move(ring));
        }
        FlyoutRow send{.glyph = kGlyphShare, .title = "Send to phone"};
        if (d.has("kdeconnect_clipboard")) {
            send.actions.push_back(
                {.label = "Clipboard", .run = call(&KdeConnectBackend::sendClipboard, d.id)});
        }
        if (d.has("kdeconnect_share")) {
            send.actions.push_back(
                {.label = "Files…", .run = call(&KdeConnectBackend::shareFiles, d.id)});
        }
        if (!send.actions.empty()) { m.rows.push_back(std::move(send)); }
        if (d.has("kdeconnect_sftp")) {
            FlyoutRow files{.glyph = kGlyphFolder, .title = "Phone storage"};
            files.actions.push_back(
                {.label = "Browse", .run = call(&KdeConnectBackend::browse, d.id), .closes = true});
            m.rows.push_back(std::move(files));
        }
        if (!d.notifications.empty()) {
            m.rows.push_back({.kind = FlyoutRow::Kind::Section, .title = "NOTIFICATIONS"});
            const size_t shown = std::min(d.notifications.size(), kMaxNotifications);
            for (size_t i = 0; i < shown; ++i) {
                const auto& n = d.notifications[i];
                FlyoutRow row{.glyph = kGlyphBell};
                row.title = n.title.empty() ? n.app : n.app + " — " + n.title;
                row.subtitle = n.text;
                if (n.dismissable) {
                    row.actions.push_back(
                        {.label = "Dismiss", .run = [backend, dev = d.id, id = n.id] {
                             if (backend != nullptr) { backend->dismissNotification(dev, id); }
                         }});
                }
                m.rows.push_back(std::move(row));
            }
            if (d.notifications.size() > shown) {
                m.rows.push_back({.kind = FlyoutRow::Kind::Note,
                                  .title = std::to_string(d.notifications.size() - shown) +
                                           " more on the phone"});
            }
        }
    }

    bool sectioned = false;
    for (const auto& d : snap.devices) {
        if (!d.paired || d.reachable) { continue; }
        if (!sectioned) {
            m.rows.push_back({.kind = FlyoutRow::Kind::Section, .title = "NOT CONNECTED"});
            sectioned = true;
        }
        m.rows.push_back({.glyph = kGlyphPhone, .title = d.name, .subtitle = deviceStatus(d)});
    }

    if (!snap.error.empty()) {
        FlyoutRow err{.glyph = kGlyphAlert, .title = snap.error, .tone = FlyoutRow::Tone::Error};
        err.actions.push_back({.label = "Dismiss", .run = [backend] {
                                   if (backend != nullptr) { backend->clearError(); }
                               }});
        m.rows.push_back(std::move(err));
    }

    if (snap.available && loop != nullptr) {
        FlyoutRow settings{.glyph = kGlyphSettings, .title = "KDE Connect"};
        settings.actions.push_back(
            {.label = "Settings", .run = [loop] { launchSettings(loop); }, .closes = true});
        m.rows.push_back(std::move(settings));
    }
    return m;
}

std::unique_ptr<DetailedPopover> PhoneLinkIndicator::createDetailedView() {
    if (backend_ == nullptr) { return nullptr; }
    return std::make_unique<ListFlyout>([backend = backend_, loop = loop_] {
        return buildFlyout(backend->snapshot(), backend, loop);
    });
}

bool PhoneLinkIndicator::hasContextMenu() const {
    return hasDetailedView() || tray_.bound();
}

std::unique_ptr<DetailedPopover> PhoneLinkIndicator::createContextMenu(double x, double y) {
    auto native = std::make_unique<NativeMenu>();
    if (const auto* s = snap(); s != nullptr && backend_ != nullptr) {
        KdeConnectBackend* b = backend_;
        for (const auto& d : s->devices) {
            if (d.pairRequestedByPeer && !d.paired) {
                native->add("Accept pairing with " + d.name,
                            [b, id = d.id] { b->acceptPairing(id); });
                native->add("Reject pairing with " + d.name,
                            [b, id = d.id] { b->rejectPairing(id); });
            }
        }
        if (const PhoneDevice* p = primary(*s)) {
            const std::string id = p->id;
            native->add(
                "Ring " + p->name, [b, id] { b->ring(id); }, p->has("kdeconnect_findmyphone"));
            native->add(
                "Send clipboard", [b, id] { b->sendClipboard(id); },
                p->has("kdeconnect_clipboard"));
            native->add("Send files…", [b, id] { b->shareFiles(id); }, p->has("kdeconnect_share"));
            native->add(
                "Browse phone storage", [b, id] { b->browse(id); }, p->has("kdeconnect_sftp"));
            native->add("Ping", [b, id] { b->ping(id); }, p->has("kdeconnect_ping"));
        }
        if (s->available && loop_ != nullptr) {
            native->separator();
            native->add("KDE Connect settings", [loop = loop_] { launchSettings(loop); });
        }
    }
    return tray_.contextMenu(x, y, std::move(native), "Phone");
}

OverflowEntry PhoneLinkIndicator::overflowEntry() const {
    const auto* s = snap();
    if (visible || s == nullptr || s->devices.empty()) { return {}; }
    const PhoneDevice& d = s->devices.front();
    return {.id = id(),
            .title = d.name,
            .subtitle = deviceStatus(d),
            .iconName = "phone-symbolic",
            .glyph = kGlyphPhone};
}

using KdeConnectIndicator = PhoneLinkIndicator;
using PhoneConnectIndicator = PhoneLinkIndicator;
REGISTER_SESSION_INDICATOR("phone-link", Zone::Right, 262, PhoneLinkIndicator)
REGISTER_SESSION_INDICATOR("kde-connect", Zone::Right, 262, KdeConnectIndicator)
REGISTER_SESSION_INDICATOR("phone-connect", Zone::Right, 262, PhoneConnectIndicator)

}  // namespace qypr
