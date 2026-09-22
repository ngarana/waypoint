// PopoverManager.hpp - Manages popover lifecycle, anchoring, auto-dismiss,
// animations, and input routing.
#pragma once

#include "ui/statusbar/DetailedPopover.hpp"
#include <functional>
#include <memory>

namespace qypr {

class EventLoop;

class PopoverManager {
public:
    PopoverManager() = default;
    ~PopoverManager() = default;

    // Bind the bar's event loop so auto-dismiss timers can be armed. Called
    // once from the StatusBar constructor; without a loop, resetAutoDismiss
    // is a no-op (standalone unit tests construct the manager bare).
    void setEventLoop(EventLoop* loop) { loop_ = loop; }

    // Open a new popover, closing the active one first
    void open(std::unique_ptr<DetailedPopover> popover, double anchorX, double anchorY);
    // Open a non-owning popover (caller retains lifetime). Used for qsPanel_.
    void openBorrowed(DetailedPopover* popover, double anchorX, double anchorY);
    void closeActive();

    // Point a popover away from the anchored screen edge (down for a top bar,
    // up for a bottom bar); 6px gap past the strip.
    void anchorToStrip(DetailedPopover& pop, bool bottom, const Rect& strip) const;

    // (Re)arm the active popover's auto-dismiss timer from autoDismissMs().
    // Interaction paths call this; a resting pointer does not. No-op when the
    // popover opts out (autoDismissMs() == 0) or no event loop is bound.
    // `onDismiss` runs when the timer fires (typically close + invalidate).
    void resetAutoDismiss(std::function<void()> onDismiss = {});

    // Propagate the standalone bar's backdrop policy to current and future
    // popovers, including a borrowed Quick Settings panel.
    void setBackdrop(bool enabled, double alpha);

    DetailedPopover* active() const { return active_.get() ? active_.get() : borrowed_; }
    bool isTransitioning() const { return transitioning_.get() != nullptr; }

    // Tallest contentHeight() among every popover currently being drawn — the
    // active/borrowed one plus any still fading out. Zero when nothing is drawn.
    // The layer-shell host sizes its overlay surface to fit this (never the whole
    // screen), so opening a popover doesn't resize a full-window surface.
    double maxContentHeight() const;

    void draw(Painter& p, int64_t now);
    bool animating(int64_t now) const;

    // Input forwarding. Returns true if handled/consumed.
    bool handleClick(double x, double y);
    bool handleSecondaryClick(double x, double y);
    bool handleDrag(double x, double y);
    bool handleMotion(double x, double y);
    bool handleScroll(double dx, double dy);
    bool handleKey(uint32_t keysym);
    bool handleText(const std::string& utf8);

    // True when the active popover needs keyboard focus (launcher search).
    bool activeWantsKeyboard() const;

private:
    std::unique_ptr<DetailedPopover> active_;
    DetailedPopover* borrowed_ = nullptr;             // non-owning pointer for stack panels
    std::unique_ptr<DetailedPopover> transitioning_;  // owned popover fading out
    DetailedPopover* borrowedClosing_ = nullptr;      // borrowed popover fading out

    bool backdropEnabled_ = false;
    double backdropAlpha_ = -1.0;

    // Auto-dismiss: inactivity timer for transient popovers. -1 when unarmed.
    EventLoop* loop_ = nullptr;
    int dismissTimer_ = -1;
    std::function<void()> onDismiss_;

    void cancelAutoDismiss();
};

}  // namespace qypr
