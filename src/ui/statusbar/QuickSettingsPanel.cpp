// QuickSettingsPanel.cpp - Shared Quick Settings panel widget implementation
#include "ui/statusbar/QuickSettingsPanel.hpp"
#include "render/Painter.hpp"
#include "ui/Theme.hpp"
#include "ui/statusbar/StatusIndicator.hpp"
#include "core/Config.hpp"
#include "core/EventLoop.hpp"
#include "core/Process.hpp"
#include "system/WifiBackend.hpp"
#include "system/BluetoothBackend.hpp"
#include "system/BrightnessBackend.hpp"
#include "system/VolumeBackend.hpp"
#include "system/DndState.hpp"
#include "mpris/MprisController.hpp"
#include "power/PowerManager.hpp"
#include <algorithm>
#include <cstdlib>
#include <unistd.h>
#include <vector>

#include "system/NightLightBackend.hpp"
#include "system/IdleInhibitor.hpp"

namespace qypr {

namespace {
// Panel geometry tracks the theme so QS stays visually consistent with the bar
// strip and the other popovers, and honours any [theme] override. These are
// *references* to the theme's inline variables, never cached copies: copying
// into a file-scope `const double` produces a dynamically-initialised global
// whose init order relative to the theme variables (defined in another TU) is
// unspecified — it can be read as 0 (static-init-order fiasco) and would also
// miss any value loadTheme() applies at startup. Binding a reference to a
// static-storage object is constant-initialised, so it is always valid and
// always reflects the live theme value.
const double& kPanelW = theme::statusbar::qsPanelWidth;
const double& kPad = theme::statusbar::qsPadding;
const double& kGap = theme::statusbar::qsTileGap;

// Header
constexpr double kHeaderH = 52.0;

// Toggle grid: 3 equal columns
constexpr int kGridCols = 3;
const double& kGridRowH = theme::statusbar::qsTileHeight;

// Volume section
constexpr double kVolumeH = 52.0;

// Media card
constexpr double kMediaH = 68.0;

// Config section for QS commands
constexpr const char* kQsSection = "quick-settings";

// Run a shell command detached (fire-and-forget), reaped through the event loop.
//
// Deliberately NOT std::system(): the child must not inherit this process's
// signal dispositions (see core/Process.hpp — an inherited SIGCHLD = SIG_IGN
// makes a helper's own waitpid() fail with ECHILD, which is what broke grimblast
// when the bar ignored SIGCHLD), and the bar must not block on the command.
// spawnReaped() handles both, and gives the child its own session so a command
// that outlives the panel is not tied to it.
void runCommand(EventLoop& loop, const std::string& cmd) {
    if (cmd.empty()) { return; }
    spawnReaped(loop, "/bin/sh", {"/bin/sh", "-c", cmd}, /*newSession=*/true);
}

std::string getUserName() {
    const char* u = std::getenv("USER");
    return (u != nullptr) ? std::string(u) : "user";
}

std::string getHostName() {
    char buf[256]{};
    if (gethostname(buf, sizeof(buf)) == 0) {
        buf[sizeof(buf) - 1] = '\0';
        return std::string(buf);
    }
    return "localhost";
}
}  // namespace

// ─── Tile building ────────────────────────────────────────────────────────

void QuickSettingsPanel::buildTiles(EventLoop& loop, const SystemBackends& backends,
                                    const std::function<void()>& onOpenWifi) {
    // Header
    std::string const user = getUserName();
    std::string const host = getHostName();
    header_ = std::make_unique<QSHeaderTile>(user, user + "@" + host, theme::color::primary);

    // Power button (right of header)
    std::string powerCmd = "waylaunch --power";
    if (backends.config != nullptr) {
        powerCmd = backends.config->getString(kQsSection, "power-command", powerCmd);
    }
    power_ = std::make_unique<QSPowerTile>([l = &loop, powerCmd]() { runCommand(*l, powerCmd); });

    // Wi-Fi combo (if backend present). The body opens the network picker via
    // `onOpenWifi`; the right power strip toggles the radio. Live updates come
    // through updateWifi() — the panel deliberately does not register its own
    // backend callback (StatusBar owns the single onChange fan-out).
    if (backends.wifi != nullptr) {
        const auto& ws = backends.wifi->snapshot();
        onOpenWifi_ = onOpenWifi;
        wifiCombo_ = std::make_unique<QSWifiComboTile>(
            ws.ssid, ws.strength, ws.enabled, ws.connected, theme::color::primary,
            [snap = backends.wifi]() { snap->setEnabled(!snap->snapshot().enabled); }, onOpenWifi);
        if (wifiCombo_) { wifiCombo_->setScanning(ws.scanning); }
    } else if (!wifiCombo_) {
        onOpenWifi_ = onOpenWifi;
        wifiCombo_ = std::make_unique<QSWifiComboTile>("", 0, false, false, theme::color::primary);
    }

    // Remove any indicator-created WiFi/Network tiles — the combo tile replaces them.
    std::erase_if(tiles_, [](const std::unique_ptr<QSTile>& t) {
        if (t == nullptr) { return false; }
        std::string const title = t->title();
        return title == "WiFi" || title == "Network" || title == "Wired";
    });

    // Purge any volume tiles from grid (volume is handled in dedicated QSVolumeTile card at bottom)
    std::erase_if(tiles_, [](const std::unique_ptr<QSTile>& t) {
        return t && (t->type() == QSTile::Type::Volume || t->title() == "Volume");
    });

    bool hasBt = false;
    bool hasBr = false;
    bool hasDnd = false;
    bool hasKa = false;
    bool hasSs = false;
    for (const auto& t : tiles_) {
        if (!t) { continue; }
        std::string const title = t->title();
        if (title == "Bluetooth") {
            hasBt = true;
        } else if (t->type() == QSTile::Type::Slider || title == "Q27G41ZDF" ||
                   title == "Brightness") {
            hasBr = true;
        } else if (title == "Do Not Disturb") {
            hasDnd = true;
        } else if (title == "Keep awake" || title == "Idle Inhibitor") {
            hasKa = true;
        } else if (title == "Screenshot") {
            hasSs = true;
        }
    }

    if (!hasBt) {
        if (backends.bluetooth != nullptr) {
            auto* bt = backends.bluetooth;
            tiles_.push_back(std::make_unique<QSToggleTile>(
                "Bluetooth", "󰂯",
                std::function<bool()>([bt]() { return bt->snapshot().powered; }),
                std::function<void()>([bt]() { bt->setPowered(!bt->snapshot().powered); }),
                std::function<std::string()>([bt]() -> std::string {
                    if (!bt->snapshot().powered) { return "Off"; }
                    if (bt->snapshot().connectedCount == 0) { return "Not Connected"; }
                    return bt->snapshot().firstDevice;
                })));
        } else {
            tiles_.push_back(std::make_unique<QSToggleTile>(
                "Bluetooth", "󰂯", std::function<bool()>([]() { return false; }),
                std::function<void()>([]() {}),
                std::function<std::string()>([]() -> std::string { return "Not Connected"; })));
        }
    }

    if (!hasBr) {
        if (backends.brightness != nullptr) {
            auto* br = backends.brightness;
            tiles_.push_back(std::make_unique<QSSliderTile>(
                "󰃟", std::function<double()>([br]() { return br->snapshot().fraction(); }),
                std::function<void(double)>([br](double v) { br->setFraction(v); }), nullptr,
                nullptr, nullptr, "Q27G41ZDF"));
        } else {
            tiles_.push_back(std::make_unique<QSSliderTile>(
                "󰃟", std::function<double()>([]() { return 0.8; }),
                std::function<void(double)>([](double) {}), nullptr, nullptr, nullptr,
                "Q27G41ZDF"));
        }
    }

    if (!hasDnd) {
        if (backends.dnd != nullptr) {
            auto* dnd = backends.dnd;
            tiles_.push_back(std::make_unique<QSToggleTile>(
                "Do Not Disturb", "󰂜", std::function<bool()>([dnd]() { return dnd->enabled(); }),
                std::function<void()>([dnd]() { dnd->toggle(); }),
                std::function<std::string()>(
                    [dnd]() -> std::string { return dnd->enabled() ? "On" : "Off"; })));
        } else {
            tiles_.push_back(std::make_unique<QSToggleTile>(
                "Do Not Disturb", "󰂜", []() { return false; }, []() {},
                []() -> std::string { return "Off"; }));
        }
    }

    // Night Light: always present. Remove any indicator-created tile and
    // re-create from the backend (or a static fallback) so the wiring is
    // consistent regardless of how the indicator was set up.
    {
        std::erase_if(tiles_, [](const std::unique_ptr<QSTile>& t) {
            return t && t->title() == "Night Light";
        });

        if (backends.nightLight != nullptr) {
            auto* nl = backends.nightLight;
            auto tile = std::make_unique<QSToggleTile>(
                "Night Light", "", std::function<bool()>([nl]() { return nl && nl->enabled(); }),
                std::function<void()>([nl]() {
                    if (nl && nl->available()) { nl->toggle(); }
                }),
                std::function<std::string()>([nl]() -> std::string {
                    if (!nl || !nl->available()) { return "Off"; }
                    return nl->enabled() ? "On" : "Off";
                }));
            tile->setOnScroll([nl](double dx, double dy) {
                if (!nl || !nl->available()) { return false; }
                const double delta = dy != 0.0 ? dy : dx;
                if (delta == 0.0) { return false; }
                nl->setSliderValue(nl->sliderValue() + (delta < 0.0 ? 0.05 : -0.05));
                return true;
            });
            tiles_.push_back(std::move(tile));
        } else {
            tiles_.push_back(std::make_unique<QSToggleTile>(
                "Night Light", "", []() { return false; }, []() {},
                []() -> std::string { return "Off"; }));
        }
    }

    if (!hasKa) {
        if (backends.idleInhibitor != nullptr) {
            auto* ii = backends.idleInhibitor;
            tiles_.push_back(std::make_unique<QSToggleTile>(
                "Keep awake", "󰅶", std::function<bool()>([ii]() { return ii && ii->active(); }),
                std::function<void()>([ii]() {
                    if (ii && ii->available()) { ii->toggle(); }
                }),
                std::function<std::string()>([ii]() -> std::string {
                    if (!ii || !ii->available()) { return "Off"; }
                    return ii->active() ? "On" : "Off";
                })));
        } else {
            tiles_.push_back(std::make_unique<QSToggleTile>(
                "Keep awake", "󰅶", []() { return false; }, []() {},
                []() -> std::string { return "Off"; }));
        }
    }

    if (!hasSs) {
        // qypr is a wlroots/Wayland bar, so the default is grim-based interactive
        // capture: grimblast lets the user drag-select a region, then copies it to
        // the clipboard AND saves a file, with a notification. flameshot is only a
        // fallback for machines without grimblast — it is broken on Hyprland at
        // v14 (flameshot-org/flameshot#4666), so it must not be preferred.
        // Chosen by availability, NOT exit code, so cancelling the region select
        // (grimblast returns non-zero) does not spuriously launch flameshot.
        // The command inherits the bar's environment (correct WAYLAND_DISPLAY —
        // never hardcode a display). Override via [quick-settings] screenshot-command.
        std::string ssCmd =
            "if command -v grimblast >/dev/null 2>&1; then grimblast --notify copysave area; "
            "else flameshot gui; fi";
        if (backends.config != nullptr) {
            ssCmd = backends.config->getString(kQsSection, "screenshot-command", ssCmd);
        }
        tiles_.push_back(std::make_unique<QSToggleTile>(
            "Screenshot", "󰄄", []() { return false; },
            [l = &loop, ssCmd]() { runCommand(*l, ssCmd); },
            []() -> std::string { return "Screenshot"; }));
    }

    // Volume section
    if (backends.volume != nullptr) {
        auto* vsnap = backends.volume;
        volume_ = std::make_unique<QSVolumeTile>([vsnap]() { return vsnap->snapshot().level; },
                                                 [vsnap](double v) { vsnap->setLevel(v); },
                                                 [vsnap]() -> std::string {
                                                     if (vsnap->snapshot().muted) { return "󰝟"; }
                                                     return "󰕾";
                                                 },
                                                 [vsnap]() { vsnap->toggleMute(); },
                                                 [vsnap]() { return vsnap->snapshot().muted; });
    } else {
        volume_ = std::make_unique<QSVolumeTile>([]() { return 0.75; }, [](double) {},
                                                 []() -> std::string { return "󰕾"; }, []() {},
                                                 []() { return false; });
    }

    // Media card
    media_ = std::make_unique<QSMediaTile>(backends.mpris);
}

void QuickSettingsPanel::updateWifi(const WifiSnapshot& s) {
    if (!wifiCombo_) { return; }
    wifiCombo_->setEnabled(s.enabled);
    wifiCombo_->setConnected(s.connected);
    wifiCombo_->setSsid(s.ssid);
    wifiCombo_->setStrength(s.strength);
    wifiCombo_->setScanning(s.scanning);
}

void QuickSettingsPanel::addTile(std::unique_ptr<QSTile> tile) {
    tiles_.push_back(std::move(tile));
}

// ─── Geometry ─────────────────────────────────────────────────────────────

double QuickSettingsPanel::contentWidth() const {
    return kPanelW;
}

double QuickSettingsPanel::contentHeight() const {
    double h = kPad;  // top padding

    // Header row
    h += kHeaderH;

    // Grid row 1 & row 2
    int totalGrid = (wifiCombo_ ? 1 : 0);
    for (const auto& t : tiles_) {
        if (t && (t->type() == QSTile::Type::Toggle || t->type() == QSTile::Type::WifiCombo)) {
            totalGrid++;
        }
    }
    int const rows = (totalGrid + kGridCols - 1) / kGridCols;
    if (rows > 0) {
        h += kGap;
        h += (rows * kGridRowH) + ((rows - 1) * kGap);
    }

    // Slider tiles (e.g. Brightness)
    for (const auto& t : tiles_) {
        if (t && t->type() == QSTile::Type::Slider) { h += kGap + 48.0; }
    }

    // Volume section
    if (volume_) { h += kGap + kVolumeH; }

    // Info tiles (e.g. Battery)
    for (const auto& t : tiles_) {
        if (t && t->type() == QSTile::Type::Info) { h += kGap + 50.0; }
    }

    // Media card
    if (media_) { h += kGap + kMediaH; }

    h += kPad;  // bottom padding
    return h;
}

// ─── Layout ───────────────────────────────────────────────────────────────

void QuickSettingsPanel::layoutTiles() {
    Rect const popBounds = getBounds();
    double const contentW = popBounds.w - (2 * kPad);
    double const colW = (contentW - ((kGridCols - 1) * kGap)) / kGridCols;
    double y = popBounds.y + kPad;

    // 1. Header (left side) & Power button (right side)
    double const powerW = 52.0;
    double const headerW = contentW - powerW - kGap;
    if (header_) {
        header_->bounds = {.x = popBounds.x + kPad, .y = y, .w = headerW, .h = kHeaderH};
    }

    if (power_) {
        double const px = popBounds.x + popBounds.w - kPad - powerW;
        powerBounds_ = {.x = px, .y = y, .w = powerW, .h = kHeaderH};
        power_->bounds = powerBounds_;
    }

    y += kHeaderH + kGap;

    // 2. 3-Column Toggle Grid
    std::vector<QSTile*> gridTiles;
    if (wifiCombo_) { gridTiles.push_back(wifiCombo_.get()); }
    for (auto& t : tiles_) {
        if (t && (t->type() == QSTile::Type::Toggle || t->type() == QSTile::Type::WifiCombo)) {
            gridTiles.push_back(t.get());
        }
    }

    int col = 0;
    double rowY = y;
    for (auto tile : gridTiles) {
        if (col >= kGridCols) {
            col = 0;
            rowY += kGridRowH + kGap;
        }
        double const tx = popBounds.x + kPad + (col * (colW + kGap));
        tile->bounds = {.x = tx, .y = rowY, .w = colW, .h = kGridRowH};
        col++;
    }

    if (!gridTiles.empty()) { y = rowY + kGridRowH; }

    // 3. Slider section (Brightness)
    for (auto& t : tiles_) {
        if (t && t->type() == QSTile::Type::Slider) {
            y += kGap;
            double const sliderH = 48.0;
            t->bounds = {.x = popBounds.x + kPad, .y = y, .w = contentW, .h = sliderH};
            y += sliderH;
        }
    }

    // 4. Volume section
    if (volume_) {
        y += kGap;
        volume_->bounds = {.x = popBounds.x + kPad, .y = y, .w = contentW, .h = kVolumeH};
        y += kVolumeH;
    }

    // 5. Info section (Battery)
    for (auto& t : tiles_) {
        if (t && t->type() == QSTile::Type::Info) {
            y += kGap;
            double const infoH = 50.0;
            t->bounds = {.x = popBounds.x + kPad, .y = y, .w = contentW, .h = infoH};
            y += infoH;
        }
    }

    // 6. Media card
    if (media_) {
        y += kGap;
        media_->bounds = {.x = popBounds.x + kPad, .y = y, .w = contentW, .h = kMediaH};
    }
}

Rect QuickSettingsPanel::findTileBounds(const std::string& tileTitle) const {
    if (wifiCombo_ &&
        (wifiCombo_->title() == tileTitle || tileTitle == "Wi-Fi" || tileTitle == "WiFi")) {
        return wifiCombo_->bounds;
    }
    for (const auto& t : tiles_) {
        if (t && (t->title() == tileTitle || (tileTitle == "Do Not Disturb" &&
                                              t->title().find("Disturb") != std::string::npos))) {
            return t->bounds;
        }
    }
    if (volume_ && (volume_->title() == tileTitle || tileTitle == "Volume")) {
        return volume_->bounds;
    }
    if (media_ && (media_->title() == tileTitle || tileTitle == "Media")) { return media_->bounds; }
    if (header_ && header_->title() == tileTitle) { return header_->bounds; }
    if (power_ && power_->title() == tileTitle) { return power_->bounds; }
    return {.x = 0, .y = 0, .w = 0, .h = 0};
}

// ─── Draw ─────────────────────────────────────────────────────────────────

void QuickSettingsPanel::draw(Painter& p, int64_t now) {
    layoutTiles();
    Rect const popBounds = getBounds();

    // Match the bar's shared backdrop. If this panel is hosted by the lock
    // screen, draw its original opaque surface instead.
    if (!drawSharedBackdrop(p, popBounds, theme::statusbar::qsCornerRadius)) {
        p.fillRoundedRect(popBounds, theme::statusbar::qsCornerRadius, theme::color::surface);
    }

    // Close button (×) at top right — a subtle circular button.
    closeBounds_ = {
        .x = popBounds.x + popBounds.w - kPad - 24.0, .y = popBounds.y + 8.0, .w = 24.0, .h = 24.0};
    p.fillCircleSource(closeBounds_.x + 12.0, closeBounds_.y + 12.0, 12.0,
                       theme::statusbar::panelSurfaceHover());
    TextStyle const closeStyle{.family = theme::font::iconFamily,
                               .size = 11.0,
                               .weight = PANGO_WEIGHT_NORMAL,
                               .color = theme::color::textSubtle};
    Size const closeSz = p.measureText("󰅖", closeStyle);
    p.drawText(closeBounds_.x + 12.0 - (closeSz.w / 2.0), closeBounds_.y + 12.0 - (closeSz.h / 2.0),
               "󰅖", closeStyle);

    auto drawOrSkip = [&](auto& tile) {
        if (!tile) { return; }
        tile->hoverAnim_.animateTo(tile->hovered ? 1.0 : 0.0, theme::anim::fast, ease::inOutQuad);
        tile->draw(p, now);
    };

    drawOrSkip(header_);
    drawOrSkip(power_);
    drawOrSkip(wifiCombo_);
    for (auto& t : tiles_) { drawOrSkip(t); }
    drawOrSkip(volume_);
    drawOrSkip(media_);
}

// ─── Input ────────────────────────────────────────────────────────────────

bool QuickSettingsPanel::handleMotion(double x, double y) {
    curX_ = x;
    curY_ = y;
    return getBounds().contains(x, y);
}

bool QuickSettingsPanel::consumeCloseRequest() {
    if (closeRequested_) {
        closeRequested_ = false;
        return true;
    }
    return false;
}

bool QuickSettingsPanel::handleClick(double x, double y) {
    activeDragTile_ = nullptr;
    curX_ = x;
    curY_ = y;

    if (closeBounds_.contains(x, y)) {
        closeRequested_ = true;
        return true;
    }

    if (power_ && power_->bounds.contains(x, y)) {
        power_->onClick(x, y);
        return true;
    }

    if (volume_ && volume_->bounds.contains(x, y)) {
        volume_->onClick(x, y);
        activeDragTile_ = volume_.get();
        return true;
    }

    for (auto& t : tiles_) {
        if (t->bounds.contains(x, y)) {
            t->onClick(x, y);
            if (t->type() == QSTile::Type::Slider) { activeDragTile_ = t.get(); }
            return true;
        }
    }

    if (media_ && media_->bounds.contains(x, y)) {
        media_->onClick(x, y);
        return true;
    }

    if (wifiCombo_ && wifiCombo_->bounds.contains(x, y)) {
        wifiCombo_->onClick(x, y);
        return true;
    }

    return false;
}

bool QuickSettingsPanel::handleSecondaryClick(double x, double y) {
    curX_ = x;
    curY_ = y;

    // Right-click is the "more" action: the Wi-Fi tile opens the network
    // picker; every other tile opens its source indicator's detail popover
    // (attached by StatusBar at createTile time) or does nothing.
    if (wifiCombo_ && wifiCombo_->bounds.contains(x, y)) {
        if (onOpenWifi_) { onOpenWifi_(); }
        return true;
    }

    for (auto& t : tiles_) {
        if (t->bounds.contains(x, y)) {
            t->onSecondaryClick(x, y);
            return true;
        }
    }

    return true;  // swallow right-clicks inside the panel (never dismiss)
}

bool QuickSettingsPanel::handleDrag(double x, double y) {
    curX_ = x;
    curY_ = y;
    if (activeDragTile_ != nullptr) {
        activeDragTile_->onDrag(x, y);
        return true;
    }
    return false;
}

bool QuickSettingsPanel::handleScroll(double dx, double dy) {
    if (volume_ && volume_->bounds.contains(curX_, curY_) && volume_->onScroll(dx, dy)) {
        return true;
    }
    for (auto& t : tiles_) {
        if (t->bounds.contains(curX_, curY_) && t->onScroll(dx, dy)) { return true; }
    }
    return false;
}

bool QuickSettingsPanel::handleKey(uint32_t keysym) {
    QSTile* target = nullptr;
    if ((activeDragTile_ != nullptr) && (activeDragTile_->type() == QSTile::Type::Slider ||
                                         activeDragTile_->type() == QSTile::Type::Volume)) {
        target = activeDragTile_;
    } else {
        for (auto& t : tiles_) {
            if (t->type() == QSTile::Type::Slider && t->bounds.contains(curX_, curY_)) {
                target = t.get();
                break;
            }
        }
    }
    if ((target != nullptr) && target->handleKey(keysym)) { return true; }
    return false;
}

}  // namespace qypr
