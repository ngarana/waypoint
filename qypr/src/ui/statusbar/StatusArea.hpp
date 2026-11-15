// StatusArea.hpp - State the status area shares across modules.
//
// How the bar decides what appears where, modelled on how desktop shells
// split the notification area (see docs/APPLICATION_STATUS.md):
//
//   1. Native modules own system features (network, Bluetooth, removable
//      drives, phone). Each *claims* the third-party tray applet that
//      duplicates it: the applet's StatusNotifierItem is never drawn, it only
//      backs that module's right-click menu. KDE and GNOME avoid the same
//      duplication at the source (nm-applet ships NotShowIn=KDE;GNOME;); a bar
//      that runs under a compositor session has to do it at runtime.
//   2. Apps report through their own entry (notification-apps): badges,
//      progress, actions.
//   3. Whatever is left — windowless background apps, unclaimed tray items,
//      and modules that are relevant but passive — lives behind one overflow
//      chevron (background-apps), Windows' hidden-icons area.
//
// A claim only holds while the claiming module is in the layout, so removing
// `wifi` from bar.conf hands nm-applet back to the overflow instead of making
// it unreachable.
//
// The StatusBar owns one StatusArea and hands it to every indicator through
// SystemBackends::statusArea.
#pragma once

#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "ui/statusbar/IndicatorCapabilities.hpp"  // OverflowEntry

namespace qypr {

class Config;
class SNIBackend;
struct SNIItem;

class StatusArea {
public:
    // ── Claims ──────────────────────────────────────────────────────────
    // Built-in identities each native module supersedes (lowercase; '*' at
    // the end is a prefix match, at the start a suffix match, at both ends a
    // substring match). Matched against a tray item's Id and, for the app
    // modules, an app's names (AppStatus::matches, which also takes
    // "category:<Name>").
    static std::vector<std::string> defaultClaims(const std::string& moduleId);
    static bool patternMatches(const std::string& pattern, const std::string& value);

    // Modules that show one app's status (whatsapp, email): notification-apps
    // leaves the apps they claim to them.
    static const std::vector<std::string>& appModules();
    // `moduleId`'s claims (defaults plus [tray] claims-<module>), or none
    // while it is not in the layout.
    [[nodiscard]] std::vector<std::string> claimPatterns(const std::string& moduleId) const;

    // Read the [tray] section: `claims-<module>` extends a module's claims,
    // `pinned` / `hidden` list background-app keys (tray Id or desktop id).
    void configure(const Config* config);

    // Indicator ids in the current layout (set by the StatusBar after every
    // load/reload, before any indicator sees a backend update).
    void setLayoutModules(std::vector<std::string> ids) { layout_ = std::move(ids); }
    [[nodiscard]] const std::vector<std::string>& layoutModules() const { return layout_; }
    [[nodiscard]] bool inLayout(const std::string& moduleId) const;

    // In-layout module that claims `key` ("" = unclaimed). `key` is a tray Id,
    // desktop id or app name; matching is case-insensitive.
    [[nodiscard]] std::string claimant(const std::string& key) const;
    [[nodiscard]] bool claimedBy(const std::string& moduleId, const std::string& key) const;
    // Index of the tray item `moduleId` claims, or -1 (first match wins).
    [[nodiscard]] int claimedItem(const std::string& moduleId, const SNIBackend* sni) const;
    [[nodiscard]] bool itemClaimed(const SNIItem& item) const;

    // ── Background-app placement ────────────────────────────────────────
    [[nodiscard]] bool pinned(const std::string& key) const;
    [[nodiscard]] bool hidden(const std::string& key) const;

    // ── Overflow plumbing ───────────────────────────────────────────────
    // The StatusBar installs a provider that collects overflowEntry() from
    // every indicator in the layout; the overflow host reads it.
    void setPassiveProvider(std::function<std::vector<OverflowEntry>()> fn) {
        passiveProvider_ = std::move(fn);
    }
    [[nodiscard]] std::vector<OverflowEntry> passiveEntries() const {
        return passiveProvider_ ? passiveProvider_() : std::vector<OverflowEntry>{};
    }

    // A row in an open overflow asks for a module's flyout. Deferred: the
    // request is taken by the StatusBar after the popover's input handler has
    // returned, so the requesting popover is never destroyed mid-call.
    struct DetailRequest {
        std::string moduleId;     // flyout to open
        std::string anchorId;     // indicator to anchor it to (the overflow chevron)
        bool appletMenu = false;  // the module's applet menu instead (merged flyouts)
    };
    void requestDetail(std::string moduleId, std::string anchorId) {
        request_ = DetailRequest{.moduleId = std::move(moduleId), .anchorId = std::move(anchorId)};
    }
    // A merged flyout's "More from <applet> ›" row: swap the flyout for the
    // applet's menu (StatusIndicator::createAppletMenu), anchored to the module.
    void requestAppletMenu(std::string moduleId) {
        std::string anchor = moduleId;
        request_ = DetailRequest{
            .moduleId = std::move(moduleId), .anchorId = std::move(anchor), .appletMenu = true};
    }
    std::optional<DetailRequest> takeDetailRequest() {
        auto r = std::move(request_);
        request_.reset();
        return r;
    }

private:
    [[nodiscard]] std::vector<std::string> claimsFor(const std::string& moduleId) const;

    std::vector<std::string> layout_;
    std::vector<std::pair<std::string, std::vector<std::string>>> extraClaims_;
    std::vector<std::string> pinned_;
    std::vector<std::string> hidden_;
    std::function<std::vector<OverflowEntry>()> passiveProvider_;
    std::optional<DetailRequest> request_;
};

}  // namespace qypr
