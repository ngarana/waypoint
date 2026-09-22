// QSTile.hpp - Quick Settings tile base and concrete UI implementations.
#pragma once

#include "core/Types.hpp"
#include "ui/Theme.hpp"
#include <cstdint>
#include <string>
#include <functional>

namespace qypr {

class Painter;
class MprisController;
class WifiBackend;

class QSTile : public theme::ThemeAware {
public:
    virtual ~QSTile() = default;

    // The panel dispatches input via virtuals on the base. Sub-classes
    // specialise by overriding the ones they care about.
    enum class Type { Toggle, Slider, Info, Header, Power, WifiCombo, Volume, Media };
    virtual Type type() const = 0;
    virtual std::string title() const { return ""; }

    // Stable identity for panel ownership (ARCHITECTURE_REVIEW finding 5):
    // which functional slot a tile fills, independent of its user-visible
    // title. The panel dedupes and replaces by role, so renaming a label
    // can never duplicate a tile or remove the wrong one. Custom tiles
    // (tests, one-off indicators) match no slot and are always kept.
    enum class Role {
        Custom,
        Bluetooth,
        Brightness,
        Volume,
        Dnd,
        NightLight,
        KeepAwake,
        Screenshot,
        Wifi
    };
    virtual Role role() const { return Role::Custom; }

    virtual void draw(Painter& p, int64_t now) = 0;

    virtual void onClick(double x, double y) {}
    // Right-click: the tile's context action. StatusBar attaches the source
    // indicator's detail popover at createTile time (setOnSecondary), so a
    // right-click on e.g. the Bluetooth tile opens its detail view; tiles
    // without an attached action ignore it.
    virtual void onSecondaryClick(double x, double y) {
        (void)x;
        (void)y;
        if (onSecondary_) { onSecondary_(); }
    }
    void setOnSecondary(std::function<void()> cb) { onSecondary_ = std::move(cb); }
    virtual void onDrag(double x, double y) {}
    virtual bool onScroll(double dx, double dy) { return false; }
    virtual bool handleKey(uint32_t keysym) { return false; }

    Rect bounds;
    bool hovered = false;
    Animated hoverAnim_{0.0};

protected:
    std::function<void()> onSecondary_;
};

class QSToggleTile : public QSTile {
public:
    QSToggleTile(const std::string& title, const std::string& icon, std::function<bool()> isActive,
                 std::function<void()> onToggle, std::function<std::string()> subtitle = nullptr,
                 Color accent = {0, 0, 0, 0}, Role role = Role::Custom)
        : title_(title),
          icon_(icon),
          isActive_(std::move(isActive)),
          onToggle_(std::move(onToggle)),
          subtitle_(std::move(subtitle)),
          accent_(accent),
          role_(role) {}

    Type type() const override { return Type::Toggle; }
    std::string title() const override { return title_; }
    Role role() const override { return role_; }
    void draw(Painter& p, int64_t now) override;
    void onClick(double x, double y) override;
    bool onScroll(double dx, double dy) override;

    void setOnScroll(std::function<bool(double, double)> callback) {
        onScroll_ = std::move(callback);
    }

private:
    std::string title_;
    std::string icon_;
    std::function<bool()> isActive_;
    std::function<void()> onToggle_;
    std::function<std::string()> subtitle_;
    Color accent_;
    std::function<bool(double, double)> onScroll_;
    Role role_ = Role::Custom;
};

class QSSliderTile : public QSTile {
public:
    // `icon` is the static fallback. The optional trio turns the icon into a
    // live button: `dynamicIcon` overrides the glyph per frame (e.g. speaker →
    // speaker-muted), `onIconClick` fires when the icon itself is clicked (the
    // track still adjusts the value), and `dimmed` greys the row to show the
    // control is currently inert (muted). Omit them for a plain slider.
    QSSliderTile(const std::string& icon, std::function<double()> getValue,
                 std::function<void(double)> onValueChange,
                 std::function<std::string()> dynamicIcon = nullptr,
                 std::function<void()> onIconClick = nullptr,
                 std::function<bool()> dimmed = nullptr, const std::string& title = "Q27G41ZDF",
                 Role role = Role::Custom)
        : icon_(icon),
          getValue_(std::move(getValue)),
          onValueChange_(std::move(onValueChange)),
          dynamicIcon_(std::move(dynamicIcon)),
          onIconClick_(std::move(onIconClick)),
          dimmed_(std::move(dimmed)),
          title_(title),
          role_(role) {}

