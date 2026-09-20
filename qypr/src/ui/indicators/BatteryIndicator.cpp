// BatteryIndicator.cpp - Status bar battery indicator implementation.
#include "ui/indicators/BatteryIndicator.hpp"

#include <algorithm>
#include <array>
#include <cstdio>
#include <vector>

#include "render/Painter.hpp"
#include "system/PowerProfilesBackend.hpp"
#include "ui/Theme.hpp"
#include "ui/statusbar/DetailedPopover.hpp"
#include "ui/statusbar/IndicatorRegistry.hpp"
#include "ui/statusbar/QSTile.hpp"

namespace qypr {

namespace {

// Short pill label for a power-profiles-daemon profile name.
std::string profileLabel(const std::string& name) {
    if (name == "power-saver") { return "Saver"; }
    if (name == "balanced") { return "Balanced"; }
    if (name == "performance") { return "Perf"; }
    return name;
}
constexpr std::array<const char*, 11> kDischargingIcons = {
    "󰂎", "󰁺", "󰁻", "󰁼", "󰁽", "󰁾", "󰁿", "󰂀", "󰂁", "󰂂", "󰁹"};
constexpr std::array<const char*, 11> kChargingIcons = {
    "󰢟", "󰢜", "󰂆", "󰂇", "󰂈", "󰢝", "󰂉", "󰂊", "󰂋", "󰂅", "󰂅"};

const char* batteryIcon(int percentage, bool charging) {
    int idx = percentage / 10;
    idx = std::min(idx, 10);
    idx = std::max(idx, 0);
    return charging ? kChargingIcons.at(static_cast<size_t>(idx))
                    : kDischargingIcons.at(static_cast<size_t>(idx));
}

// freedesktop symbolic names, tiered by percentage with -charging variants.
const char* batteryThemedIcon(int percentage, bool charging, bool full) {
    if (full) { return "battery-full-charged-symbolic"; }
    if (charging) {
        if (percentage >= 90) { return "battery-full-charging-symbolic"; }
        if (percentage >= 60) { return "battery-good-charging-symbolic"; }
        if (percentage >= 40) { return "battery-low-charging-symbolic"; }
        if (percentage >= 20) { return "battery-caution-charging-symbolic"; }
        return "battery-empty-charging-symbolic";
    }
    if (percentage >= 90) { return "battery-full-symbolic"; }
    if (percentage >= 60) { return "battery-good-symbolic"; }
    if (percentage >= 40) { return "battery-low-symbolic"; }
    if (percentage >= 20) { return "battery-caution-symbolic"; }
    return "battery-empty-symbolic";
}

std::string stateString(BatterySnapshot::State state) {
    switch (state) {
        case BatterySnapshot::Charging:
            return "Charging";
        case BatterySnapshot::Discharging:
            return "Discharging";
        case BatterySnapshot::Full:
            return "Full";
        case BatterySnapshot::PendingCharge:
            return "Pending Charge";
        default:
            return "Unknown";
    }
}

std::string formatTime(int64_t seconds) {
    if (seconds <= 0) { return ""; }
    int64_t const h = seconds / 3600;
    int64_t const m = (seconds % 3600) / 60;
    if (h > 0) { return std::to_string(h) + "h " + std::to_string(m) + "min"; }
    return std::to_string(m) + "min";
}

// Detailed popover per docs/STATUS_BAR.md: percentage bar, time remaining,
// power draw, state. Reads the indicator's snapshot live (safe: PopoverManager
// is destroyed before the indicator vectors in StatusBar).
class BatteryPopover : public DetailedPopover {
public:
    // `profiles` is null on the lock screen (bar-only) — the profile selector is
    // then omitted and only the battery detail shows.
    BatteryPopover(const BatterySnapshot* snap, PowerProfilesBackend* profiles)
        : snap_(snap),
          profiles_(profiles) {}

    [[nodiscard]] bool hasProfiles() const {
        return (profiles_ != nullptr) && profiles_->snapshot().available;
    }
    [[nodiscard]] double contentHeight() const override { return hasProfiles() ? 224.0 : 148.0; }

