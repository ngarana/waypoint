// AudioController.hpp - Glassmorphic "now playing" panel (ports AudioController.qml
// + AudioMetadata.qml). Shows metadata, progress/LIVE, transport buttons and a
// volume slider, all driven by MprisController. Only drawn while audio is active.

#pragma once

#include "core/Types.hpp"
#include "ui/ActionButton.hpp"

namespace qypr {

class Painter;
class MprisController;

class AudioController {
public:
    explicit AudioController(MprisController& mpris);

    bool active() const;
    void refresh();  // poll MPRIS (called from the 1s tick)

    // Draw the panel centred on centerX with its top at topY.
    void draw(Painter& p, int64_t now, double centerX, double topY, double maxWidth);

    // Pointer interaction (surface coordinates). handlePress returns true if the
    // event hit a control (so the caller stops hit-testing further).
    bool handlePress(double x, double y, int64_t now);
    void handleDrag(double x, double y);
    void handleRelease() { dragging_ = false; }
    void updateHover(double x, double y, int64_t now);
    void clearHover(int64_t now);
    bool animating(int64_t now) const;

private:
    MprisController& mpris_;
    ActionButton prev_, playPause_, next_;
    Rect volumeTrack_;
    bool dragging_ = false;
};

}  // namespace qypr
