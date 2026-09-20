// WifiIndicator.cpp - Status bar WiFi indicator implementation.
//
// The detailed view is a proper network module, not a one-shot list: it reads
// the backend's live snapshot every frame (the backend pushes scan results),
// carries its own on/off switch, shows a spinner while a scan runs, offers a
// refresh button, and can join *any* network — open ones directly, secured
// ones through an inline passphrase editor fed by the keyboard (NM persists
// the profile via AddAndActivateConnection, so the network becomes saved).
#include "ui/indicators/WifiIndicator.hpp"

#include <algorithm>
#include <string>
#include <vector>

#include <xkbcommon/xkbcommon-keysyms.h>

#include "render/Painter.hpp"
#include "ui/Theme.hpp"
#include "ui/statusbar/IndicatorRegistry.hpp"
#include "ui/statusbar/QSTile.hpp"

namespace qypr {

namespace {
const char* wifiIcon(const WifiSnapshot& s) {
    if (!s.enabled) { return "󰤮"; }
    if (!s.connected) { return "󰤭"; }
    if (s.strength >= 75) { return "󰤨"; }
    if (s.strength >= 50) { return "󰤥"; }
    if (s.strength >= 25) { return "󰤢"; }
    return "󰤯";
}

// freedesktop symbolic names — signal-bar icons with -secure variants.
const char* wifiThemedIcon(const WifiSnapshot& s) {
    if (!s.enabled) { return "network-wireless-disabled-symbolic"; }
    if (!s.connected) { return "network-wireless-disconnected-symbolic"; }
    if (s.strength >= 75) { return "network-wireless-signal-excellent-symbolic"; }
    if (s.strength >= 50) { return "network-wireless-signal-good-symbolic"; }
    if (s.strength >= 25) { return "network-wireless-signal-ok-symbolic"; }
    return "network-wireless-signal-weak-symbolic";
}

// Signal glyph by strength for a picker row.
const char* apGlyph(int strength) {
    if (strength >= 75) { return "󰤨"; }
    if (strength >= 50) { return "󰤥"; }
    if (strength >= 25) { return "󰤢"; }
    return "󰤟";
}

constexpr double kWW = 300.0;
constexpr double kWPad = 12.0;
constexpr double kWHeaderH = 30.0;  // title + switch row
constexpr double kWSubH = 24.0;     // "Visible networks" + refresh row
constexpr double kWRow = 34.0;
constexpr double kWEditorH = 74.0;  // inline passphrase editor
constexpr size_t kWMax = 8;         // one page; scroll reaches the rest
constexpr const char* kLock = "󰌾";
constexpr const char* kCheck = "󰄬";
constexpr const char* kRefresh = "󰑐";

// Animated scan spinner (arc whose head orbits once per second).
void drawSpinner(Painter& p, double cx, double cy, double r, int64_t now, const Color& c) {
    const double phase = static_cast<double>(now % 900) / 900.0;
    cairo_t* cr = p.cr();
    cairo_new_path(cr);  // cairo_arc appends: detach from any leftover path
    cairo_set_source_rgba(cr, c.r, c.g, c.b, c.a);
    cairo_set_line_width(cr, 2.0);
    cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
    const double a0 = phase * 2.0 * M_PI;
    cairo_arc(cr, cx, cy, r, a0, a0 + 1.4);
    cairo_stroke(cr);
}

// Network picker. Fully snapshot-driven: nothing is cached from open time, so
// scan results, radio state, and connection changes appear live.
class WifiPopover : public DetailedPopover {
public:
    explicit WifiPopover(WifiBackend* backend) : backend_(backend) {
        if (backend_ != nullptr && backend_->snapshot().enabled) {
            backend_->requestScan();  // fresh list for this open (async)
        }
    }

    [[nodiscard]] double contentWidth() const override { return kWW; }

    [[nodiscard]] double contentHeight() const override {
        double h = (kWPad * 2) + kWHeaderH + kWSubH;
        if (!enabled()) { return h; }
        const auto& nets = networks();
        if (nets.empty()) {
            h += kWRow;  // "No networks found" row
        } else {
            h += static_cast<double>(std::min(nets.size(), kWMax)) * kWRow;
            if (nets.size() > kWMax) { h += 18.0; }  // scroll hint
        }
        if (!authSsid_.empty()) { h += kWEditorH; }
        return h;
    }