    void draw(Painter& p, int64_t now) override {
        Rect b = getBounds();
        // Slide down 6px while opening (fade is applied by PopoverManager).
        b.y -= (1.0 - openProgress_.value(now)) * 6.0;

        if (!drawSharedBackdrop(p, b, theme::statusbar::popoverRadius)) {
            p.fillRoundedRectSource(b, theme::statusbar::popoverRadius,
                                    theme::statusbar::panelSurface());
        }
        drawProfiles(p, b);

        const double pad = theme::statusbar::popoverPadding;
        double const x = b.x + pad;
        double y = b.y + pad;
        const double innerW = b.w - (2 * pad);

        TextStyle const title{.family = theme::font::family,
                              .size = 14.0,
                              .weight = PANGO_WEIGHT_BOLD,
                              .color = theme::color::text};
        p.drawText(x, y, "Battery", title);
        p.drawText(b.x + b.w - pad, y, std::to_string(snap_->percentage) + "%", title,
                   HAlign::Right);
        y += 30.0;

        // Charge bar
        const double barH = 8.0;
        p.fillRoundedRectSource({.x = x, .y = y, .w = innerW, .h = barH}, barH / 2,
                                theme::statusbar::panelSurface());
        double const frac = snap_->percentage / 100.0;
        if (frac > 0.01) {
            Color fill;
            if (snap_->percentage > 50) {
                fill = theme::color::success;
            } else if (snap_->percentage > 20) {
                fill = theme::color::warning;
            } else {
                fill = theme::color::error;
            }
            p.fillRoundedRect({.x = x, .y = y, .w = innerW * frac, .h = barH}, barH / 2, fill);
        }
        y += barH + 16.0;

        TextStyle const line{.family = theme::font::family,
                             .size = 12.5,
                             .weight = PANGO_WEIGHT_NORMAL,
                             .color = theme::color::textSubtle};
        if (snap_->state == BatterySnapshot::Discharging && snap_->timeToEmpty > 0) {
            p.drawText(x, y, formatTime(snap_->timeToEmpty) + " remaining", line);
            y += 20.0;
        } else if (snap_->state == BatterySnapshot::Charging && snap_->timeToFull > 0) {
            p.drawText(x, y, formatTime(snap_->timeToFull) + " until full", line);
            y += 20.0;
        }
        if (snap_->energyRate > 0.05) {
            std::array<char, 32> buf{};
            std::snprintf(buf.data(), buf.size(), "%.1f W power draw", snap_->energyRate);
            p.drawText(x, y, buf.data(), line);
            y += 20.0;
        }
        p.drawText(x, y, "State: " + stateString(snap_->state), line);
    }

    bool handleClick(double x, double y) override {
        const auto it = std::ranges::find_if(
            profileButtons_, [&](const ProfileBtn& btn) { return btn.rect.contains(x, y); });
        if (it == profileButtons_.end()) { return false; }
        profiles_->setActiveProfile(it->name);
        return true;
    }

private:
    struct ProfileBtn {
        Rect rect;
        std::string name;
    };

    // A segmented control of the daemon's profiles, anchored to the popover's
    // bottom so it never collides with the (variable-height) battery detail.
    void drawProfiles(Painter& p, const Rect& b) {
        profileButtons_.clear();
        if (!hasProfiles()) { return; }
        const auto& snap = profiles_->snapshot();
        const double pad = theme::statusbar::popoverPadding;
        const double innerW = b.w - (2 * pad);
        const double x = b.x + pad;

        double hy = b.y + b.h - 62.0;
        p.fillRectSource({.x = x, .y = hy - 12.0, .w = innerW, .h = 1.0},
                         theme::statusbar::panelSurfaceHover());
        TextStyle const hdr{.family = theme::font::family,
                            .size = 11.0,
                            .weight = PANGO_WEIGHT_BOLD,
                            .color = theme::color::textSubtle};
        p.drawText(x, hy, "POWER PROFILE", hdr);
        hy += 20.0;

        const size_t n = snap.profiles.size();
        if (n == 0) { return; }
        const double gap = 8.0;
        const auto nf = static_cast<double>(n);
        const double pw = (innerW - ((nf - 1.0) * gap)) / nf;
        const double ph = 30.0;
        for (size_t i = 0; i < n; ++i) {
            const std::string& name = snap.profiles.at(i);
            const Rect r{.x = x + (static_cast<double>(i) * (pw + gap)), .y = hy, .w = pw, .h = ph};
            const bool active = name == snap.active;
            p.fillRoundedRect(r, 8.0, active ? theme::color::primary : theme::color::glass);
            p.strokeRoundedRect(r, 8.0, theme::color::glassBorder, 1.0);
            TextStyle const ts{.family = theme::font::family,
                               .size = 12.0,
                               .weight = active ? PANGO_WEIGHT_BOLD : PANGO_WEIGHT_NORMAL,
                               .color = active ? Color::fromHex("#1e1e2e") : theme::color::text};
            p.drawText(r.x + (r.w / 2.0), r.y + ((ph - 14.0) / 2.0), profileLabel(name), ts,
                       HAlign::Center);
            profileButtons_.push_back({.rect = r, .name = name});
        }
    }

