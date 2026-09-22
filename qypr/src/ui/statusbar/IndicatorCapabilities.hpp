// IndicatorCapabilities.hpp - Small capability interfaces for status applets.
//
// StatusIndicator composes all of these (see below); most indicators need
// only a subset. New code — tests, the registry, future hosts — should target
// the narrow capability it actually uses instead of the whole applet, so a
// tile-only consumer never depends on input handling, policy, or popovers.
//
// ARCHITECTURE_REVIEW finding 7: this is the "compose these capabilities"
// alternative. The shared compact-render implementation (crossfade, symbolic
// cache, measure) stays on StatusIndicator as a Template Method base — that
// is shared behavior, not interface bloat, and every indicator keeps it.
#pragma once

#include <cstdint>
#include <memory>
#include <string>

namespace qypr {

class Painter;
class QSTile;
class DetailedPopover;

// Compact bar representation: glyph/symbolic icon, label, tooltip, draw.
class ICompactView {
public:
    virtual ~ICompactView() = default;
    virtual std::string icon() const = 0;
    virtual std::string themedIcon() const = 0;
    virtual std::string label() const = 0;
    virtual std::string tooltip() const = 0;
    virtual double labelFontSize() const = 0;
    virtual double measureWidth(Painter& p) = 0;
    virtual void draw(Painter& p, int64_t now) = 0;
};

// Quick Settings tile contribution (null = no tile).
class ITileProvider {
public:
    virtual ~ITileProvider() = default;
    virtual std::unique_ptr<QSTile> createTile() = 0;
};

// Individual popover contribution (opt-in via hasDetailedView).
class IDetailProvider {
public:
    virtual ~IDetailProvider() = default;
    virtual bool hasDetailedView() const = 0;
    virtual std::unique_ptr<DetailedPopover> createDetailedView() = 0;
};

// Frame-loop lifecycle: polling, backend pushes, activation, animation.
class IIndicatorLifecycle {
public:
    virtual ~IIndicatorLifecycle() = default;
    virtual void poll(int64_t now) = 0;
    virtual void onBackendUpdate() = 0;
    virtual void onActivate() = 0;
    virtual bool animating(int64_t now) const = 0;
};

// Pointer input forwarded by the host (positions are surface-relative).
class IIndicatorInput {
public:
    virtual ~IIndicatorInput() = default;
    virtual bool onScroll(double dx, double dy, double x, double y) = 0;
    virtual bool onClick(double x, double y) = 0;
    virtual bool onSecondaryClick(double x, double y) = 0;
    virtual bool onMiddleClick(double x, double y) = 0;
};

// Visibility/interactivity policy: session sensitivity, lock-screen
// allow-list (default deny, see LOCK_SECURITY_REVIEW QL-1/QL-2/QL-4),
// QS-only applets.
class IIndicatorPolicy {
public:
    virtual ~IIndicatorPolicy() = default;
    virtual bool sensitive() const = 0;
    virtual bool lockInteractive() const = 0;
    virtual bool qsOnly() const = 0;
};

}  // namespace qypr