    Type type() const override { return Type::Slider; }
    std::string title() const override { return title_; }
    Role role() const override { return role_; }
    void draw(Painter& p, int64_t now) override;
    void onClick(double x, double y) override;
    void onDrag(double x, double y) override;
    bool handleKey(uint32_t keysym) override;

private:
    void updateValueFromCoord(double x);
    bool stepValue(bool up);
    std::string currentIcon() const { return dynamicIcon_ ? dynamicIcon_() : icon_; }

    std::string icon_;
    std::function<double()> getValue_;
    std::function<void(double)> onValueChange_;
    std::function<std::string()> dynamicIcon_;
    std::function<void()> onIconClick_;
    std::function<bool()> dimmed_;
    std::string title_;
    Role role_ = Role::Custom;

    Rect sliderTrackBounds_;
    Rect iconBounds_;
};

class QSInfoTile : public QSTile {
public:
    // `icon` is the static fallback; the optional `dynamicIcon` re-reads the
    // glyph every frame, the same way QSSliderTile does. Prefer the dynamic form
    // whenever the glyph encodes state (a battery level, a signal tier): tiles
    // are constructed once, before any backend has reported, so a captured
    // string freezes whatever placeholder was current at construction and never
    // tracks the value again.
    QSInfoTile(const std::string& title, const std::string& icon,
               std::function<double()> getProgress, std::function<std::string()> getInfo,
               std::function<std::string()> dynamicIcon = nullptr)
        : title_(title),
          icon_(icon),
          getProgress_(std::move(getProgress)),
          getInfo_(std::move(getInfo)),
          dynamicIcon_(std::move(dynamicIcon)) {}

    Type type() const override { return Type::Info; }
    std::string title() const override { return title_; }
    void draw(Painter& p, int64_t now) override;

private:
    std::string currentIcon() const { return dynamicIcon_ ? dynamicIcon_() : icon_; }

    std::string title_;
    std::string icon_;
    std::function<double()> getProgress_;
    std::function<std::string()> getInfo_;
    std::function<std::string()> dynamicIcon_;
};

// ─── Header (avatar + name) ────────────────────────────────────────────────
// A non-interactive row at the top of the panel showing the current user
// (avatar circle, name, optional subtitle like email/hostname). Spans the
// full panel width; onClick is a no-op.
class QSHeaderTile : public QSTile {
public:
    QSHeaderTile(std::string title, std::string subtitle, Color avatarColor)
        : title_(std::move(title)),
          subtitle_(std::move(subtitle)),
          avatarColor_(avatarColor) {}

    Type type() const override { return Type::Header; }
    std::string title() const override { return title_; }
    void draw(Painter& p, int64_t now) override;

private:
    std::string title_;
    std::string subtitle_;
    Color avatarColor_;
};

// ─── Power button ──────────────────────────────────────────────────────────
// Floating circular button on the top-right of the panel. Click triggers the
// supplied callback (typically the system power menu).
class QSPowerTile : public QSTile {
public:
    explicit QSPowerTile(std::function<void()> onClick) : onClick_(std::move(onClick)) {}