    const BatterySnapshot* snap_;
    PowerProfilesBackend* profiles_ = nullptr;
    std::vector<ProfileBtn> profileButtons_;
};
}  // namespace

BatteryIndicator::BatteryIndicator(const SystemBackends& backends)
    : StatusIndicator("battery", Zone::Right, 500),
      backend_(backends.battery),
      profiles_(backends.powerProfiles) {
    // Hidden until this indicator's own backend publishes a snapshot — no
    // placeholder glyph standing in for data we do not have. StateCache
    // normally seeds that snapshot before the first frame, so this is only
    // visibly empty on a first-ever run or when the daemon never answers.
    // Without a backend (tests, registry previews) render sample defaults.
    loaded_ = (backend_ == nullptr);
    visible = loaded_;
}

std::string BatteryIndicator::icon() const {
    if (!loaded_) {
        return "󰂑";  // no data yet: neutral glyph for the QS tile (the bar hides)
    }
    bool const charging = lastSnap_.state == BatterySnapshot::Charging ||
                          lastSnap_.state == BatterySnapshot::PendingCharge;
    return batteryIcon(lastSnap_.percentage, charging);
}

std::string BatteryIndicator::themedIcon() const {
    if (!loaded_) {
        return "";  // no data yet: fall back to the neutral glyph
    }
    bool const charging = lastSnap_.state == BatterySnapshot::Charging ||
                          lastSnap_.state == BatterySnapshot::PendingCharge;
    bool const full = lastSnap_.state == BatterySnapshot::Full;
    return batteryThemedIcon(lastSnap_.percentage, charging, full);
}

std::string BatteryIndicator::label() const {
    if (!loaded_) {
        return "";  // no data yet: never invent a "0%"
    }
    return std::to_string(lastSnap_.percentage) + "%";
}

std::string BatteryIndicator::tooltip() const {
    std::string tip =
        "Battery " + std::to_string(lastSnap_.percentage) + "% — " + stateString(lastSnap_.state);
    return tip;
}

Color BatteryIndicator::iconColor() const {
    if (!loaded_) {
        return theme::color::text;  // no data yet: neutral, not a red "0%" alarm
    }
    int const pct = lastSnap_.percentage;
    if (pct > 50) { return theme::color::success; }
    if (pct > 20) { return theme::color::warning; }
    return theme::color::error;
}

void BatteryIndicator::draw(Painter& p, int64_t now) {
    StatusIndicator::draw(p, now);
}

void BatteryIndicator::onBackendUpdate() {
    if (backend_ == nullptr) { return; }
    lastSnap_ = backend_->snapshot();
    // Only *this* backend's readiness reveals the indicator — a push from an
    // unrelated backend must not mark us loaded with a still-empty snapshot.
    loaded_ = backend_->ready();
    visible = loaded_ && lastSnap_.present;
}

std::unique_ptr<QSTile> BatteryIndicator::createTile() {
    auto* snap = &lastSnap_;
    return std::make_unique<QSInfoTile>(
        "Battery", icon(), [snap]() { return snap->percentage / 100.0; },
        [snap]() {
            std::string info = stateString(snap->state);
            if (snap->state == BatterySnapshot::Discharging && snap->timeToEmpty > 0) {
                info += " — " + formatTime(snap->timeToEmpty) + " remaining";
            } else if (snap->state == BatterySnapshot::Charging && snap->timeToFull > 0) {
                info += " — " + formatTime(snap->timeToFull) + " to full";
            }
            if (snap->energyRate > 0) {
                info += "  •  " + std::to_string(static_cast<int>(snap->energyRate)) + "W";
            }
            return info;
        },
        // Re-read the glyph per frame. Tiles are built in the StatusBar
        // constructor, before any backend has reported, so the static `icon()`
        // above is captured while the indicator is still unloaded — it would
        // freeze the neutral "unknown" glyph into the panel permanently and
        // never follow the charge level afterwards.
        [this]() { return icon(); });
}

std::unique_ptr<DetailedPopover> BatteryIndicator::createDetailedView() {
    return std::make_unique<BatteryPopover>(&lastSnap_, profiles_);
}

REGISTER_INDICATOR("battery", Zone::Right, 500, BatteryIndicator)

}  // namespace qypr
