// Clock.hpp - The lock screen's chronometer lockup.
//
// One typographic block in the manner of the redesign mock
// (docs/mock/code.html): an index + date line, a fading hairline, the
// oversized monospace time, and a side stack carrying the zone, the local
// time label and the seconds. The parts read as a single composed unit, so
// they are measured and drawn as one block instead of as loose widgets.

#pragma once

#include <string>

#include "render/Painter.hpp"
#include "ui/Widget.hpp"

namespace qypr {

class Painter;

class Clock : public Widget {
public:
    // Responsive type size of the oversized time, set by the layout from the
    // output size (the theme's size-clock is the baseline).
    double fontSize = 96.0;

    // Width available to the lockup, set by the layout. The side stack
    // (zone, label, seconds) is only drawn when it still fits.
    double maxWidth = 0.0;

    // Size of the whole lockup (metadata line + rule + time + side stack).
    Size measure(Painter& p) const;

    // Paint the lockup with its top-left corner at bounds.
    void draw(Painter& p, int64_t now) override;

private:
    struct Metrics {
        Size index;
        Size date;
        Size hours;
        Size minutes;
        Size slash;
        Size zone;
        Size zoneLabel;
        Size seconds;
        double stackX = 0.0;
        double stackWidth = 0.0;
        double width = 0.0;
        double height = 0.0;
        bool stackFits = true;
    };

    Metrics metrics(Painter& p) const;

    static std::string dateString();
    static std::string zoneString();
    static std::string secondsString();
};

}  // namespace qypr
