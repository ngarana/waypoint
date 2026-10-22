// AudioController.hpp - Compact "now playing" strip for the lock footer.
//
// Shows the track title and artist, progress/LIVE and the transport buttons,
// all driven by MprisController. Only drawn while audio is active. The panel
// form (metadata stack + volume slider) was replaced by the footer strip the
// redesign mock calls for.

#pragma once

#include "core/Types.hpp"
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

    // Draw the strip inside `area`, the layout's footer slot.
    void draw(Painter& p, int64_t now, const Rect& area);

    // ThemeAware: cascade to the transport buttons.
    void setTheme(const theme::State& state) override;

    // Pointer interaction (surface coordinates). handlePress returns true if the
    // event hit a control (so the caller stops hit-testing further).
    bool handlePress(double x, double y, int64_t now);
    void updateHover(double x, double y, int64_t now);
    void clearHover(int64_t now);
    bool animating(int64_t now) const;

private:
    MprisController& mpris_;
    ActionButton prev_, playPause_, next_;
};

}  // namespace qypr
