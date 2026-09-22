// StatusBarLayout.hpp - Pure three-zone strip layout (no indicator types).
//
// Accepts measured item widths + visibility and returns strip/content/right-
// group geometry plus per-item bounds. StatusBar measures indicators, calls
// computeStatusBarLayout, then writes the returned rects back onto them.
#pragma once

#include <vector>

#include "core/Types.hpp"
#include "ui/Theme.hpp"

namespace qypr {

// Panel geometry. Defaults reproduce the compiled-in lockscreen strip; qypr-bar
// overrides them from bar.conf (Phase 9). `bottom` mirrors the bar to the lower
// screen edge — including the direction popovers open.
struct BarGeometry {
    double height = theme::kDefaultState.statusbar.height;
    double edgeMargin = theme::kDefaultState.statusbar.topMargin;  // gap from the anchored edge
    double sideMargin = theme::kDefaultState.statusbar.sideMargin;
    bool bottom = false;
};

// One measured strip item. `shown == false` yields a zero rect (same as the
// previous isShown() skip path).
struct LayoutItem {
    double width = 0.0;
    bool shown = true;
};

struct LayoutRequest {
    BarGeometry geom;
    double pad = 0.0;      // theme().statusbar.padding
    double spacing = 0.0;  // theme().statusbar.iconSpacing
    int screenW = 0;
    int screenH = 0;
    std::vector<LayoutItem> left;
    std::vector<LayoutItem> center;
    std::vector<LayoutItem> right;
};

struct LayoutResult {
    Rect bounds;            // full strip (side margins applied)
    Rect contentBounds;     // centered content span (or = bounds)
    Rect rightGroupBounds;  // union of visible right-zone items
    std::vector<Rect> left;
    std::vector<Rect> center;
    std::vector<Rect> right;
};

// Pure: same geometry as StatusBar::layout's historical three-zone pass,
// including content centering and right-group chip bounds. Testable at
// multiple widths and both bar edges without constructing a StatusBar.
LayoutResult computeStatusBarLayout(const LayoutRequest& req);

}  // namespace qypr