    void draw(Painter& p, int64_t now) override {
        Rect b = getBounds();
        b.y -= (1.0 - openProgress_.value(now)) * 6.0;
        if (!drawSharedBackdrop(p, b, theme::statusbar::popoverRadius)) {
            p.fillRoundedRectSource(b, theme::statusbar::popoverRadius,
                                    theme::statusbar::panelSurface());
        }

        hits_.clear();
        double y = b.y + kWPad;

        // ── Header: title + radio switch ──
        TextStyle const hdr{.family = theme::font::family,
                            .size = 12.0,
                            .weight = PANGO_WEIGHT_BOLD,
                            .color = theme::color::text};
        p.drawText(b.x + kWPad, y + 2.0, "Wi-Fi", hdr);
        drawSwitch(p, {b.x + b.w - kWPad - kSwitchW, y + 1.0, kSwitchW, kSwitchH}, enabled());
        hits_.push_back({.r = {b.x + b.w - kWPad - kSwitchW, y, kSwitchW + kWPad, kSwitchH},
                         .kind = Hit::Kind::Radio});
        y += kWHeaderH;

        if (!enabled()) {
            TextStyle const e{.family = theme::font::family,
                              .size = 12.0,
                              .weight = PANGO_WEIGHT_NORMAL,
                              .color = theme::color::textSubtle};
            p.drawText(b.x + kWPad, y + 10.0, "Wi-Fi is off", e);
            return;
        }

        // ── Subheader: section label + scan spinner / refresh button ──
        TextStyle const sub{.family = theme::font::family,
                            .size = 11.0,
                            .weight = PANGO_WEIGHT_BOLD,
                            .color = theme::color::textSubtle};
        p.drawText(b.x + kWPad, y, "VISIBLE NETWORKS", sub);
        const bool scanning = snap().scanning;
        const Rect refreshRect{b.x + b.w - kWPad - 18.0, y - 2.0, 18.0, 18.0};
        if (scanning) {
            drawSpinner(p, refreshRect.x + 9.0, refreshRect.y + 9.0, 6.0, now,
                        theme::color::primary);
        } else {
            TextStyle const rs{.family = theme::font::iconFamily,
                               .size = 13.0,
                               .weight = PANGO_WEIGHT_NORMAL,
                               .color = refreshRect.contains(hoverX_, hoverY_)
                                            ? theme::color::text
                                            : theme::color::textSubtle};
            p.drawText(refreshRect.x + 2.0, refreshRect.y + 1.0, kRefresh, rs);
        }
        hits_.push_back({.r = refreshRect, .kind = Hit::Kind::Refresh});
        y += kWSubH;

        // ── Network rows (live snapshot) ──
        const auto& nets = networks();
        if (nets.empty()) {
            TextStyle const e{.family = theme::font::family,
                              .size = 12.0,
                              .weight = PANGO_WEIGHT_NORMAL,
                              .color = theme::color::textSubtle};
            p.drawText(b.x + kWPad, y + 8.0, scanning ? "Scanning…" : "No networks found", e);
            return;
        }

        const size_t total = nets.size();
        const size_t first = std::min(scroll_, total > kWMax ? total - kWMax : size_t{0});
        const size_t last = std::min(first + kWMax, total);
        for (size_t i = first; i < last; ++i) {
            const WifiAp& a = nets.at(i);
            const Rect row{.x = b.x + kWPad, .y = y, .w = b.w - (kWPad * 2), .h = kWRow};
            if (row.contains(hoverX_, hoverY_)) {
                p.fillRoundedRect(row, 8.0, theme::color::glassHover);
            }

            TextStyle const gs{.family = theme::font::iconFamily,
                               .size = 15.0,
                               .weight = PANGO_WEIGHT_NORMAL,
                               .color = a.active ? theme::color::primary : theme::color::text};
            p.drawText(row.x + 4.0, row.y + ((kWRow - 16.0) / 2.0), apGlyph(a.strength), gs);

            TextStyle const ns{.family = theme::font::family,
                               .size = 13.0,
                               .weight = a.active ? PANGO_WEIGHT_BOLD : PANGO_WEIGHT_NORMAL,
                               .color = theme::color::text};
            p.drawText(row.x + 30.0, row.y + ((kWRow - 15.0) / 2.0), a.ssid, ns, HAlign::Left,
                       row.w - 90.0);

            // Right cluster: saved hint · lock · active check.
            double rx = row.x + row.w - 6.0;
            if (a.active) {
                TextStyle const cs{.family = theme::font::iconFamily,
                                   .size = 14.0,
                                   .weight = PANGO_WEIGHT_NORMAL,
                                   .color = theme::color::primary};
                const Size cz = p.measureText(kCheck, cs);
                rx -= cz.w;
                p.drawText(rx, row.y + ((kWRow - 14.0) / 2.0), kCheck, cs);
                rx -= 6.0;
            }
            if (a.secured) {
                TextStyle const ls{.family = theme::font::iconFamily,
                                   .size = 12.0,
                                   .weight = PANGO_WEIGHT_NORMAL,
                                   .color = theme::color::textSubtle};
                const Size lz = p.measureText(kLock, ls);
                rx -= lz.w;
                p.drawText(rx, row.y + ((kWRow - 12.0) / 2.0), kLock, ls);
                rx -= 6.0;
            }
            if (a.saved && !a.active) {
                TextStyle const ss{.family = theme::font::family,
                                   .size = 10.0,
                                   .weight = PANGO_WEIGHT_NORMAL,
                                   .color = theme::color::textSubtle};
                const Size sz = p.measureText("saved", ss);
                rx -= sz.w;
                p.drawText(rx, row.y + ((kWRow - 12.0) / 2.0), "saved", ss);
            }

            hits_.push_back({.r = row, .kind = Hit::Kind::Network, .ssid = a.ssid, .net = a});
            y += kWRow;
        }

        if (total > kWMax) {
            TextStyle const m{.family = theme::font::family,
                              .size = 10.0,
                              .weight = PANGO_WEIGHT_NORMAL,
                              .color = theme::color::textSubtle};
            p.drawText(b.x + kWPad, y + 1.0,
                       std::to_string(first + 1) + "–" + std::to_string(last) + " of " +
                           std::to_string(total) + "  ·  scroll for more",
                       m);
            y += 18.0;
        }

        if (!authSsid_.empty()) { drawEditor(p, b, y, now); }
    }

