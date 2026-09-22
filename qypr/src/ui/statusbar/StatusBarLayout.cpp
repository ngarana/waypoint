// StatusBarLayout.cpp - Three-zone strip layout implementation.
#include "ui/statusbar/StatusBarLayout.hpp"

#include <algorithm>
#include <ranges>

namespace qypr {

namespace {

// Place a zone's shown items left→right starting at `x0`; hidden items get
// zero rects. Returns the x cursor after the last shown item.
double placeForward(const std::vector<LayoutItem>& items, double x0, double y, double h,
                    double spacing, std::vector<Rect>& out) {
    out.assign(items.size(), Rect{0, 0, 0, 0});
    double x = x0;
    for (size_t i = 0; i < items.size(); ++i) {
        if (!items[i].shown) { continue; }
        out[i] = {.x = x, .y = y, .w = items[i].width, .h = h};
        x += items[i].width + spacing;
    }
    return x;
}

// Place a zone's shown items right→left ending so the last item's right edge
// is at `x1` (before trailing pad). Hidden items stay zero.
void placeReverse(const std::vector<LayoutItem>& items, double x1, double y, double h,
                  double spacing, std::vector<Rect>& out) {
    out.assign(items.size(), Rect{0, 0, 0, 0});
    double x = x1;
    for (size_t n = items.size(); n-- > 0;) {
        if (!items[n].shown) { continue; }
        x -= items[n].width;
        out[n] = {.x = x, .y = y, .w = items[n].width, .h = h};
        x -= spacing;
    }
}

Rect rightGroupOf(const std::vector<LayoutItem>& items, const std::vector<Rect>& rects,
                  const Rect& fallbackY, double h) {
    double minX = fallbackY.x + fallbackY.w;
    double maxX = 0.0;
    bool any = false;
    for (size_t i = 0; i < items.size(); ++i) {
        if (!items[i].shown || rects[i].w <= 0.0) { continue; }
        minX = std::min(rects[i].x, minX);
        maxX = std::max(rects[i].x + rects[i].w, maxX);
        any = true;
    }
    if (!any || minX >= maxX) { return Rect{0, 0, 0, 0}; }
    return {.x = minX, .y = fallbackY.y, .w = maxX - minX, .h = h};
}

double spanWidth(const std::vector<LayoutItem>& items, const std::vector<Rect>& rects) {
    double left = 1e300;
    double right = -1e300;
    for (size_t i = 0; i < items.size(); ++i) {
        if (!items[i].shown || rects[i].w <= 0.0) { continue; }
        left = std::min(rects[i].x, left);
        right = std::max(rects[i].x + rects[i].w, right);
    }
    return right > left ? right - left : 0.0;
}

}  // namespace

LayoutResult computeStatusBarLayout(const LayoutRequest& req) {
    LayoutResult r;
    const BarGeometry& g = req.geom;
    const double side = g.sideMargin;
    const double barH = g.height;
    const double y = g.bottom ? req.screenH - g.edgeMargin - barH : g.edgeMargin;

    r.bounds = {.x = side, .y = y, .w = req.screenW - (2 * side), .h = barH};

    // Pass 1: natural positions within the full-width strip.
    placeForward(req.left, r.bounds.x + req.pad, y, barH, req.spacing, r.left);
    placeReverse(req.right, r.bounds.x + r.bounds.w - req.pad, y, barH, req.spacing, r.right);
    r.rightGroupBounds = rightGroupOf(req.right, r.right, r.bounds, barH);

    // Center zone: measure visible span, then center it in the strip.
    double totalCenterW = 0.0;
    int visibleCenter = 0;
    for (const auto& it : req.center) {
        if (!it.shown) { continue; }
        totalCenterW += it.width;
        ++visibleCenter;
    }
    if (visibleCenter > 1) { totalCenterW += (visibleCenter - 1) * req.spacing; }
    {
        double cx = r.bounds.x + ((r.bounds.w - totalCenterW) / 2.0);
        r.center.assign(req.center.size(), Rect{0, 0, 0, 0});
        for (size_t i = 0; i < req.center.size(); ++i) {
            if (!req.center[i].shown) { continue; }
            r.center[i] = {.x = cx, .y = y, .w = req.center[i].width, .h = barH};
            cx += req.center[i].width + req.spacing;
        }
    }

    // Content span across all zones (for centering the whole strip content).
    auto accumulate = [&](const std::vector<LayoutItem>& items, const std::vector<Rect>& rects,
                          double& left, double& right) {
        for (size_t i = 0; i < items.size(); ++i) {
            if (!items[i].shown || rects[i].w <= 0.0) { continue; }
            left = std::min(rects[i].x, left);
            right = std::max(rects[i].x + rects[i].w, right);
        }
    };
    double contentLeft = r.bounds.x + r.bounds.w;
    double contentRight = r.bounds.x;
    accumulate(req.left, r.left, contentLeft, contentRight);
    accumulate(req.right, r.right, contentLeft, contentRight);
    accumulate(req.center, r.center, contentLeft, contentRight);

    int contentW = 0;
    if (contentRight > contentLeft) {
        contentW = static_cast<int>(contentRight - contentLeft + (2 * req.pad) + 0.5);
    }
    contentW = std::min(contentW, req.screenW);

    r.contentBounds = r.bounds;
    const double innerW = req.screenW - (2 * side);
    if (contentW > 0 && contentW < static_cast<int>(innerW)) {
        const double offset = (innerW - contentW) / 2.0;
        r.contentBounds = {
            .x = r.bounds.x + offset, .y = y, .w = static_cast<double>(contentW), .h = barH};

        // Re-place zones inside the centered content span.
        placeForward(req.left, r.bounds.x + offset + req.pad, y, barH, req.spacing, r.left);
        placeReverse(req.right, r.bounds.x + offset + contentW - req.pad, y, barH, req.spacing,
                     r.right);
        r.rightGroupBounds = rightGroupOf(req.right, r.right, r.contentBounds, barH);

        double cx = r.bounds.x + offset + ((contentW - totalCenterW) / 2.0);
        r.center.assign(req.center.size(), Rect{0, 0, 0, 0});
        for (size_t i = 0; i < req.center.size(); ++i) {
            if (!req.center[i].shown) { continue; }
            r.center[i] = {.x = cx, .y = y, .w = req.center[i].width, .h = barH};
            cx += req.center[i].width + req.spacing;
        }
    }

    // Prefer spanWidth for content when contentW rounding is off by a pixel.
    (void)spanWidth;
    return r;
}

}  // namespace qypr
