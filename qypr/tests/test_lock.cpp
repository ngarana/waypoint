// test_lock.cpp - Lock-screen input, unlock gating, secret wiping.
// Split verbatim from tests/unit_tests.cpp; bodies unchanged.
#include "test_framework.hpp"

TEST(LockScreenInputHandling) {
    qypr::EventLoop loop;
    qypr::App app;
    qypr::PamAuthenticator pam(loop);
    qypr::PowerManager power(loop);

    qypr::LockScreen screen(loop, app, pam, power);

    // Test text inputs
    screen.handleTextInput("a");
    screen.handleTextInput("b");
    screen.handleSpecialKey(0xff08, 0);  // Backspace keysym
    screen.handleSpecialKey(0xff0d, 0);  // Enter keysym (submits pam auth)

    // Test pointer events
    screen.handlePointerMotion(800, 600, 100, 100);
    screen.handlePointerButton(800, 600, 100, 100, 272, true);   // left press
    screen.handlePointerButton(800, 600, 100, 100, 272, false);  // left release
    screen.handlePointerLeave();

    // Verify clock layout & icon resolver
    EXPECT_TRUE(screen.isAnimating());
}

// The core lockscreen invariant: the session unlocks if and only if PAM
// reports Success. A wrong password or a PAM subsystem error must never reach
// requestUnlock(). A spy RenderHost lets us observe that directly.
TEST(LockScreenUnlockGating) {
    qypr::EventLoop loop;
    struct SpyHost : qypr::RenderHost {
        int unlocks = 0;
        void invalidate() override {}
        void requestUnlock() override { ++unlocks; }
    } host;
    qypr::PamAuthenticator pam(loop);
    qypr::PowerManager power(loop);
    qypr::LockScreen screen(loop, host, pam, power);

    mock_pam_reset();
    mock_pam_set_expected_password("open-sesame");

    auto submit = [&](const std::string& pw) {
        screen.password_.clear();
        screen.password_.append(pw);  // private, exposed for this test TU
        screen.submitPassword();
        while (pam.busy()) {}   // wait for the auth worker to finish
        loop.dispatchPosted();  // run onAuthResult on the loop thread
    };

    // Wrong password: no unlock, error surfaced, secret wiped.
    submit("wrong");
    EXPECT_EQ(host.unlocks, 0);
    EXPECT_TRUE(screen.hasError_);
    EXPECT_TRUE(screen.password_.empty());

    // PAM subsystem error (not a plain auth failure): still no unlock.
    mock_pam_force_rc(PAM_USER_UNKNOWN);
    submit("open-sesame");
    EXPECT_EQ(host.unlocks, 0);
    mock_pam_force_rc(-1);

    // Correct password: unlocks exactly once and enters the unlocking state.
    submit("open-sesame");
    EXPECT_EQ(host.unlocks, 1);
    EXPECT_TRUE(screen.unlocking_);
}

// A monitor hot-plugged while the session is locked must be covered by a lock
// surface immediately — otherwise the desktop behind it is exposed. Conversely,
// an output that appears while unlocked must NOT get a lock surface. Registry
// globals are driven directly (connect()'s global burst is one-shot per run).
TEST(LockScreenWipesPasswordAfterSubmit) {
    qypr::EventLoop loop;
    struct SpyHost : qypr::RenderHost {
        void invalidate() override {}
        void requestUnlock() override {}
    } host;
    qypr::PamAuthenticator pam(loop);
    qypr::PowerManager power(loop);
    qypr::LockScreen screen(loop, host, pam, power);

    mock_pam_reset();
    mock_pam_set_expected_password("open-sesame");

    auto& secret = screen.password();  // TESTING seam
    secret.append("open-sesame");
    EXPECT_FALSE(secret.empty());

    screen.submitPassword();
    while (pam.busy()) {}
    loop.dispatchPosted();

    // Moved into the worker and wiped there: the field is empty again and ready
    // for the next keystroke.
    EXPECT_TRUE(secret.empty());

    // A wrong attempt also wipes the field (onAuthResult clears it).
    secret.append("wrong");
    screen.submitPassword();
    while (pam.busy()) {}
    loop.dispatchPosted();
    EXPECT_TRUE(secret.empty());
}