    bool handleClick(double x, double y) override {
        for (const Hit& h : hits_) {
            if (!h.r.contains(x, y)) { continue; }
            switch (h.kind) {
                case Hit::Kind::Radio:
                    if (backend_ != nullptr) { backend_->setEnabled(!enabled()); }
                    authSsid_.clear();
                    return true;
                case Hit::Kind::Refresh:
                    if (backend_ != nullptr) { backend_->requestScan(); }
                    return true;
                case Hit::Kind::Network:
                    return clickNetwork(h);
                case Hit::Kind::Connect:
                    if (backend_ != nullptr && !psk_.empty()) {
                        backend_->connectPsk(authSsid_, psk_);
                    }
                    authSsid_.clear();
                    psk_.clear();
                    return true;
                case Hit::Kind::Cancel:
                    authSsid_.clear();
                    psk_.clear();
                    return true;
            }
        }
        return true;  // swallow clicks inside the popover (never dismiss on a miss)
    }

    // Right-click is the row's context action: the active network disconnects,
    // a saved one is forgotten (its profile deleted), an unsaved one has no
    // context action.
    bool handleSecondaryClick(double x, double y) override {
        for (const Hit& h : hits_) {
            if (!h.r.contains(x, y)) { continue; }
            if (h.kind != Hit::Kind::Network || backend_ == nullptr) { return true; }
            if (h.net.active) {
                backend_->disconnect();
            } else if (h.net.saved) {
                backend_->forgetSsid(h.net.ssid);
            }
            return true;
        }
        return true;  // swallow right-clicks inside the popover (never dismiss)
    }

    bool handleDrag(double x, double y) override {
        hoverX_ = x;
        hoverY_ = y;
        return false;
    }

    bool handleMotion(double x, double y) override {
        hoverX_ = x;
        hoverY_ = y;
        return true;
    }

    bool handleScroll(double /*dx*/, double dy) override {
        const size_t total = networks().size();
        if (total <= kWMax) { return false; }
        const size_t maxScroll = total - kWMax;
        if (dy > 0 && scroll_ > 0) {
            --scroll_;
        } else if (dy < 0 && scroll_ < maxScroll) {
            ++scroll_;
        }
        return true;
    }

