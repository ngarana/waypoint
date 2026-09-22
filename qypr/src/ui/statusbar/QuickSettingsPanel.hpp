// QuickSettingsPanel.hpp - Shared Quick Settings panel widget.
#pragma once

#include "ui/statusbar/DetailedPopover.hpp"
#include "ui/statusbar/QSTile.hpp"
#include "system/WifiBackend.hpp"  // WifiSnapshot (updateWifi)
#include <vector>
#include <memory>
#include <functional>

namespace qypr {

struct SystemBackends;
class EventLoop;

class QuickSettingsPanel : public DetailedPopover {
public:
    QuickSettingsPanel() = default;
    ~QuickSettingsPanel() override = default;

    // Build the panel's internal tiles from the given system backends.
    // The header power button runs the configured power command
    // ([quick-settings] power-command, default `waylaunch --power`) directly;
    // `onOpenWifi` opens the network picker when the Wi-Fi tile body is
    // clicked (its right power strip toggles the radio instead).
    // The `backends` pointer is borrowed for the panel's lifetime.
    void buildTiles(EventLoop& loop, const SystemBackends& backends,
                    const std::function<void()>& onOpenWifi = {});

    // Live tile refresh, driven by StatusBar::notifyBackendUpdate (the panel
    // deliberately does not own a backend callback: StatusBar owns the single
    // onChange → repaint fan-out, and this keeps the tile in step with it).
    void updateWifi(const WifiSnapshot& s);

    void addTile(std::unique_ptr<QSTile> tile);
    void clearTiles() { tiles_.clear(); }

    // ThemeAware: bind the owner's live copy and cascade to every owned
    // tile (grid + header/power/wifi/volume/media), so a re-theme repaints
    // the whole panel without rebuilding it.
    void setTheme(const theme::State& state) override;

    void draw(Painter& p, int64_t now) override;
    double contentHeight() const override;
    double contentWidth() const override;

    bool handleMotion(double x, double y) override;
    bool handleClick(double x, double y) override;
    // Right-click: the tile's context action — opens the source indicator's
    // detail popover (attached by StatusBar at createTile time); the Wi-Fi
    // tile opens the network picker.
    bool handleSecondaryClick(double x, double y) override;
    bool handleDrag(double x, double y) override;
    bool handleScroll(double dx, double dy) override;
    bool handleKey(uint32_t keysym) override;
    bool consumeCloseRequest() override;
    // Persistent: QS is a deliberate panel with its own pointer-leave dismissal;
    // it must not time out while open.
    int autoDismissMs() const override { return 0; }

    // Geometry shorthands bound to the live theme (replacing the old
    // file-scope const& to the globals): every layout/draw read goes through
    // these, so a re-theme repaints at the new metrics without rebuilding.
    double panelW() const { return theme().statusbar.qsPanelWidth; }
    double pad() const { return theme().statusbar.qsPadding; }
    double gap() const { return theme().statusbar.qsTileGap; }
    double gridRowH() const { return theme().statusbar.qsTileHeight; }

    Rect boundsFor(QSTile::Role role) const;
    Rect findTileBounds(const std::string& tileTitle) const;

    QSTile* activeDragTile_ = nullptr;
    double curX_ = -1, curY_ = -1;

private:
    void layoutTiles();

    // Indicator-created tiles (toggle, slider, info) placed in the grid
    std::vector<std::unique_ptr<QSTile>> tiles_;

    // Internal panel tiles (owned, placed by the panel itself)
    std::unique_ptr<QSHeaderTile> header_;
    std::unique_ptr<QSPowerTile> power_;
    std::unique_ptr<QSWifiComboTile> wifiCombo_;
    std::unique_ptr<QSVolumeTile> volume_;
    std::unique_ptr<QSMediaTile> media_;

    // Bounds for the internal tiles (set during layout)
    Rect powerBounds_;
    Rect closeBounds_;
    bool closeRequested_ = false;
    // Opens the network picker (wired by StatusBar at buildTiles time).
    std::function<void()> onOpenWifi_;
};

}  // namespace qypr