// QL-5/QL-6: helpers are resolved once at startup (no PATH lookup inside the
// child), spawned with posix_spawn (never fork()), and reaped through the event
// loop without any process-wide SIGCHLD disposition.
TEST(IndicatorsDenyByDefaultOnLockScreen) {
    qypr::EventLoop loop;
    struct SpyHost : qypr::RenderHost {
        void invalidate() override {}
        void requestUnlock() override {}
    } host;
    qypr::SystemBackends const backends{};
    qypr::StatusBar bar(loop, host, backends);

    // A bare indicator opts into nothing: shown (visibility is separate), but
    // never interactive while the session is locked.
    struct BareIndicator : qypr::StatusIndicator {
        using StatusIndicator::StatusIndicator;
        [[nodiscard]] std::string icon() const override { return ""; }
        [[nodiscard]] std::string tooltip() const override { return ""; }
    } const bare("test-indicator", qypr::Zone::Right, 100);

    EXPECT_TRUE(bare.visible);             // visibility: allowed (a field, not a call)
    EXPECT_FALSE(bare.lockInteractive());  // default deny on the lock screen

    bar.setSessionContentVisible(false);    // the lock state
    EXPECT_FALSE(bar.isInteractive(bare));  // the bar enforces the policy
    bar.setSessionContentVisible(true);     // the unlocked bar
    EXPECT_TRUE(bar.isInteractive(bare));

    // The explicit allow-list still works when locked.
    qypr::VolumeIndicator const vol(backends);
    EXPECT_TRUE(vol.lockInteractive());
    bar.setSessionContentVisible(false);
    EXPECT_TRUE(bar.isInteractive(vol));
}

// QL-1: the Bluetooth pairing agent must be off while the session is locked.

// I1 allow-list (maintainer decision, INTEGRATION.md §9 Q2): while locked,
// exactly these interactions stay live — volume/brightness scroll, media
// transport, and notification expand/collapse + dismiss. Everything else is
// default-deny. This test pins the set: adding an opt-in without updating it
// fails loudly, which is the point.
TEST(LockInteractionAllowList) {
    qypr::SystemBackends const backends{};
    // Scroll/transport surfaces.
    EXPECT_TRUE(qypr::VolumeIndicator(backends).lockInteractive());
    EXPECT_TRUE(qypr::BrightnessIndicator(backends).lockInteractive());
    EXPECT_TRUE(qypr::MediaIndicator(backends).lockInteractive());
    // The launcher, power and tray applets stay inert (representative sample;
    // the default-deny test above covers the general rule).
    EXPECT_FALSE(qypr::LauncherIndicator(backends).lockInteractive());
    EXPECT_FALSE(qypr::PowerMenuIndicator(backends).lockInteractive());

    // Notification cards: body press expands (consumed), × dismisses.
    qypr::Notification note;
    note.id = 7;
    note.app = "Mail";
    note.title = "Hello";
    note.body = "World";
    qypr::NotificationView view;
    view.update({note});
    EXPECT_TRUE(view.active());

    cairo_surface_t* surf = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 400, 400);
    cairo_t* cr = cairo_create(surf);
    qypr::Painter p(cr);
    const int64_t now = 1'000'000;
    view.draw(p, now, 0.0, 400.0, 360.0);

    // Body press is consumed (expand toggle).
    EXPECT_TRUE(view.handlePress(10.0, 390.0, now));
    EXPECT_TRUE(view.active());
    // Empty space is not.
    EXPECT_FALSE(view.handlePress(395.0, 5.0, now));

    // Sweep the card's top-right corner for the ×; it must dismiss. Grid
    // covers the 24x24 button wherever padding puts it (integer steps —
    // float induction would accumulate error and could skip the button).
    bool dismissed = false;
    for (int yi = 0; yi <= 60 && !dismissed; ++yi) {
        for (int xi = 0; xi <= 15 && !dismissed; ++xi) {
            const double x = 300.0 + (xi * 4.0);
            const double y = 395.0 - (yi * 4.0);
            if (view.handlePress(x, y, now)) {
                // Body presses also consume (expand toggle); only the ×
                // removes the card.
                dismissed = !view.active();
            }
        }
    }
    EXPECT_TRUE(dismissed);
    EXPECT_FALSE(view.active());

    cairo_destroy(cr);
    cairo_surface_destroy(surf);
}
