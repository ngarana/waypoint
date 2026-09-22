// BluetoothIndicator.cpp - Status bar Bluetooth indicator implementation.
#include "ui/indicators/BluetoothIndicator.hpp"

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
constexpr double kBW = 300.0;
constexpr double kBPad = 12.0;
constexpr double kBHdrH = 30.0;  // title + radio switch row
constexpr double kBSubH = 22.0;  // section label row
constexpr double kBRow = 42.0;   // device row
constexpr double kBNote = 26.0;  // "no devices" / "scanning" line
constexpr double kBErrH = 22.0;  // error line
constexpr double kBBtnH = 18.0;  // prompt button
constexpr size_t kBMax = 7;      // rows on one page; scroll reaches the rest
constexpr const char* kBRefresh = "󰑐";
constexpr const char* kBCheck = "󰄬";

// A device glyph from BlueZ's freedesktop Icon category.
const char* btGlyph(const std::string& icon) {
    if (icon.find("headset") != std::string::npos) { return "󰋎"; }
    if (icon.find("headphone") != std::string::npos) { return "󰋋"; }
    if (icon.find("phone") != std::string::npos) { return "󰏳"; }
    if (icon.find("audio") != std::string::npos || icon.find("speaker") != std::string::npos) {
        return "󰓃";
    }
    if (icon.find("keyboard") != std::string::npos) { return "󰌌"; }
    if (icon.find("mouse") != std::string::npos) { return "󰦋"; }
    return "󰂯";  // generic bluetooth
}

