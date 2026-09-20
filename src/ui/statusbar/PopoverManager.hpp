// PopoverManager.hpp - Manages popover lifecycle, animations, and input routing.
#pragma once

#include "ui/statusbar/DetailedPopover.hpp"
#include <memory>

namespace qypr {

class PopoverManager {
public:
    PopoverManager() = default;
    ~PopoverManager() = default;

    // Open a new popover, closing the active one first
    void open(std::unique_ptr<DetailedPopover> popover, double anchorX, double anchorY);
    // Open a non-owning popover (caller retains lifetime). Used for qsPanel_.
    void openBorrowed(DetailedPopover* popover, double anchorX, double anchorY);
    void closeActive();

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
};

}  // namespace qypr