    Type type() const override { return Type::Power; }
    std::string title() const override { return "Power"; }
    void draw(Painter& p, int64_t now) override;
    void onClick(double x, double y) override;

private:
    std::function<void()> onClick_;
};

// ─── Wi-Fi combo tile ────────────────────────────────────────────────────
// A toggle (top) with the SSID name and a thin slider beneath showing signal
// strength. Proper-module behavior: the tile body opens the network picker
// (scan list, per-network connect); the power zone on the right toggles the
// radio. While a scan runs the badge shows a spinner and the subtitle reads
// "Scanning…".
class QSWifiComboTile : public QSTile {
public:
    QSWifiComboTile(std::string ssid, int strength, bool enabled, bool connected, Color accent,
                    std::function<void()> onToggle = {}, std::function<void()> onOpenPicker = {})
        : ssid_(std::move(ssid)),
          strength_(strength),
          enabled_(enabled),
          connected_(connected),
          accent_(accent),
          onToggle_(std::move(onToggle)),
          onOpenPicker_(std::move(onOpenPicker)) {}

    // Legacy 4-arg constructor (assumes connected when ssid is non-empty)
    QSWifiComboTile(std::string ssid, int strength, bool enabled, Color accent,
                    std::function<void()> onToggle = {})
        : QSWifiComboTile(std::move(ssid), strength, enabled, !ssid.empty(), accent,
                          std::move(onToggle), {}) {}

    Type type() const override { return Type::WifiCombo; }
    std::string title() const override { return "Wi-Fi"; }
    Role role() const override { return Role::Wifi; }
    void draw(Painter& p, int64_t now) override;
    // Split hit zones: the right power strip toggles the radio, the body
    // opens the network picker.
    void onClick(double x, double y) override {
        if (x >= bounds.x + bounds.w - kPowerZoneW) {
            if (onToggle_) onToggle_();
        } else {
            if (onOpenPicker_) onOpenPicker_();
        }
    }

    void setEnabled(bool e) { enabled_ = e; }
    void setConnected(bool c) { connected_ = c; }
    void setSsid(std::string s) { ssid_ = std::move(s); }
    void setStrength(int s) { strength_ = s; }
    void setScanning(bool s) { scanning_ = s; }

    static constexpr double kPowerZoneW = 40.0;

private:
    std::string ssid_;
    int strength_ = 0;
    bool enabled_ = false;
    bool connected_ = false;
    bool scanning_ = false;
    Color accent_;
    std::function<void()> onToggle_;
    std::function<void()> onOpenPicker_;
};

// ─── Volume section ────────────────────────────────────────────────────────
// A full-width labelled slider row. The header says "Volume"; the track spans
// the rest of the row.
class QSVolumeTile : public QSTile {
public:
    QSVolumeTile(std::function<double()> getValue, std::function<void(double)> onChange,
                 std::function<std::string()> dynamicIcon, std::function<void()> onIconClick,
                 std::function<bool()> dimmed);

    Type type() const override { return Type::Volume; }
    std::string title() const override { return "Volume"; }
    Role role() const override { return Role::Volume; }
    void draw(Painter& p, int64_t now) override;
    void onClick(double x, double y) override;
    void onDrag(double x, double y) override;
    bool handleKey(uint32_t keysym) override;

private:
    void updateValueFromCoord(double x);
    bool stepValue(bool up);
    std::string currentIcon() const { return dynamicIcon_ ? dynamicIcon_() : icon_; }

    std::string icon_;
    std::function<double()> getValue_;
    std::function<void(double)> onValueChange_;
    std::function<std::string()> dynamicIcon_;
    std::function<void()> onIconClick_;
    std::function<bool()> dimmed_;

    Rect trackBounds_;
    Rect iconBounds_;
};

// ─── Media card ────────────────────────────────────────────────────────────
// A full-width card that summarises the active MPRIS player (title / artist
// / album) and a row of transport controls.
class QSMediaTile : public QSTile {
public:
    explicit QSMediaTile(MprisController* mpris) : mpris_(mpris) {}

    Type type() const override { return Type::Media; }
    std::string title() const override { return "Media"; }
    void draw(Painter& p, int64_t now) override;
    void onClick(double x, double y) override;

private:
    MprisController* mpris_ = nullptr;
    Rect prevBounds_{0, 0, 0, 0};
    Rect playBounds_{0, 0, 0, 0};
    Rect nextBounds_{0, 0, 0, 0};
};

}  // namespace qypr