    // Keyboard input reaches the popover only while the passphrase editor is
    // open; the host grabs/releases bar keyboard focus around it.
    [[nodiscard]] bool wantsKeyboard() const override { return !authSsid_.empty(); }

    // A waiting password prompt must never time out on the user.
    [[nodiscard]] int autoDismissMs() const override { return authSsid_.empty() ? 6000 : 0; }

    bool handleText(const std::string& utf8) override {
        if (authSsid_.empty()) { return false; }
        if (psk_.size() < 64) { psk_ += utf8; }
        return true;
    }

    bool handleKey(uint32_t keysym) override {
        if (authSsid_.empty()) { return false; }
        switch (keysym) {
            case XKB_KEY_BackSpace:
                if (!psk_.empty()) {
                    size_t i = psk_.size();
                    do {
                        --i;
                    } while (i > 0 && (static_cast<unsigned char>(psk_[i]) & 0xC0) == 0x80);
                    psk_.erase(i);
                }
                return true;
            case XKB_KEY_Return:
            case XKB_KEY_KP_Enter:
                if (backend_ != nullptr && !psk_.empty()) { backend_->connectPsk(authSsid_, psk_); }
                authSsid_.clear();
                psk_.clear();
                return true;
            case XKB_KEY_Escape:
                authSsid_.clear();
                psk_.clear();
                return true;
        }
        return false;
    }

private:
    struct Hit {
        enum class Kind { Radio, Refresh, Network, Connect, Cancel };
        Rect r;
        Kind kind = Kind::Network;
        std::string ssid;
        // Value copy, never a pointer into the backend's snapshot: a push
        // between draw and click can reallocate that vector.
        WifiAp net{};
    };

    [[nodiscard]] const WifiSnapshot& snap() const { return backend_->snapshot(); }
    [[nodiscard]] bool enabled() const { return backend_ != nullptr && snap().enabled; }
    [[nodiscard]] const std::vector<WifiAp>& networks() const { return snap().networks; }

    [[nodiscard]] bool clickNetwork(const Hit& h) {
        if (backend_ == nullptr || h.net.ssid.empty()) { return true; }
        if (h.net.active) {
            backend_->disconnect();  // stay open: the list live-updates
        } else if (h.net.saved) {
            backend_->connectSsid(h.net.ssid);
        } else if (h.net.secured) {
            authSsid_ = h.net.ssid;  // collect a passphrase first
            psk_.clear();
        } else {
            backend_->connectAp(h.net);  // open network: join directly
        }
        return true;
    }

    // Pill switch (on = accent fill, knob right).
    void drawSwitch(Painter& p, const Rect& r, bool on) {
        p.fillRoundedRect(r, r.h / 2.0, on ? theme::color::primary : theme::color::glassHover);
        const double knobR = (r.h - 4.0) / 2.0;
        const double kx = on ? r.x + r.w - knobR - 2.0 : r.x + knobR + 2.0;
        p.fillCircle(kx, r.y + r.h / 2.0, knobR,
                     on ? theme::color::background : theme::color::textSubtle);
    }

