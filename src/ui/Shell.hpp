// Shell.hpp - Root UI compositor: coordinates StatusBar and LockScreen.
//
// Inspired by ChromeOS ash::Shell and GNOME Shell's ScreenShield.
// Implements InputSink to receive events from the platform layer, then routes
// them to the correct child based on priority. Manages the shared idle/dim
// state.
//
// Decoupling contract (STATUS_BAR.md principle 1): LockScreen and StatusBar
// are peers that never reference each other; Shell is the sole composition
// point and all coordination flows through it.

#pragma once

#include "core/Interfaces.hpp"
#include "core/Types.hpp"
#include "system/DndState.hpp"
#include "ui/LockScreen.hpp"
#include "ui/statusbar/StatusBar.hpp"

namespace qypr {

class EventLoop;
class AudioController;
class VideoPlayer;

class Shell : public InputSink {
public:
    Shell(EventLoop& loop, RenderHost& host, PamAuthenticator& pam, PowerManager& power,
          const SystemBackends& backends);

    // Inject optional subsystems (same API LockScreen had).
    void setAudioController(AudioController* audio);
    void setVideoPlayer(VideoPlayer* video);
    void setNotifications(std::vector<Notification> notes);
    void setIdleTimeout(int64_t ms);

    // Render the full compositor stack.
    void draw(cairo_t* cr, int width, int height, int scale);
    bool isAnimating() const;

    // InputSink — routes to StatusBar first (unless a lockscreen modal is
    // active), then LockScreen.
    void onTextInput(const std::string& utf8) override;
    void onSpecialKey(uint32_t keysym, uint32_t modifiers) override;
    void onPointerMotion(int w, int h, double x, double y) override;
    void onPointerButton(int w, int h, double x, double y, uint32_t button, bool pressed) override;
    void onPointerScroll(int w, int h, double x, double y, double dx, double dy) override;
    void onPointerLeave() override;

    // Access for App wiring.
    LockScreen& lockScreen() { return lockScreen_; }
    StatusBar& statusBar() { return statusBar_; }

private:
    void wakeFromIdle();
    void restartIdleTimer();
    void enterIdle();
    void applyNotificationFilter();

    EventLoop& loop_;
    RenderHost& host_;
    VideoPlayer* video_ = nullptr;

    // Children — peers, not parent-child.
    LockScreen lockScreen_;
    StatusBar statusBar_;

    // DND mediation: Shell reads the qypr-local flag and decides what the
    // lockscreen sees. Neither child references the other; the monitor keeps
    // collecting, so the stack reappears intact when DND lifts.
    DndState* dnd_ = nullptr;
    std::vector<Notification> pendingNotes_;

    // Shared idle state (moved from LockScreen).
    bool idle_ = false;
    int64_t idleTimeoutMs_ = 60000;
    int idleTimer_ = -1;
    Animated dimAnim_{0};
};

}  // namespace qypr