// Animated spinner (arc whose head orbits once per second), matching the WiFi
// picker's scan indicator.
void drawBtSpinner(Painter& p, double cx, double cy, double r, int64_t now, const Color& c) {
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

// Device picker. Fully snapshot-driven like the WiFi one: paired devices first,
// then whatever an active scan turns up. Clicking a paired device connects or
// disconnects it; clicking a discovered one pairs it (Pair → Trust → Connect).
// Right-click forgets a paired device. Discovery runs only while the picker is
// open — it keeps the radio busy, so it stops on close.
class BluetoothPopover : public DetailedPopover {
public:
    explicit BluetoothPopover(BluetoothBackend* backend) : backend_(backend) {
        if (backend_ != nullptr) {
            backend_->startDiscovery();  // no-op until the adapter is powered
        }
    }

    ~BluetoothPopover() override {
        if (backend_ != nullptr) { backend_->stopDiscovery(); }
    }

    // The destructor stops discovery, so this must not be copied or moved —
    // a second owner would stop a scan the first is still using.
    BluetoothPopover(const BluetoothPopover&) = delete;
    BluetoothPopover& operator=(const BluetoothPopover&) = delete;
    BluetoothPopover(BluetoothPopover&&) = delete;
    BluetoothPopover& operator=(BluetoothPopover&&) = delete;

    [[nodiscard]] double contentWidth() const override { return kBW; }

    [[nodiscard]] double contentHeight() const override {
        double h = (kBPad * 2) + kBHdrH + promptHeight(snap().pairing);
        if (!powered()) { return h + kBNote; }
        const std::vector<Row> rows = buildRows();
        const size_t first = firstRow(rows.size());
        for (size_t i = first; i < std::min(first + kBMax, rows.size()); ++i) {
            h += rowHeight(rows.at(i));
        }
        if (rows.size() > kBMax) { h += 18.0; }  // scroll hint
        if (!snap().error.empty()) { h += kBErrH; }
        return h;
    }

    void draw(Painter& p, int64_t now) override {
        Rect b = getBounds();
        b.y -= (1.0 - openProgress_.value(now)) * 6.0;
        if (!drawSharedBackdrop(p, b, theme().statusbar.popoverRadius)) {
            p.fillRoundedRectSource(b, theme().statusbar.popoverRadius, theme().panelSurface());
        }

        hits_.clear();
        double y = b.y + kBPad;

        // ── Header: title + radio switch ──
        TextStyle const hdr{.family = theme().font.family,
                            .size = 12.0,
                            .weight = PANGO_WEIGHT_BOLD,
                            .color = theme().colors.text};
        p.drawText(b.x + kBPad, y + 2.0, "Bluetooth", hdr);
        drawSwitch(p, {b.x + b.w - kBPad - kSwitchW, y + 1.0, kSwitchW, kSwitchH}, powered());
        hits_.push_back({.r = {b.x + b.w - kBPad - kSwitchW, y, kSwitchW + kBPad, kSwitchH},
                         .kind = Hit::Kind::Radio});
        y += kBHdrH;

        if (!powered()) {
            TextStyle const e{.family = theme().font.family,
                              .size = 12.0,
                              .weight = PANGO_WEIGHT_NORMAL,
                              .color = theme().colors.textSubtle};
            p.drawText(b.x + kBPad, y + 4.0, "Bluetooth is off", e);
            // A prompt outlasting the radio is contradictory, but BlueZ still
            // holds a call open until it is answered — never hide the answer.
            drawPrompt(p, b, y + kBNote, now);
            return;
        }

        const std::vector<Row> rows = buildRows();
        const size_t total = rows.size();
        const size_t first = firstRow(total);
        const size_t last = std::min(first + kBMax, total);
        for (size_t i = first; i < last; ++i) {
            const Row& r = rows.at(i);
            switch (r.kind) {
                case Row::Kind::Section:
                    drawSection(p, b, y, r, now);
                    break;
                case Row::Kind::Note:
                    drawNote(p, b, y, r);
                    break;
                case Row::Kind::Device:
                    drawDevice(p, b, y, r.dev, now);
                    break;
            }
            y += rowHeight(r);
        }

        if (total > kBMax) {
            TextStyle const m{.family = theme().font.family,
                              .size = 10.0,
                              .weight = PANGO_WEIGHT_NORMAL,
                              .color = theme().colors.textSubtle};
            p.drawText(b.x + kBPad, y + 1.0, "scroll for more", m);
            y += 18.0;
        }

        if (!snap().error.empty()) {
            TextStyle const es{.family = theme().font.family,
                               .size = 11.0,
                               .weight = PANGO_WEIGHT_NORMAL,
                               .color = theme().colors.error};
            p.drawText(b.x + kBPad, y + 4.0, snap().error, es, HAlign::Left, b.w - (kBPad * 2));
            y += kBErrH;
        }

        drawPrompt(p, b, y, now);
    }

    bool handleClick(double x, double y) override {
        for (const Hit& h : hits_) {
            if (!h.r.contains(x, y)) { continue; }
            switch (h.kind) {
                case Hit::Kind::Radio:
                    if (backend_ != nullptr) { backend_->setPowered(!powered()); }
                    return true;
                case Hit::Kind::Refresh:
                    if (backend_ != nullptr) { backend_->startDiscovery(); }
                    return true;
                case Hit::Kind::Device:
                    return clickDevice(h.dev);
                case Hit::Kind::PromptAccept:
                    acceptPrompt();
                    return true;
                case Hit::Kind::PromptCancel:
                    if (backend_ != nullptr) { backend_->respondPairing(false); }
                    entry_.clear();
                    return true;
            }
        }
        return true;  // swallow clicks inside the popover (never dismiss on a miss)
    }

    // Right-click is the row's context action: forget a paired device so it
    // onboards from scratch next time. Mirrors "forget network" in the WiFi
    // picker. A discovered-but-unpaired device has no context action.
    bool handleSecondaryClick(double x, double y) override {
        for (const Hit& h : hits_) {
            if (!h.r.contains(x, y)) { continue; }
            if (h.kind == Hit::Kind::Device && h.dev.paired && backend_ != nullptr) {
                backend_->forgetDevice(h.dev.path);
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

    // Without this the row highlight only tracked a held button, so hovering
    // the picker did nothing (the WiFi popover has always had both).
    bool handleMotion(double x, double y) override {
        hoverX_ = x;
        hoverY_ = y;
        return true;
    }

    bool handleScroll(double /*dx*/, double dy) override {
        const size_t total = buildRows().size();
        if (total <= kBMax) { return false; }
        const size_t maxScroll = total - kBMax;
        if (dy > 0 && scroll_ > 0) {
            --scroll_;
        } else if (dy < 0 && scroll_ < maxScroll) {
            ++scroll_;
        }
        return true;
    }

    // A running scan must not be cut short by the idle timeout (discovery takes
    // longer than the default dismiss window, and closing stops it), and a
    // pairing prompt must never time out on the user while BlueZ waits.
    [[nodiscard]] int autoDismissMs() const override {
        return (snap().discovering || snap().pairing.active()) ? 0 : 6000;
    }

    // Keyboard reaches the popover only while a passkey/PIN entry is up; the
    // host grabs and releases bar keyboard focus around it.
    [[nodiscard]] bool wantsKeyboard() const override {
        return snap().pairing.kind == BtPairRequest::Kind::Entry;
    }

    bool handleText(const std::string& utf8) override {
        if (!wantsKeyboard()) { return false; }
        // A BlueZ passkey is six digits; a legacy PIN is up to 16 characters.
        const size_t limit = snap().pairing.numericEntry ? 6 : 16;
        if (snap().pairing.numericEntry && (utf8.size() != 1 || utf8[0] < '0' || utf8[0] > '9')) {
            return true;  // swallow non-digits rather than let them show up
        }
        if (entry_.size() < limit) { entry_ += utf8; }
        return true;
    }

    bool handleKey(uint32_t keysym) override {
        if (!wantsKeyboard()) { return false; }
        switch (keysym) {
            case XKB_KEY_BackSpace:
                if (!entry_.empty()) {
                    size_t i = entry_.size();
                    do {
                        --i;
                    } while (i > 0 && (static_cast<unsigned char>(entry_[i]) & 0xC0) == 0x80);
                    entry_.erase(i);
                }
                return true;
            case XKB_KEY_Return:
            case XKB_KEY_KP_Enter:
                acceptPrompt();
                return true;
            case XKB_KEY_Escape:
                if (backend_ != nullptr) { backend_->respondPairing(false); }
                entry_.clear();
                return true;
            default:
                return false;
        }
    }

private:
    struct Row {
        enum class Kind { Section, Device, Note };
        Kind kind = Kind::Device;
        std::string label;
        // Value copy, never a pointer into the backend's snapshot: a push
        // between draw and click can reallocate that vector.
        BtDevice dev{};
    };

    struct Hit {
        enum class Kind { Radio, Refresh, Device, PromptAccept, PromptCancel };
        Rect r;
        Kind kind = Kind::Device;
        BtDevice dev{};
    };

    [[nodiscard]] const BluetoothSnapshot& snap() const { return backend_->snapshot(); }
    [[nodiscard]] bool powered() const { return backend_ != nullptr && snap().powered; }

    static double rowHeight(const Row& r) {
        switch (r.kind) {
            case Row::Kind::Section:
                return kBSubH;
            case Row::Kind::Note:
                return kBNote;
            case Row::Kind::Device:
                return kBRow;
        }
        return kBRow;
    }

    [[nodiscard]] size_t firstRow(size_t total) const {
        return std::min(scroll_, total > kBMax ? total - kBMax : size_t{0});
    }

    // Flat row list: two labelled sections with their devices inlined, so
    // scrolling and hit-testing stay uniform across headings and rows.
    [[nodiscard]] std::vector<Row> buildRows() const {
        std::vector<Row> rows;
        const BluetoothSnapshot& s = snap();

        std::vector<BtDevice> paired;
        std::vector<BtDevice> available;
        for (const BtDevice& d : s.devices) { (d.paired ? paired : available).push_back(d); }
        std::ranges::stable_sort(
            paired, [](const BtDevice& a, const BtDevice& b) { return a.connected > b.connected; });
        std::ranges::stable_sort(
            available, [](const BtDevice& a, const BtDevice& b) { return a.name < b.name; });

        rows.push_back({.kind = Row::Kind::Section, .label = "MY DEVICES"});
        if (paired.empty()) {
            rows.push_back({.kind = Row::Kind::Note, .label = "No paired devices"});
        }
        for (const BtDevice& d : paired) { rows.push_back({.kind = Row::Kind::Device, .dev = d}); }

        if (s.discovering || !available.empty()) {
            rows.push_back({.kind = Row::Kind::Section, .label = "AVAILABLE"});
            for (const BtDevice& d : available) {
                rows.push_back({.kind = Row::Kind::Device, .dev = d});
            }
            if (available.empty()) {
                rows.push_back({.kind = Row::Kind::Note, .label = "Scanning…"});
            }
        }
        return rows;
    }

    // The subtitle under a device name. The busy row reports which operation is
    // running, inferred from the state it is leaving.
    [[nodiscard]] std::string subtitle(const BtDevice& d) const {
        if (snap().busy == d.path) {
            if (!d.paired) { return "Pairing…"; }
            return d.connected ? "Disconnecting…" : "Connecting…";
        }
        if (d.connected) {
            std::string out = "Connected";
            if (d.battery >= 0) { out += "  ·  " + std::to_string(d.battery) + "%"; }
            return out;
        }
        return d.paired ? "Disconnected" : "Tap to pair";
    }

    [[nodiscard]] bool clickDevice(const BtDevice& d) {
        if (backend_ == nullptr || d.path.empty()) { return true; }
        if (snap().busy == d.path) { return true; }  // an operation is already running
        if (!d.paired) {
            backend_->pairDevice(d.path);
        } else if (d.connected) {
            backend_->disconnectDevice(d.path);
        } else {
            backend_->connectDevice(d.path);
        }
        return true;
    }

    // Section heading; the first one carries the scan spinner / refresh button.
    void drawSection(Painter& p, const Rect& b, double y, const Row& r, int64_t now) {
        TextStyle const sub{.family = theme().font.family,
                            .size = 11.0,
                            .weight = PANGO_WEIGHT_BOLD,
                            .color = theme().colors.textSubtle};
        p.drawText(b.x + kBPad, y, r.label, sub);
        if (r.label != "MY DEVICES") { return; }

        const Rect refresh{b.x + b.w - kBPad - 18.0, y - 3.0, 18.0, 18.0};
        if (snap().discovering) {
            drawBtSpinner(p, refresh.x + 9.0, refresh.y + 9.0, 6.0, now, theme().colors.primary);
        } else {
            TextStyle const rs{.family = theme().font.iconFamily,
                               .size = 13.0,
                               .weight = PANGO_WEIGHT_NORMAL,
                               .color = refresh.contains(hoverX_, hoverY_)
                                            ? theme().colors.text
                                            : theme().colors.textSubtle};
            p.drawText(refresh.x + 2.0, refresh.y + 1.0, kBRefresh, rs);
        }
        hits_.push_back({.r = refresh, .kind = Hit::Kind::Refresh});
    }

    void drawNote(Painter& p, const Rect& b, double y, const Row& r) {
        TextStyle const e{.family = theme().font.family,
                          .size = 12.0,
                          .weight = PANGO_WEIGHT_NORMAL,
                          .color = theme().colors.textSubtle};
        p.drawText(b.x + kBPad, y + 4.0, r.label, e);
    }

    void drawDevice(Painter& p, const Rect& b, double y, const BtDevice& d, int64_t now) {
        const Rect row{.x = b.x + kBPad, .y = y, .w = b.w - (kBPad * 2), .h = kBRow};
        if (row.contains(hoverX_, hoverY_)) {
            p.fillRoundedRect(row, 8.0, theme().colors.glassHover);
        }

        const Color accent = d.connected ? theme().colors.primary : theme().colors.text;
        TextStyle const gs{.family = theme().font.iconFamily,
                           .size = 18.0,
                           .weight = PANGO_WEIGHT_NORMAL,
                           .color = d.paired ? accent : theme().colors.textSubtle};
        p.drawText(row.x + 6.0, row.y + ((kBRow - 20.0) / 2.0), btGlyph(d.icon), gs);

        TextStyle const ns{.family = theme().font.family,
                           .size = 13.0,
                           .weight = d.connected ? PANGO_WEIGHT_BOLD : PANGO_WEIGHT_NORMAL,
                           .color = theme().colors.text};
        p.drawText(row.x + 34.0, row.y + 6.0, d.name.empty() ? "Device" : d.name, ns, HAlign::Left,
                   row.w - 66.0);

        const bool busy = snap().busy == d.path;
        TextStyle const ss{.family = theme().font.family,
                           .size = 11.0,
                           .weight = PANGO_WEIGHT_NORMAL,
                           .color =
                               d.connected ? theme().colors.primary : theme().colors.textSubtle};
        p.drawText(row.x + 34.0, row.y + 23.0, subtitle(d), ss);

        // Right cluster: a spinner while an operation runs, otherwise a check
        // on the connected device.
        if (busy) {
            drawBtSpinner(p, row.x + row.w - 14.0, row.y + (kBRow / 2.0), 6.0, now,
                          theme().colors.primary);
        } else if (d.connected) {
            TextStyle const cs{.family = theme().font.iconFamily,
                               .size = 14.0,
                               .weight = PANGO_WEIGHT_NORMAL,
                               .color = theme().colors.primary};
            const Size cz = p.measureText(kBCheck, cs);
            p.drawText(row.x + row.w - 6.0 - cz.w, row.y + ((kBRow - 14.0) / 2.0), kBCheck, cs);
        }

        hits_.push_back({.r = row, .kind = Hit::Kind::Device, .dev = d});
    }

    // ── Pairing prompt ────────────────────────────────────────────────────
    // BlueZ is holding a method call open behind every one of these; the card
    // is the only way the user can answer it.

    static double promptHeight(const BtPairRequest& r) {
        switch (r.kind) {
            case BtPairRequest::Kind::None:
                return 0.0;
            case BtPairRequest::Kind::Confirm:
            case BtPairRequest::Kind::Display:
                return 96.0;  // label + the code + buttons
            case BtPairRequest::Kind::Entry:
                return 84.0;  // label + entry box + buttons
            case BtPairRequest::Kind::Authorize:
            case BtPairRequest::Kind::Service:
                return 68.0;  // label + buttons
        }
        return 0.0;
    }

    void acceptPrompt() {
        if (backend_ == nullptr) { return; }
        if (snap().pairing.kind == BtPairRequest::Kind::Entry) {
            if (entry_.empty()) { return; }  // nothing to send yet
            backend_->respondPairingInput(entry_);
        } else {
            backend_->respondPairing(true);
        }
        entry_.clear();
    }

    // Headline and accept-label per kind. Display has no accept path — the user
    // types the code on the other device, so only Cancel is offered.
    [[nodiscard]] static std::string promptTitle(const BtPairRequest& r) {
        const std::string& who = r.deviceName;
        switch (r.kind) {
            case BtPairRequest::Kind::Confirm:
                return "Does this code match " + who + "?";
            case BtPairRequest::Kind::Authorize:
                return "Pair with " + who + "?";
            case BtPairRequest::Kind::Display:
                return "Enter this code on " + who;
            case BtPairRequest::Kind::Entry:
                return "Enter the code shown on " + who;
            case BtPairRequest::Kind::Service:
                return "Allow " + who + " to connect?";
            case BtPairRequest::Kind::None:
                break;
        }
        return {};
    }

    void drawPrompt(Painter& p, const Rect& b, double y, int64_t now) {
        const BtPairRequest& r = snap().pairing;
        if (!r.active()) {
            entry_.clear();
            return;
        }

        const Rect card{
            .x = b.x + kBPad, .y = y, .w = b.w - (kBPad * 2), .h = promptHeight(r) - 6.0};
        p.fillRoundedRect(card, 8.0, theme().colors.glassHover);

        TextStyle const lbl{.family = theme().font.family,
                            .size = 11.5,
                            .weight = PANGO_WEIGHT_MEDIUM,
                            .color = theme().colors.text};
        p.drawText(card.x + 10.0, card.y + 8.0, promptTitle(r), lbl, HAlign::Left, card.w - 20.0);

        double inner = card.y + 28.0;
        if (r.kind == BtPairRequest::Kind::Confirm || r.kind == BtPairRequest::Kind::Display) {
            // The code, spaced out so it can be read off the screen at a glance.
            std::string spaced;
            for (size_t i = 0; i < r.passkey.size(); ++i) {
                if (i == 3 && r.passkey.size() == 6) { spaced += ' '; }
                spaced += r.passkey[i];
            }
            TextStyle const code{.family = theme().font.family,
                                 .size = 22.0,
                                 .weight = PANGO_WEIGHT_BOLD,
                                 .color = theme().colors.primary};
            const Size cz = p.measureText(spaced, code);
            p.drawText(card.x + ((card.w - cz.w) / 2.0), inner, spaced, code);
            if (r.kind == BtPairRequest::Kind::Display && r.entered > 0) {
                TextStyle const prog{.family = theme().font.family,
                                     .size = 10.0,
                                     .weight = PANGO_WEIGHT_NORMAL,
                                     .color = theme().colors.textSubtle};
                p.drawText(card.x + 10.0, inner + 26.0, std::to_string(r.entered) + " entered",
                           prog);
            }
            inner += 34.0;
        } else if (r.kind == BtPairRequest::Kind::Entry) {
            const Rect box{.x = card.x + 10.0, .y = inner, .w = card.w - 20.0, .h = 22.0};
            p.fillRoundedRect(box, 6.0, theme().colors.background);
            const bool blink = (now % 1000) < 500;
            TextStyle const txt{.family = theme().font.family,
                                .size = 13.0,
                                .weight = PANGO_WEIGHT_NORMAL,
                                .color = theme().colors.text};
            // Shown in the clear: the user is copying it off the other device,
            // so masking it would only make it harder to check.
            p.drawText(box.x + 8.0, box.y + 3.0, blink ? entry_ + "│" : entry_, txt);
            inner += 28.0;
        }

        // Buttons. Display offers no accept — there is nothing to agree to.
        const bool canAccept = r.kind != BtPairRequest::Kind::Display;
        const bool acceptReady = r.kind != BtPairRequest::Kind::Entry || !entry_.empty();
        const char* acceptLabel = r.kind == BtPairRequest::Kind::Service ? "Allow" : "Pair";
        const char* cancelLabel = r.kind == BtPairRequest::Kind::Service ? "Deny" : "Cancel";

        TextStyle const bs{.family = theme().font.family,
                           .size = 11.5,
                           .weight = PANGO_WEIGHT_MEDIUM,
                           .color = theme().colors.background};
        TextStyle const xs{.family = theme().font.family,
                           .size = 11.5,
                           .weight = PANGO_WEIGHT_MEDIUM,
                           .color = theme().colors.textSubtle};
        const Rect cancel{.x = card.x + card.w - 60.0, .y = inner, .w = 50.0, .h = kBBtnH};
        if (canAccept) {
            const Rect accept{.x = card.x + card.w - 118.0, .y = inner, .w = 52.0, .h = kBBtnH};
            p.fillRoundedRect(accept, kBBtnH / 2.0,
                              acceptReady ? theme().colors.primary
                                          : theme().colors.primary.withAlpha(0.4));
            const Size az = p.measureText(acceptLabel, bs);
            p.drawText(accept.x + ((accept.w - az.w) / 2.0), accept.y + 2.0, acceptLabel, bs);
            hits_.push_back({.r = accept, .kind = Hit::Kind::PromptAccept});
        }
        const Size xz = p.measureText(cancelLabel, xs);
        p.drawText(cancel.x + ((cancel.w - xz.w) / 2.0), cancel.y + 2.0, cancelLabel, xs);
        hits_.push_back({.r = cancel, .kind = Hit::Kind::PromptCancel});
    }

    // Pill switch (on = accent fill, knob right) — same control as the WiFi
    // picker's radio toggle.
    void drawSwitch(Painter& p, const Rect& r, bool on) {
        p.fillRoundedRect(r, r.h / 2.0, on ? theme().colors.primary : theme().colors.glassHover);
        const double knobR = (r.h - 4.0) / 2.0;
        const double kx = on ? r.x + r.w - knobR - 2.0 : r.x + knobR + 2.0;
        p.fillCircle(kx, r.y + r.h / 2.0, knobR,
                     on ? theme().colors.background : theme().colors.textSubtle);
    }

    BluetoothBackend* backend_ = nullptr;
    std::vector<Hit> hits_;
    size_t scroll_ = 0;
    double hoverX_ = -1, hoverY_ = -1;
    std::string entry_;  // typed passkey/PIN while an Entry prompt is up
    static constexpr double kSwitchW = 34.0;
    static constexpr double kSwitchH = 18.0;
};
}  // namespace

BluetoothIndicator::BluetoothIndicator(const SystemBackends& backends)
    : StatusIndicator("bluetooth", Zone::Right, 350),
      backend_(backends.bluetooth) {
    // Hidden until this indicator's own backend publishes a snapshot — no
    // placeholder glyph standing in for data we do not have. StateCache
    // normally seeds that snapshot before the first frame, so this is only
    // visibly empty on a first-ever run or when the daemon never answers.
    // Without a backend (tests, registry previews) render sample defaults.
    loaded_ = (backend_ == nullptr);
    visible = loaded_;
}

std::string BluetoothIndicator::icon() const {
    if (!loaded_) {
        return "󰂯";  // no data yet: neutral glyph for the QS tile (the bar hides)
    }
    if (!lastSnap_.powered) { return "󰂲"; }
    return lastSnap_.connectedCount > 0 ? "󰂱" : "󰂯";
}

std::string BluetoothIndicator::themedIcon() const {
    if (!loaded_) {
        return "";  // no data yet: fall back to the neutral glyph
    }
    if (!lastSnap_.powered) { return "bluetooth-disabled-symbolic"; }
    return lastSnap_.connectedCount > 0 ? "bluetooth-active-symbolic" : "bluetooth-paired-symbolic";
}

std::string BluetoothIndicator::tooltip() const {
    if (!lastSnap_.powered) { return "Bluetooth off"; }
    if (lastSnap_.connectedCount == 0) { return "Bluetooth on"; }
    if (lastSnap_.connectedCount == 1) { return "Connected: " + lastSnap_.firstDevice; }
    return std::to_string(lastSnap_.connectedCount) + " devices connected";
}

Color BluetoothIndicator::iconColor() const {
    // Blue accent while something is connected.
    if (lastSnap_.powered && lastSnap_.connectedCount > 0) { return theme().colors.primary; }
    return theme().colors.text;
}

void BluetoothIndicator::onBackendUpdate() {
    if (backend_ == nullptr) { return; }
    lastSnap_ = backend_->snapshot();
    // Only *this* backend's readiness reveals the indicator — a push from an
    // unrelated backend must not mark us loaded with a still-empty snapshot.
    loaded_ = backend_->ready();
    visible = loaded_ && lastSnap_.available;
}

std::unique_ptr<QSTile> BluetoothIndicator::createTile() {
    auto* snap = &lastSnap_;
    auto* backend = backend_;
    return std::make_unique<QSToggleTile>(
        "Bluetooth", "󰂯", [snap]() { return snap->powered; },
        [backend, snap]() {
            if (backend) { backend->setPowered(!snap->powered); }
        },
        [snap]() -> std::string {
            if (!snap->powered) { return "Off"; }
            if (snap->connectedCount == 0) { return "No devices"; }
            if (snap->connectedCount == 1) { return snap->firstDevice; }
            return std::to_string(snap->connectedCount) + " devices";
        },
        Color{0, 0, 0, 0}, QSTile::Role::Bluetooth);
}

std::unique_ptr<DetailedPopover> BluetoothIndicator::createDetailedView() {
    if (backend_ == nullptr) { return nullptr; }
    return std::make_unique<BluetoothPopover>(backend_);
}

REGISTER_INDICATOR("bluetooth", Zone::Right, 350, BluetoothIndicator)

}  // namespace qypr
