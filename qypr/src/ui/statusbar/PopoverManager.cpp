// PopoverManager.cpp - Popover manager implementation
#include "ui/statusbar/PopoverManager.hpp"
#include "core/EventLoop.hpp"
#include "render/Painter.hpp"

#include <algorithm>

namespace qypr {

void PopoverManager::open(std::unique_ptr<DetailedPopover> popover, double anchorX,
                          double anchorY) {
    cancelAutoDismiss();
    ownerId_.clear();
    if (borrowed_) {
        borrowed_->handleRelease();
        borrowed_->close();
        borrowedClosing_ = borrowed_;
        borrowed_ = nullptr;
    }
    if (active_) {
        // Move current to transitioning and start closing it
        transitioning_ = std::move(active_);
        transitioning_->close();
    }

    active_ = std::move(popover);
    if (active_) {
        active_->setBackdrop(backdropEnabled_, backdropAlpha_);
        active_->anchorX = anchorX;
        active_->anchorY = anchorY;
        active_->open();
    }
}

void PopoverManager::openBorrowed(DetailedPopover* popover, double anchorX, double anchorY) {
    cancelAutoDismiss();
    ownerId_.clear();
    if (borrowedClosing_ == popover) borrowedClosing_ = nullptr;  // reopened mid-close
    if (active_) {
        transitioning_ = std::move(active_);
        transitioning_->close();
    }
    borrowed_ = popover;
    if (borrowed_) {
        borrowed_->setBackdrop(backdropEnabled_, backdropAlpha_);
        borrowed_->anchorX = anchorX;
        borrowed_->anchorY = anchorY;
        borrowed_->open();
    }
}

void PopoverManager::setBackdrop(bool enabled, double alpha) {
    backdropEnabled_ = enabled;
    backdropAlpha_ = alpha;
    if (active_) active_->setBackdrop(enabled, alpha);
    if (borrowed_) borrowed_->setBackdrop(enabled, alpha);
    if (transitioning_) transitioning_->setBackdrop(enabled, alpha);
    if (borrowedClosing_) borrowedClosing_->setBackdrop(enabled, alpha);
}

void PopoverManager::setTheme(const theme::State& state) {
    if (active_) active_->setTheme(state);
    if (borrowed_) borrowed_->setTheme(state);
    if (transitioning_) transitioning_->setTheme(state);
    if (borrowedClosing_) borrowedClosing_->setTheme(state);
}

void PopoverManager::anchorToStrip(DetailedPopover& pop, bool bottom, const Rect& strip) const {
    // growUp makes getBounds() extend upward from the anchor instead of down.
    pop.growUp = bottom;
    pop.anchorY = bottom ? strip.y - 6.0 : strip.y + strip.h + 6.0;
}

void PopoverManager::cancelAutoDismiss() {
    if (loop_ != nullptr && dismissTimer_ >= 0) { loop_->removeTimer(dismissTimer_); }
    dismissTimer_ = -1;
    onDismiss_ = {};
}

void PopoverManager::resetAutoDismiss(std::function<void()> onDismiss) {
    cancelAutoDismiss();
    DetailedPopover const* p = active();
    if (p == nullptr || loop_ == nullptr) { return; }
    const int ms = p->autoDismissMs();
    if (ms <= 0) { return; }  // popover does not opt into auto-dismiss (e.g. QS)

    onDismiss_ = std::move(onDismiss);
    dismissTimer_ = loop_->addTimer(ms, /*repeat=*/false, [this] {
        dismissTimer_ = -1;  // the loop already removed the one-shot fd
        DetailedPopover const* cur = active();
        if (cur == nullptr || cur->autoDismissMs() <= 0) {
            return;  // gone or not transient
        }
        // Inactivity timer, not hover: a parked pointer is not interaction.
        auto cb = std::move(onDismiss_);
        onDismiss_ = {};
        if (cb) {
            cb();
        } else {
            closeActive();
        }
    });
}

void PopoverManager::clear() {
    closeActive();
    transitioning_.reset();
    borrowedClosing_ = nullptr;
}

void PopoverManager::closeActive() {
    if (auto* pop = active()) { pop->handleRelease(); }
    cancelAutoDismiss();
    ownerId_.clear();
    if (borrowed_) {
        borrowed_->handleRelease();
        borrowed_->close();
        borrowedClosing_ = borrowed_;  // keep drawing it through the fade-out
        borrowed_ = nullptr;
    } else if (active_) {
        transitioning_ = std::move(active_);
        transitioning_->close();
    }
}

void PopoverManager::draw(Painter& p, int64_t now) {
    auto drawPanel = [&](DetailedPopover& panel, double alpha) {
        const Rect b = panel.visibleBounds();
        cairo_save(p.cr());
        cairo_rectangle(p.cr(), b.x, b.y, b.w, b.h);
        cairo_clip(p.cr());
        p.pushGroup();
        panel.draw(p, now);
        p.popGroupWithAlpha(alpha);
        cairo_restore(p.cr());
        if (panel.maxScroll() > 0) {
            const double trackH = std::max(1.0, b.h - 16.0);
            const double thumbH = std::max(16.0, trackH * b.h / panel.contentHeight());
            const double offset = b.y - panel.getBounds().y;
            const double y = b.y + 8.0 + (trackH - thumbH) * offset / panel.maxScroll();
            p.fillRoundedRect({b.x + b.w - 5.0, y, 3.0, thumbH}, 1.5,
                              panel.theme().colors.textSubtle.withAlpha(alpha * 0.6));
        }
    };
    if (transitioning_) {
        double prog = transitioning_->openProgress_.value(now);
        if (!transitioning_->openProgress_.active(now) && prog <= 0.01) {
            transitioning_.reset();
        } else {
            drawPanel(*transitioning_, prog);
        }
    }

    if (borrowedClosing_) {
        double prog = borrowedClosing_->openProgress_.value(now);
        if (!borrowedClosing_->openProgress_.active(now) && prog <= 0.01) {
            borrowedClosing_ = nullptr;
        } else {
            drawPanel(*borrowedClosing_, prog);
        }
    }

    DetailedPopover* cur = active_ ? active_.get() : borrowed_;
    if (cur) {
        double prog = cur->openProgress_.value(now);
        drawPanel(*cur, prog);
    }
}

double PopoverManager::maxContentHeight() const {
    double h = 0.0;
    auto consider = [&](const DetailedPopover* p) {
        if (p) h = std::max(h, p->contentHeight());
    };
    consider(active_.get());
    consider(borrowed_);
    consider(transitioning_.get());
    consider(borrowedClosing_);
    return h;
}

bool PopoverManager::animating(int64_t now) const {
    if (transitioning_ && transitioning_->openProgress_.active(now)) { return true; }
    if (borrowedClosing_ && borrowedClosing_->openProgress_.active(now)) { return true; }
    DetailedPopover* cur = active_ ? active_.get() : borrowed_;
    return cur && cur->openProgress_.active(now);
}

bool PopoverManager::handleClick(double x, double y) {
    DetailedPopover* cur = active_ ? active_.get() : borrowed_;
    if (cur && cur->contains(x, y)) { return cur->handleClick(x, y); }
    return false;
}

bool PopoverManager::handleSecondaryClick(double x, double y) {
    DetailedPopover* cur = active_ ? active_.get() : borrowed_;
    if (cur && cur->contains(x, y)) { return cur->handleSecondaryClick(x, y); }
    return false;
}

bool PopoverManager::handleDrag(double x, double y) {
    DetailedPopover* cur = active_ ? active_.get() : borrowed_;
    if (cur) { return cur->handleDrag(x, y); }
    return false;
}

bool PopoverManager::handleMotion(double x, double y) {
    DetailedPopover* cur = active_ ? active_.get() : borrowed_;
    if (!cur) { return false; }
    const bool handled = cur->handleMotion(x, y);
    return handled || cur->contains(x, y);
}

bool PopoverManager::handleScroll(double dx, double dy) {
    DetailedPopover* cur = active_ ? active_.get() : borrowed_;
    if (cur) { return cur->scrollViewport(dy) || cur->handleScroll(dx, dy); }
    return false;
}

bool PopoverManager::handleKey(uint32_t keysym) {
    DetailedPopover* cur = active_ ? active_.get() : borrowed_;
    if (cur) { return cur->handleKey(keysym); }
    return false;
}

bool PopoverManager::handleText(const std::string& utf8) {
    DetailedPopover* cur = active_ ? active_.get() : borrowed_;
    return cur ? cur->handleText(utf8) : false;
}

bool PopoverManager::activeWantsKeyboard() const {
    DetailedPopover* cur = active_ ? active_.get() : borrowed_;
    return cur && cur->wantsKeyboard();
}

}  // namespace qypr
