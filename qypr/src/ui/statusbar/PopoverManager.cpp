// PopoverManager.cpp - Popover manager implementation
#include "ui/statusbar/PopoverManager.hpp"
#include "core/EventLoop.hpp"
#include "render/Painter.hpp"

#include <algorithm>

namespace qypr {

void PopoverManager::open(std::unique_ptr<DetailedPopover> popover, double anchorX,
                          double anchorY) {
    cancelAutoDismiss();
    if (borrowed_) {
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

void PopoverManager::closeActive() {
    cancelAutoDismiss();
    if (borrowed_) {
        borrowed_->close();
        borrowedClosing_ = borrowed_;  // keep drawing it through the fade-out
        borrowed_ = nullptr;
    } else if (active_) {
        transitioning_ = std::move(active_);
        transitioning_->close();
    }
}

void PopoverManager::draw(Painter& p, int64_t now) {
    if (transitioning_) {
        double prog = transitioning_->openProgress_.value(now);
        if (!transitioning_->openProgress_.active(now) && prog <= 0.01) {
            transitioning_.reset();
        } else {
            p.pushGroup();
            transitioning_->draw(p, now);
            p.popGroupWithAlpha(prog);
        }
    }

    if (borrowedClosing_) {
        double prog = borrowedClosing_->openProgress_.value(now);
        if (!borrowedClosing_->openProgress_.active(now) && prog <= 0.01) {
            borrowedClosing_ = nullptr;
        } else {
            p.pushGroup();
            borrowedClosing_->draw(p, now);
            p.popGroupWithAlpha(prog);
        }
    }

    DetailedPopover* cur = active_ ? active_.get() : borrowed_;
    if (cur) {
        double prog = cur->openProgress_.value(now);
        p.pushGroup();
        cur->draw(p, now);
        p.popGroupWithAlpha(prog);
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
    if (cur && cur->contains(x, y)) { return cur->handleDrag(x, y); }
    return false;
}

bool PopoverManager::handleMotion(double x, double y) {
    DetailedPopover* cur = active_ ? active_.get() : borrowed_;
    return cur ? cur->handleMotion(x, y) : false;
}

bool PopoverManager::handleScroll(double dx, double dy) {
    DetailedPopover* cur = active_ ? active_.get() : borrowed_;
    if (cur) { return cur->handleScroll(dx, dy); }
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
