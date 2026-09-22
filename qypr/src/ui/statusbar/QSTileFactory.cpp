// QSTileFactory.cpp - Factory for Quick Settings tiles and lock-safe fallbacks.
#include "ui/statusbar/QSTileFactory.hpp"

#include <unistd.h>

#include <algorithm>
#include <cstdlib>
#include <string>

#include "core/Config.hpp"
#include "core/EventLoop.hpp"
#include "core/Process.hpp"
#include "mpris/MprisController.hpp"
#include "system/BluetoothBackend.hpp"
#include "system/BrightnessBackend.hpp"
#include "system/DndState.hpp"
#include "system/IdleInhibitor.hpp"
#include "system/NightLightBackend.hpp"
#include "system/VolumeBackend.hpp"
#include "system/WifiBackend.hpp"
#include "ui/statusbar/StatusIndicator.hpp"

namespace qypr {

namespace {

constexpr const char* kQsSection = "quick-settings";

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

BuiltQSTiles QSTileFactory::createTiles(EventLoop& loop, const SystemBackends& backends,
                                        const theme::State& theme,
                                        const std::vector<std::unique_ptr<QSTile>>& existingTiles,
                                        const std::function<void()>& onOpenWifi) {
    BuiltQSTiles res;

    // Header
    std::string const user = getUserName();
    std::string const host = getHostName();
    res.header = std::make_unique<QSHeaderTile>(user, user + "@" + host, theme.colors.primary);

    // Power button
    std::string powerCmd = "waylaunch --power";
    if (backends.config != nullptr) {
        powerCmd = backends.config->getString(kQsSection, "power-command", powerCmd);
    }
    res.power =
        std::make_unique<QSPowerTile>([l = &loop, powerCmd]() { runCommand(*l, powerCmd); });

    // Wi-Fi combo
    if (backends.wifi != nullptr) {
        const auto& ws = backends.wifi->snapshot();
        res.wifiCombo = std::make_unique<QSWifiComboTile>(
            ws.ssid, ws.strength, ws.enabled, ws.connected, theme.colors.primary,
            [snap = backends.wifi]() { snap->setEnabled(!snap->snapshot().enabled); }, onOpenWifi);
        if (res.wifiCombo) { res.wifiCombo->setScanning(ws.scanning); }
    } else {
        res.wifiCombo =
            std::make_unique<QSWifiComboTile>("", 0, false, false, theme.colors.primary);
    }

    bool hasBt = false;
    bool hasBr = false;
    bool hasDnd = false;
    bool hasKa = false;
    bool hasSs = false;

    for (const auto& t : existingTiles) {
        if (!t) { continue; }
        const auto role = t->role();
        if (role == QSTile::Role::Bluetooth) {
            hasBt = true;
        } else if (t->type() == QSTile::Type::Slider || role == QSTile::Role::Brightness) {
            hasBr = true;
        } else if (role == QSTile::Role::Dnd) {
            hasDnd = true;
        } else if (role == QSTile::Role::KeepAwake) {
            hasKa = true;
        } else if (role == QSTile::Role::Screenshot) {
            hasSs = true;
        }
    }

    if (!hasBt) {
        if (backends.bluetooth != nullptr) {
            auto* bt = backends.bluetooth;
            res.fallbackTiles.push_back(std::make_unique<QSToggleTile>(
                "Bluetooth", "󰂯",
                std::function<bool()>([bt]() { return bt->snapshot().powered; }),
                std::function<void()>([bt]() { bt->setPowered(!bt->snapshot().powered); }),
                std::function<std::string()>([bt]() -> std::string {
                    if (!bt->snapshot().powered) { return "Off"; }
                    if (bt->snapshot().connectedCount == 0) { return "Not Connected"; }
                    return bt->snapshot().firstDevice;
                }),
                Color{0, 0, 0, 0}, QSTile::Role::Bluetooth));
        } else {
            res.fallbackTiles.push_back(std::make_unique<QSToggleTile>(
                "Bluetooth", "󰂯", std::function<bool()>([]() { return false; }),
                std::function<void()>([]() {}),
                std::function<std::string()>([]() -> std::string { return "Not Connected"; }),
                Color{0, 0, 0, 0}, QSTile::Role::Bluetooth));
        }
    }

    if (!hasBr) {
        if (backends.brightness != nullptr) {
            auto* br = backends.brightness;
            res.fallbackTiles.push_back(std::make_unique<QSSliderTile>(
                "󰃟", std::function<double()>([br]() { return br->snapshot().fraction(); }),
                std::function<void(double)>([br](double v) { br->setFraction(v); }), nullptr,
                nullptr, nullptr, "Q27G41ZDF", QSTile::Role::Brightness));
        } else {
            res.fallbackTiles.push_back(std::make_unique<QSSliderTile>(
                "󰃟", std::function<double()>([]() { return 0.8; }),
                std::function<void(double)>([](double) {}), nullptr, nullptr, nullptr, "Q27G41ZDF",
                QSTile::Role::Brightness));
        }
    }

    if (!hasDnd) {
        if (backends.dnd != nullptr) {
            auto* dnd = backends.dnd;
            res.fallbackTiles.push_back(std::make_unique<QSToggleTile>(
                "Do Not Disturb", "󰂜", std::function<bool()>([dnd]() { return dnd->enabled(); }),
                std::function<void()>([dnd]() { dnd->toggle(); }),
                std::function<std::string()>(
                    [dnd]() -> std::string { return dnd->enabled() ? "On" : "Off"; }),
                Color{0, 0, 0, 0}, QSTile::Role::Dnd));
        } else {
            res.fallbackTiles.push_back(std::make_unique<QSToggleTile>(
                "Do Not Disturb", "󰂜", []() { return false; }, []() {},
                []() -> std::string { return "Off"; }, Color{0, 0, 0, 0}, QSTile::Role::Dnd));
        }
    }

    // Night Light
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
            }),
            Color{0, 0, 0, 0}, QSTile::Role::NightLight);
        tile->setOnScroll([nl](double dx, double dy) {
            if (!nl || !nl->available()) { return false; }
            const double delta = dy != 0.0 ? dy : dx;
            if (delta == 0.0) { return false; }
            nl->setSliderValue(nl->sliderValue() + (delta < 0.0 ? 0.05 : -0.05));
            return true;
        });
        res.fallbackTiles.push_back(std::move(tile));
    } else {
        res.fallbackTiles.push_back(std::make_unique<QSToggleTile>(
            "Night Light", "", []() { return false; }, []() {},
            []() -> std::string { return "Off"; }, Color{0, 0, 0, 0}, QSTile::Role::NightLight));
    }

    if (!hasKa) {
        if (backends.idleInhibitor != nullptr) {
            auto* ii = backends.idleInhibitor;
            res.fallbackTiles.push_back(std::make_unique<QSToggleTile>(
                "Keep awake", "󰅶", std::function<bool()>([ii]() { return ii && ii->active(); }),
                std::function<void()>([ii]() {
                    if (ii && ii->available()) { ii->toggle(); }
                }),
                std::function<std::string()>([ii]() -> std::string {
                    if (!ii || !ii->available()) { return "Off"; }
                    return ii->active() ? "On" : "Off";
                }),
                Color{0, 0, 0, 0}, QSTile::Role::KeepAwake));
        } else {
            res.fallbackTiles.push_back(std::make_unique<QSToggleTile>(
                "Keep awake", "󰅶", []() { return false; }, []() {},
                []() -> std::string { return "Off"; }, Color{0, 0, 0, 0}, QSTile::Role::KeepAwake));
        }
    }

    if (!hasSs) {
        std::string ssCmd =
            "if command -v grimblast >/dev/null 2>&1; then grimblast --notify copysave area; "
            "else flameshot gui; fi";
        if (backends.config != nullptr) {
            ssCmd = backends.config->getString(kQsSection, "screenshot-command", ssCmd);
        }
        res.fallbackTiles.push_back(std::make_unique<QSToggleTile>(
            "Screenshot", "󰄄", []() { return false; },
            [l = &loop, ssCmd]() { runCommand(*l, ssCmd); },
            []() -> std::string { return "Screenshot"; }, Color{0, 0, 0, 0},
            QSTile::Role::Screenshot));
    }

    // Volume section
    if (backends.volume != nullptr) {
        auto* vsnap = backends.volume;
        res.volume = std::make_unique<QSVolumeTile>([vsnap]() { return vsnap->snapshot().level; },
                                                    [vsnap](double v) { vsnap->setLevel(v); },
                                                    [vsnap]() -> std::string {
                                                        if (vsnap->snapshot().muted) {
                                                            return "󰝟";
                                                        }
                                                        return "󰕾";
                                                    },
                                                    [vsnap]() { vsnap->toggleMute(); },
                                                    [vsnap]() { return vsnap->snapshot().muted; });
    } else {
        res.volume = std::make_unique<QSVolumeTile>([]() { return 0.75; }, [](double) {},
                                                    []() -> std::string { return "󰕾"; }, []() {},
                                                    []() { return false; });
    }

    // Media card
    res.media = std::make_unique<QSMediaTile>(backends.mpris);

    return res;
}

}  // namespace qypr
