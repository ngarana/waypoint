// NotificationIndicator.hpp - Notification centre for the unlocked bar.
//
// A bell with a count, plus a history popover, over the existing
// NotificationMonitor — the same daemon-agnostic D-Bus observer the lock screen
// uses (SwayNC, dunst, mako, …). No daemon-specific client is involved
// (STATUS_BAR.md decision D6), so this works wherever the monitor does.
//
// Session-sensitive: notification titles and bodies are exactly the content the
// lock screen redacts, so this applet must never appear while locked. The lock
// screen keeps its own notification stack behind the reveal.

#pragma once

#include <string>

#include "ui/statusbar/StatusIndicator.hpp"

namespace qypr {

class EventLoop;
class NotificationMonitor;
class NotificationActions;
class DndState;
class DesktopIndex;
class Painter;

// Render the notification centre with demo data to `p` (offline --preview only).
void previewNotificationCentre(Painter& p, double anchorX, double anchorY, bool growUp,
                               bool backdropEnabled = false, double backdropAlpha = -1.0);

class NotificationIndicator : public StatusIndicator {
public:
    explicit NotificationIndicator(const SystemBackends& backends);

    std::string icon() const override;
    std::string themedIcon() const override;
    std::string label() const override;
    std::string tooltip() const override;
    Color iconColor() const override;

    double measureWidth(Painter& p) override;
    void draw(Painter& p, int64_t now) override;

    bool hasDetailedView() const override { return true; }
    std::unique_ptr<DetailedPopover> createDetailedView() override;

    void onBackendUpdate() override;

    // Reveals notification content: never on the lock screen.
    bool sensitive() const override { return true; }

private:
    size_t count() const;

    static constexpr double kSuperscriptSize = 9.0;
    static constexpr double kSuperscriptOffsetX = 2.0;
    static constexpr double kSuperscriptOffsetY = -4.0;

    NotificationMonitor* monitor_ = nullptr;
    NotificationActions* actions_ = nullptr;  // dismiss/clear (null → read-only)
    DndState* dnd_ = nullptr;
    DesktopIndex* apps_ = nullptr;  // click-to-launch resolution (null ok)
    EventLoop* loop_ = nullptr;     // pidfd reaping (I3); null on qypr-lock
};

}  // namespace qypr