    void drawEditor(Painter& p, const Rect& b, double y, int64_t now) {
        const Rect card{b.x + kWPad, y, b.w - (kWPad * 2), kWEditorH - 6.0};
        p.fillRoundedRect(card, 8.0, theme::color::glassHover);

        TextStyle const lbl{.family = theme::font::family,
                            .size = 11.0,
                            .weight = PANGO_WEIGHT_NORMAL,
                            .color = theme::color::textSubtle};
        p.drawText(card.x + 10.0, card.y + 7.0, "Password for " + authSsid_, lbl);

        // Entry box: dots for each typed char, caret blink, hidden text.
        const Rect entry{card.x + 10.0, card.y + 24.0, card.w - 20.0, 22.0};
        p.fillRoundedRect(entry, 6.0, theme::color::background);
        TextStyle const dots{.family = theme::font::family,
                             .size = 12.0,
                             .weight = PANGO_WEIGHT_NORMAL,
                             .color = theme::color::text};
        // One • per typed codepoint (count non-continuation bytes).
        std::string masked;
        for (char c : psk_) {
            if ((static_cast<unsigned char>(c) & 0xC0) != 0x80) { masked += "•"; }
        }
        const bool blink = (now % 1000) < 500;
        p.drawText(entry.x + 8.0, entry.y + 4.0, blink ? masked + "│" : masked, dots);

        // Buttons.
        TextStyle const bs{.family = theme::font::family,
                           .size = 11.5,
                           .weight = PANGO_WEIGHT_MEDIUM,
                           .color = theme::color::background};
        const Rect connect{card.x + card.w - 118.0, card.y + 52.0, 52.0, 18.0};
        const Rect cancel{card.x + card.w - 60.0, card.y + 52.0, 50.0, 18.0};
        p.fillRoundedRect(connect, 9.0,
                          psk_.empty() ? theme::color::primary.withAlpha(0.4)
                                       : theme::color::primary);
        const Size cs = p.measureText("Connect", bs);
        p.drawText(connect.x + (connect.w - cs.w) / 2.0, connect.y + 2.0, "Connect", bs);
        TextStyle const xs{.family = theme::font::family,
                           .size = 11.5,
                           .weight = PANGO_WEIGHT_MEDIUM,
                           .color = theme::color::textSubtle};
        const Size xs2 = p.measureText("Cancel", xs);
        p.drawText(cancel.x + (cancel.w - xs2.w) / 2.0, cancel.y + 2.0, "Cancel", xs);
        hits_.push_back({.r = connect, .kind = Hit::Kind::Connect});
        hits_.push_back({.r = cancel, .kind = Hit::Kind::Cancel});
    }

    WifiBackend* backend_ = nullptr;
    std::vector<Hit> hits_;
    size_t scroll_ = 0;
    double hoverX_ = -1, hoverY_ = -1;
    std::string authSsid_;  // non-empty while the passphrase editor is open
    std::string psk_;
    static constexpr double kSwitchW = 34.0;
    static constexpr double kSwitchH = 18.0;
};
}  // namespace

WifiIndicator::WifiIndicator(const SystemBackends& backends)
    : StatusIndicator("wifi", Zone::Right, 300),
      backend_(backends.wifi) {
    // Hidden until this indicator's own backend publishes a snapshot — no
    // placeholder glyph standing in for data we do not have. StateCache
    // normally seeds that snapshot before the first frame, so this is only
    // visibly empty on a first-ever run or when the daemon never answers.
    // Without a backend (tests, registry previews) render sample defaults.
    loaded_ = (backend_ == nullptr);
    visible = loaded_;
}

std::string WifiIndicator::icon() const {
    if (!loaded_) {
        return "󰖩";  // no data yet: neutral glyph for the QS tile (the bar hides)
    }
    return wifiIcon(lastSnap_);
}

std::string WifiIndicator::themedIcon() const {
    if (!loaded_) {
        return "";  // no data yet: fall back to the neutral glyph
    }
    return wifiThemedIcon(lastSnap_);
}

std::string WifiIndicator::tooltip() const {
    if (!lastSnap_.enabled) { return "WiFi off"; }
    if (!lastSnap_.connected) { return "WiFi disconnected"; }
    return lastSnap_.ssid + " (" + std::to_string(lastSnap_.strength) + "%)";
}

void WifiIndicator::onBackendUpdate() {
    if (backend_ == nullptr) { return; }
    lastSnap_ = backend_->snapshot();
    // Only *this* backend's readiness reveals the indicator — a push from an
    // unrelated backend must not mark us loaded with a still-empty snapshot.
    loaded_ = backend_->ready();
    visible = loaded_ && lastSnap_.available;
}

std::unique_ptr<QSTile> WifiIndicator::createTile() {
    auto* snap = &lastSnap_;
    auto* backend = backend_;
    return std::make_unique<QSToggleTile>(
        "WiFi", "󰤨", [snap]() { return snap->enabled; },
        [backend, snap]() {
            if (backend) { backend->setEnabled(!snap->enabled); }
        },
        [snap]() -> std::string {
            if (!snap->enabled) { return "Off"; }
            if (!snap->connected) { return "Not connected"; }
            return snap->ssid;
        });
}

std::unique_ptr<DetailedPopover> WifiIndicator::createDetailedView() {
    if (backend_ == nullptr) { return nullptr; }
    return std::make_unique<WifiPopover>(backend_);
}

REGISTER_INDICATOR("wifi", Zone::Right, 300, WifiIndicator)

}  // namespace qypr
