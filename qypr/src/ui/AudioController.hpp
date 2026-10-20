// AudioController.hpp - Glassmorphic "now playing" panel (ports AudioController.qml
// + AudioMetadata.qml). Shows metadata, progress/LIVE, transport buttons and a
// volume slider, all driven by MprisController. Only drawn while audio is active.

#pragma once

#include "core/Types.hpp"
#include "render/Painter.hpp"
#include "ui/ActionButton.hpp"
#include "ui/Theme.hpp"

namespace qypr {

class Painter;
class MprisController;

class AudioController : public theme::ThemeAware {
public:
    explicit AudioController(MprisController& mpris);

    bool active() const;
    void refresh();  // poll MPRIS (called from the 1s tick)

    // Size of the panel. The lock layout places the card before drawing it,
    // so the renderer can measure it without painting.
    Size measure(Painter& p, double maxWidth) const;

    // Draw the panel centred on centerX with its top at topY.
    void draw(Painter& p, int64_t now, double centerX, double topY, double maxWidth);

    // ThemeAware: cascade to the transport buttons.
    void setTheme(const theme::State& state) override;

    // Pointer interaction (surface coordinates). handlePress returns true if the
    // event hit a control (so the caller stops hit-testing further).
    bool handlePress(double x, double y, int64_t now);
    void handleDrag(double x, double y);
    void handleRelease() { dragging_ = false; }
    void updateHover(double x, double y, int64_t now);
    void clearHover(int64_t now);
    bool animating(int64_t now) const;

private:
    // The panel's measured rows: everything the draw pass needs, computed
    // once so measure() and draw() cannot drift apart.
    struct Metrics {
        double panelW = 0.0;
        double panelH = 0.0;
        double hSource = 0.0;
        double hNow = 0.0;
        double hTitle = 0.0;
        double hSub = 0.0;
        double hProgress = 0.0;
        double hTransport = 0.0;
        double hVolume = 0.0;
        bool showVolume = false;
        bool hasDuration = false;
        std::string source;
        std::string title;
        std::string sub;
    };

    Metrics metrics(Painter& p, double maxWidth) const;

    MprisController& mpris_;
    ActionButton prev_, playPause_, next_;
    Rect volumeTrack_;
    bool dragging_ = false;
};

}  // namespace qypr
