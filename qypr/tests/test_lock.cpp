// test_lock.cpp - Lock-screen input, unlock gating, secret wiping.
// Split verbatim from tests/unit_tests.cpp; bodies unchanged.
#include "test_framework.hpp"

#include <algorithm>
#include <deque>
#include <functional>
#include <utility>
#include <vector>

TEST(LockScreenInputHandling) {
    qypr::EventLoop loop;
    qypr::App app;
    qypr::PamAuthenticator pam(loop);
    qypr::SystemActions power(loop);

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
    qypr::SystemActions power(loop);
    qypr::LockScreen screen(loop, host, pam, power);

    mock_pam_reset();
    mock_pam_set_expected_password("open-sesame");

    auto submit = [&](const std::string& pw) {
        screen.password().clear();
        screen.password().append(pw);
        screen.submitPassword();
        while (pam.busy()) {}   // wait for the auth worker to finish
        loop.dispatchPosted();  // run onAuthResult on the loop thread
    };

    // Wrong password: no unlock, error surfaced, secret wiped.
    submit("wrong");
    EXPECT_EQ(host.unlocks, 0);
    EXPECT_TRUE(screen.hasError());
    EXPECT_TRUE(screen.password().empty());

    // PAM subsystem error (not a plain auth failure): still no unlock.
    mock_pam_force_rc(PAM_USER_UNKNOWN);
    submit("open-sesame");
    EXPECT_EQ(host.unlocks, 0);
    mock_pam_force_rc(-1);

    // Correct password: unlocks exactly once and enters the unlocking state.
    submit("open-sesame");
    EXPECT_EQ(host.unlocks, 1);
    EXPECT_TRUE(screen.unlocking());
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
    qypr::SystemActions power(loop);
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

// The redesigned lock screen: the auth card's inline UNLOCK affordance is
// hit-testable and submits the password like the Enter key does.
TEST(AuthPanelUnlockAffordanceSubmits) {
    qypr::EventLoop loop;
    struct SpyHost : qypr::RenderHost {
        int unlocks = 0;
        void invalidate() override {}
        void requestUnlock() override { ++unlocks; }
    } host;
    qypr::PamAuthenticator pam(loop);
    qypr::SystemActions power(loop);
    qypr::LockScreen screen(loop, host, pam, power);

    mock_pam_reset();
    mock_pam_set_expected_password("open-sesame");

    // Typing wakes the screen; the auth card fades in with the reveal.
    screen.handleTextInput("open-sesame");
    std::this_thread::sleep_for(std::chrono::milliseconds(700));

    cairo_surface_t* surf = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 1920, 1080);
    cairo_t* cr = cairo_create(surf);
    screen.draw(cr, 1920, 1080, 1);

    const qypr::Rect button = screen.renderer_.auth().unlockButton();
    EXPECT_TRUE(button.w > 0);
    EXPECT_TRUE(button.h > 0);
    EXPECT_TRUE(screen.renderer_.auth().unlockHit(button.cx(), button.cy()));
    // The affordance lives inside the card.
    EXPECT_TRUE(button.x >= screen.renderer_.auth().bounds.x);
    EXPECT_TRUE(button.x + button.w <=
                screen.renderer_.auth().bounds.x + screen.renderer_.auth().bounds.w);

    screen.handlePointerButton(1920, 1080, button.cx(), button.cy(), 0x110, true);
    while (pam.busy()) {}
    loop.dispatchPosted();

    EXPECT_EQ(host.unlocks, 1);

    cairo_destroy(cr);
    cairo_surface_destroy(surf);
}

// The lock screen draws its whole composition at every supported size: the
// chronometer lockup and the auth card.
TEST(LockScreenComposesAtEverySize) {
    qypr::EventLoop loop;
    struct SpyHost : qypr::RenderHost {
        void invalidate() override {}
        void requestUnlock() override {}
    } host;
    qypr::PamAuthenticator pam(loop);
    qypr::SystemActions power(loop);
    qypr::LockScreen screen(loop, host, pam, power);

    for (const auto [width, height] :
         {std::pair{800, 600}, std::pair{1920, 1080}, std::pair{3440, 1440}}) {
        cairo_surface_t* surf = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, width, height);
        cairo_t* cr = cairo_create(surf);
        screen.draw(cr, width, height, 1);

        const auto& renderer = screen.renderer_;
        EXPECT_TRUE(renderer.clock_.bounds.w > 0);
        EXPECT_TRUE(renderer.clock_.bounds.h > 0);
        EXPECT_TRUE(renderer.auth_.bounds.w > 0);
        // The clock column stays inside the output.
        EXPECT_TRUE(renderer.clock_.bounds.x + renderer.clock_.bounds.w <= width);
        EXPECT_TRUE(renderer.clock_.bounds.y >= 0);
        EXPECT_TRUE(renderer.clock_.bounds.y + renderer.clock_.bounds.h <= height);

        cairo_destroy(cr);
        cairo_surface_destroy(surf);
    }
}

// The facts line stays empty: the fingerprint state lives on the auth tile
// and the PAM stack is no longer repeated as text.
TEST(LockFactsRowIsEmpty) {
    qypr::EventLoop loop;
    struct SpyHost : qypr::RenderHost {
        void invalidate() override {}
        void requestUnlock() override {}
    } host;
    qypr::PamAuthenticator pam(loop);
    qypr::SystemActions power(loop);
    qypr::LockScreen screen(loop, host, pam, power);

    cairo_surface_t* surf = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 1920, 1080);
    cairo_t* cr = cairo_create(surf);
    screen.draw(cr, 1920, 1080, 1);

    EXPECT_TRUE(screen.renderer_.facts_.items.empty());

    cairo_destroy(cr);
    cairo_surface_destroy(surf);
}

TEST(LockLayoutGeometryMultipleSizes) {
    const qypr::LockLayout layout;
    const qypr::Size clock{.w = 300.0, .h = 260.0};
    const qypr::Size facts{.w = 200.0, .h = 14.0};
    const qypr::Size auth{.w = 420.0, .h = 210.0};

    for (const auto [width, height] :
         {std::pair{800, 600}, std::pair{1920, 1080}, std::pair{3440, 1440}}) {
        const auto result = layout.compute(width, height, clock, facts, auth, true);
        const auto row = layout.powerRowRect(width, height);
        const auto anchor = layout.powerAnchorRect(width, height);

        EXPECT_NEAR(row.x + row.w, width - layout.theme().spacing.xlarge, 0.001);
        EXPECT_NEAR(row.y + row.h, height - layout.theme().spacing.xlarge, 0.001);
        EXPECT_TRUE(anchor.y > row.y);

        // The lock's text aligns with the status bar's label inset.
        EXPECT_NEAR(result.clock.x, layout.contentMargin(), 0.001);
        EXPECT_NEAR(result.facts.x, layout.contentMargin(), 0.001);

        // The auth card owns the right column and is bottom-anchored to the
        // content baseline.
        EXPECT_NEAR(result.auth.w, layout.authColumnWidth(width), 0.001);
        EXPECT_TRUE(result.auth.x + result.auth.w <= width - layout.theme().spacing.xlarge);
        const double baseline = result.facts.y + result.facts.h;
        EXPECT_NEAR(result.auth.y + result.auth.h, baseline, 0.001);
        EXPECT_TRUE(baseline <=
                    height - layout.theme().spacing.xlarge - layout.notificationZoneHeight(height));

        // The player is a footer strip sharing the auth column's edges and
        // the notification chips' bottom line, clear of both the chips and
        // the power column.
        EXPECT_NEAR(result.media.x, result.auth.x, 0.001);
        EXPECT_NEAR(result.media.w, result.auth.w, 0.001);
        EXPECT_NEAR(result.media.h, layout.theme().audio.compactHeight, 0.001);
        EXPECT_NEAR(result.media.y + result.media.h, height - layout.theme().spacing.xlarge, 0.001);
        EXPECT_TRUE(result.media.x >= result.clock.x + result.clock.w);
        EXPECT_TRUE(result.media.x + result.media.w <= row.x);
        EXPECT_TRUE(result.media.y >= result.auth.y + result.auth.h);

        // The clock column starts below the status bar.
        EXPECT_TRUE(result.clock.y >= layout.theme().statusbar.topMargin +
                                          layout.theme().statusbar.height +
                                          layout.theme().spacing.small);
        // The facts line hangs under the lockup.
        EXPECT_NEAR(result.facts.y, result.clock.y + result.clock.h + layout.theme().spacing.large,
                    0.001);

        // The two columns never overlap.
        EXPECT_TRUE(result.clock.x + result.clock.w <= result.auth.x);
    }
}

// Without media the right column holds only the auth card.
TEST(LockLayoutWithoutMediaKeepsAuthColumn) {
    const qypr::LockLayout layout;
    const qypr::Size clock{.w = 300.0, .h = 260.0};
    const qypr::Size facts{.w = 200.0, .h = 14.0};
    const qypr::Size auth{.w = 420.0, .h = 210.0};

    const auto result = layout.compute(1920, 1080, clock, facts, auth, false);
    EXPECT_NEAR(result.media.h, 0.0, 0.001);
    EXPECT_NEAR(result.auth.w, layout.authColumnWidth(1920), 0.001);
    // The auth card keeps the content baseline without the player.
    EXPECT_NEAR(result.auth.y + result.auth.h, result.facts.y + result.facts.h, 0.001);
}

// The oversized clock scales with the output but stays inside sane bounds.
TEST(LockClockFontSizeIsResponsive) {
    const qypr::LockLayout layout;
    EXPECT_TRUE(layout.clockFontSize(1920, 1080) > layout.clockFontSize(1280, 720));
    EXPECT_TRUE(layout.clockFontSize(800, 600) >= 56.0);
    EXPECT_TRUE(layout.clockFontSize(7680, 4320) <= 208.0);
}

// The clock's responsive size is still driven by the theme's scale.
TEST(LockClockScaleComesFromTheme) {
    qypr::LockLayout layout;
    const double base = layout.clockFontSize(1920, 1080);
    auto scaled = layout.theme();
    scaled.lock.clock.scale = 0.5;
    layout.setTheme(scaled);
    EXPECT_NEAR(layout.clockFontSize(1920, 1080), base * 0.5, 0.001);
}

TEST(PowerMenuRequiresExplicitConfirmation) {
    struct SpyInvalidator : qypr::Invalidator {
        void invalidate() override { ++invalidations; }
        int invalidations = 0;
    } host;
    qypr::EventLoop loop;
    qypr::SystemActions power(loop);
    const qypr::LockLayout layout;
    qypr::PowerMenuController menu(host, power, layout);
    menu.layout(800, 600);

    const auto anchor = layout.powerAnchorRect(800, 600);
    menu.handlePress(anchor.cx(), anchor.cy(), qypr::nowMs());
    EXPECT_TRUE(menu.expanded());

    const auto action = layout.powerButtonRect(0, 800, 600);
    menu.handlePress(action.cx(), action.cy(), qypr::nowMs());
    EXPECT_TRUE(menu.modalActive());

    menu.dismissModal();
    EXPECT_FALSE(menu.modalActive());
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

// The chip band packs chips into at most two rows; whatever does not fit
// becomes the overflow count. A chip that is dropped must not shift the
// chips behind it: the band indexes the placed chips by their own position.
TEST(NotificationChipBandSurvivesOverflow) {
    qypr::NotificationView view;
    const std::string longTitle = "A very long notification headline that overflows";
    std::vector<qypr::Notification> notes;
    for (uint64_t i = 0; i < 3; ++i) {
        qypr::Notification n;
        n.id = i + 1;
        n.app = "Mail";
        n.title = longTitle;
        notes.push_back(n);
    }
    // A short chip behind the overflowing ones still has to be placed.
    qypr::Notification shortNote;
    shortNote.id = 4;
    shortNote.app = "Mail";
    shortNote.title = "Hi";
    notes.push_back(shortNote);
    view.update(notes);

    cairo_surface_t* surf = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 800, 400);
    cairo_t* cr = cairo_create(surf);
    qypr::Painter p(cr);
    view.draw(p, qypr::nowMs(), 0.0, 400.0, 540.0);
    cairo_destroy(cr);
    cairo_surface_destroy(surf);

    EXPECT_TRUE(view.active());
}

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

    // Notification chips: body press expands (consumed), × dismisses.
    qypr::Notification note;
    note.id = 7;
    note.app = "Mail";
    note.title = "Hello";
    note.body = "World";
    qypr::NotificationView view;
    uint64_t dismissedId = 0;
    view.setOnDismiss([&](const qypr::Notification& dismissed) { dismissedId = dismissed.id; });
    view.update({note});
    EXPECT_TRUE(view.active());

    cairo_surface_t* surf = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 400, 400);
    cairo_t* cr = cairo_create(surf);
    qypr::Painter p(cr);
    // The chips fade in: the first frame starts the animation, the second
    // (after it finished) lays out the hit-test boxes.
    view.draw(p, qypr::nowMs(), 0.0, 400.0, 360.0);
    std::this_thread::sleep_for(std::chrono::milliseconds(700));
    view.draw(p, qypr::nowMs(), 0.0, 400.0, 360.0);

    // Body press is consumed (expand toggle). The chip band is bottom
    // anchored, so the press lands on its last row.
    bool expanded = false;
    for (int yi = 0; yi <= 30 && !expanded; ++yi) {
        for (int xi = 0; xi <= 350 && !expanded; ++xi) {
            const double x = 2.0 + (xi * 2.0);
            const double y = 398.0 - (yi * 2.0);
            expanded = view.handlePress(x, y, qypr::nowMs());
        }
    }
    EXPECT_TRUE(expanded);
    EXPECT_TRUE(view.active());
    // Empty space is not.
    EXPECT_FALSE(view.handlePress(395.0, 5.0, qypr::nowMs()));

    // Re-draw so the hit-test boxes match the expanded chip, then sweep the
    // band for the ×; it must dismiss. Grid covers the 16x16 button wherever
    // padding puts it (integer steps — float induction would accumulate
    // error and could skip the button).
    view.draw(p, qypr::nowMs(), 0.0, 400.0, 360.0);
    bool dismissed = false;
    for (int yi = 0; yi <= 30 && !dismissed; ++yi) {
        for (int xi = 0; xi <= 360 && !dismissed; ++xi) {
            const double x = 360.0 - (xi * 1.0);
            const double y = 400.0 - (yi * 1.0);
            if (view.handlePress(x, y, qypr::nowMs())) {
                // Body presses also consume (expand toggle); only the ×
                // removes the chip.
                dismissed = !view.active();
            }
        }
    }
    EXPECT_TRUE(dismissed);
    EXPECT_FALSE(view.active());
    EXPECT_EQ(dismissedId, 7U);

    // A monitor refresh can still contain the card while the daemon's
    // CloseNotification signal is in flight; the local suppression prevents
    // it from being resurrected. Once the source removes it, the id can be
    // retired normally.
    view.update({note});
    EXPECT_FALSE(view.active());
    view.update({});
    view.update({note});
    EXPECT_TRUE(view.active());

    cairo_destroy(cr);
    cairo_surface_destroy(surf);
}

// Fingerprint unlock (docs/FINGERPRINT.md) is driven against a scripted
// fprintd: FakeFprintd records each D-Bus call and answers it on the next loop
// pass, as the bus would, and tests emit VerifyStatus the way the reader does.
// Everything runs on the loop thread, so pumpUntil() simply runs the loop
// (posted replies and back-off timers both fire) until a condition holds.
namespace {

struct FakeFprintd : qypr::FprintdTransport {
    explicit FakeFprintd(qypr::EventLoop& l) : loop(l) {}

    void setHandlers(StatusHandler s, VanishedHandler v) override {
        status = std::move(s);
        vanished = std::move(v);
    }
    void claim(Reply done) override {
        calls.emplace_back("Claim");
        qypr::FprintdError const e = next(claimErrors);
        if (!e.failed()) { claimed = true; }
        answer(std::move(done), e);
    }
    void verifyStart(Reply done) override {
        calls.emplace_back("VerifyStart");
        qypr::FprintdError const e =
            claimed ? next(startErrors)
                    : qypr::FprintdError{.name = qypr::fprintd_error::kClaimDevice, .message = ""};
        if (!e.failed()) { verifying = true; }
        answer(std::move(done), e);
    }
    void verifyStop(Reply done) override {
        calls.emplace_back("VerifyStop");
        bool const was = verifying;
        verifying = false;
        answer(std::move(done),
               was ? qypr::FprintdError{}
                   : qypr::FprintdError{.name = qypr::fprintd_error::kNoActionInProgress,
                                        .message = ""});
    }
    void release(Reply done) override {
        calls.emplace_back("Release");
        bool const was = claimed;
        claimed = false;
        verifying = false;
        answer(std::move(done),
               was ? qypr::FprintdError{}
                   : qypr::FprintdError{.name = qypr::fprintd_error::kClaimDevice, .message = ""});
    }

    // The reader reports on the live scan (done = the scan is over).
    void emit(const std::string& result, bool done = true) {
        loop.post([this, result, done] {
            if (status) { status(result, done); }
        });
    }
    [[nodiscard]] int count(const std::string& call) const {
        return static_cast<int>(std::count(calls.begin(), calls.end(), call));
    }
    [[nodiscard]] std::string sequence() const {
        std::string out;
        for (const std::string& c : calls) { out += (out.empty() ? "" : ",") + c; }
        return out;
    }
    // While `hold` is set, replies wait for flush() — a slow fprintd.
    void flush() {
        auto pending = std::move(held);
        held.clear();
        for (auto& [done, e] : pending) {
            loop.post([done = std::move(done), e = e] { done(e); });
        }
    }

    static qypr::FprintdError next(std::deque<qypr::FprintdError>& queue) {
        if (queue.empty()) { return {}; }
        qypr::FprintdError e = queue.front();
        queue.pop_front();
        return e;
    }
    void answer(Reply done, const qypr::FprintdError& e) {
        if (hold) {
            held.emplace_back(std::move(done), e);
            return;
        }
        loop.post([done = std::move(done), e] { done(e); });
    }

    qypr::EventLoop& loop;
    StatusHandler status;
    VanishedHandler vanished;
    std::vector<std::string> calls;
    std::deque<qypr::FprintdError> claimErrors;  // scripted failures, oldest first
    std::deque<qypr::FprintdError> startErrors;
    bool claimed = false;
    bool verifying = false;
    bool hold = false;
    std::vector<std::pair<Reply, qypr::FprintdError>> held;
};

qypr::FprintdError readerBusy() {
    return {.name = qypr::fprintd_error::kAlreadyInUse, .message = "Device was already claimed"};
}

bool pumpUntil(qypr::EventLoop& loop, const std::function<bool()>& cond, int ms = 2000) {
    if (cond()) { return true; }
    int const poll = loop.addTimer(1, true, [&] {
        if (cond()) { loop.quit(); }
    });
    int const deadline = loop.addTimer(ms, false, [&] { loop.quit(); });
    loop.run();
    loop.removeTimer(poll);
    loop.removeTimer(deadline);
    return cond();
}

void pumpFor(qypr::EventLoop& loop, int ms) {
    pumpUntil(loop, [] { return false; }, ms);
}

struct FpHost : qypr::RenderHost {
    int unlocks = 0;
    void invalidate() override {}
    void requestUnlock() override { ++unlocks; }
};

// Rig shared by the tests: a lock screen whose finger side talks to `fprintd`.
struct FpRig {
    qypr::EventLoop loop;
    FpHost host;
    qypr::PamAuthenticator pam{loop};
    FakeFprintd fprintd{loop};
    qypr::FingerprintAuthenticator fp{loop, fprintd};
    qypr::SystemActions power{loop};
    qypr::LockScreen screen{loop, host, pam, power, &fp};

    FpRig() { fp.retryBackoff_ = std::chrono::milliseconds(1); }

    bool scanning() {
        return pumpUntil(loop, [&] { return fprintd.verifying; });
    }
    bool unlockedBy(const std::string& password) {
        mock_pam_reset();
        mock_pam_set_expected_password(password.c_str());
        screen.password().clear();
        screen.password().append(password);
        screen.submitPassword();
        return pumpUntil(loop, [&] { return host.unlocks == 1; });
    }
    // `n` wrong fingers in a row. Each is followed by the next scan, or — once
    // the miss budget is spent — by the authenticator stopping.
    void miss(int n) {
        for (int i = 0; i < n; ++i) {
            EXPECT_TRUE(scanning());
            int const starts = fprintd.count("VerifyStart");
            fprintd.emit("verify-no-match");
            EXPECT_TRUE(pumpUntil(
                loop, [&] { return fprintd.count("VerifyStart") > starts || !fp.running(); }));
        }
    }
    bool lockedOutAndReleased() {
        return pumpUntil(loop, [&] {
            return !fprintd.claimed && !fp.inflight_ &&
                   screen.controller_.fingerprint_ == qypr::FingerprintState::Disabled;
        });
    }
};

}  // namespace

// A touch unlocks without any password; the reader is then handed back: the
// scan stopped first, then the claim released (fprintd requires that order).
TEST(FingerprintSuccessUnlocksWithoutPassword) {
    FpRig rig;
    rig.screen.startFingerprint();
    EXPECT_TRUE(rig.scanning());
    pumpFor(rig.loop, 5);  // the "Place your finger" prompt is posted
    EXPECT_TRUE(rig.screen.controller_.fingerprint_ == qypr::FingerprintState::Prompting);

    rig.fprintd.emit("verify-match");
    EXPECT_TRUE(pumpUntil(rig.loop, [&] { return rig.host.unlocks == 1 && !rig.fprintd.claimed; }));
    EXPECT_FALSE(rig.fp.running());
    EXPECT_TRUE(rig.screen.unlocking());
    EXPECT_EQ(rig.fprintd.sequence(), std::string("Claim,VerifyStart,VerifyStop,Release"));
}

// No reader: Unavailable at once (no retry loop), fingerprint UI off, and the
// password still unlocks exactly once.
TEST(FingerprintUnavailableStaysPasswordOnly) {
    FpRig rig;
    rig.fprintd.claimErrors.push_back(
        {.name = qypr::fprintd_error::kNoSuchDevice, .message = "No devices available"});
    rig.screen.startFingerprint();
    EXPECT_TRUE(pumpUntil(rig.loop, [&] { return !rig.fp.running(); }));
    pumpFor(rig.loop, 20);
    EXPECT_EQ(rig.fprintd.count("Claim"), 1);
    EXPECT_FALSE(rig.screen.controller_.fingerprintActive());
    EXPECT_EQ(rig.host.unlocks, 0);

    EXPECT_TRUE(rig.unlockedBy("open-sesame"));
    EXPECT_TRUE(rig.screen.unlocking());
}

// Nothing enrolled: fprintd refuses VerifyStart. Give up, and do not keep the
// reader claimed (sudo/polkit fingerprint prompts need it).
TEST(FingerprintWithoutEnrolledPrintsReleasesReader) {
    FpRig rig;
    rig.fprintd.startErrors.push_back(
        {.name = qypr::fprintd_error::kNoEnrolledPrints, .message = "No enrolled prints"});
    rig.screen.startFingerprint();
    EXPECT_TRUE(pumpUntil(rig.loop, [&] { return !rig.fp.running() && !rig.fprintd.claimed; }));
    EXPECT_EQ(rig.fprintd.sequence(), std::string("Claim,VerifyStart,Release"));
    pumpFor(rig.loop, 5);
    EXPECT_FALSE(rig.screen.controller_.fingerprintActive());
}

// A wrong finger shows the error state, then a new scan starts on the same
// claim, and a later touch unlocks.
TEST(FingerprintMissShowsErrorThenRetries) {
    FpRig rig;
    rig.screen.startFingerprint();
    EXPECT_TRUE(rig.scanning());
    rig.fprintd.emit("verify-no-match");
    EXPECT_TRUE(pumpUntil(rig.loop, [&] { return rig.screen.controller_.fingerprintError_; }));
    EXPECT_TRUE(pumpUntil(
        rig.loop, [&] { return rig.fprintd.count("VerifyStart") == 2 && rig.fprintd.verifying; }));
    EXPECT_EQ(rig.fprintd.count("Claim"), 1);

    rig.fprintd.emit("verify-match");
    EXPECT_TRUE(pumpUntil(rig.loop, [&] { return rig.host.unlocks == 1; }));
}

// Up to the limit, a wrong finger only costs a retry: the right one still
// unlocks on the last allowed scan.
TEST(FingerprintMatchWithinMissBudgetUnlocks) {
    FpRig rig;
    rig.screen.startFingerprint();
    rig.miss(qypr::FingerprintAuthenticator::kMaxMisses - 1);
    EXPECT_TRUE(rig.fp.running());
    EXPECT_TRUE(rig.scanning());
    rig.fprintd.emit("verify-match");
    EXPECT_TRUE(pumpUntil(rig.loop, [&] { return rig.host.unlocks == 1; }));
}

// The kMaxMisses-th wrong finger locks fingerprint out: the reader is handed
// back, the status line says why, later touches are ignored, and only the
// password unlocks.
TEST(FingerprintLocksOutAfterTooManyMisses) {
    FpRig rig;
    rig.screen.startFingerprint();
    rig.miss(qypr::FingerprintAuthenticator::kMaxMisses);
    EXPECT_TRUE(rig.lockedOutAndReleased());
    EXPECT_TRUE(rig.fp.lockedOut());
    EXPECT_FALSE(rig.fp.running());
    EXPECT_EQ(rig.fprintd.count("VerifyStart"), qypr::FingerprintAuthenticator::kMaxMisses);
    EXPECT_TRUE(rig.screen.controller_.hasError_);
    EXPECT_EQ(rig.screen.controller_.statusMessage_,
              std::string("Too many fingerprint attempts. Enter your password."));

    rig.fprintd.emit("verify-match");  // not a scan of ours any more
    pumpFor(rig.loop, 20);
    EXPECT_EQ(rig.host.unlocks, 0);
    EXPECT_TRUE(rig.unlockedBy("open-sesame"));
}

// Neither a suspend/resume nor a stop/start buys fresh attempts, and once
// locked out the reader is never claimed again — not by the restart the App's
// resume handler asks for, nor by a direct start().
TEST(FingerprintLockoutSurvivesSuspendAndRestart) {
    FpRig rig;
    rig.screen.startFingerprint();
    rig.miss(2);

    bool ready = false;
    rig.fp.suspend([&] { ready = true; });
    EXPECT_TRUE(pumpUntil(rig.loop, [&] { return ready; }));
    rig.fp.resume();
    rig.miss(1);

    rig.screen.stopFingerprint();
    EXPECT_TRUE(pumpUntil(rig.loop, [&] { return !rig.fprintd.claimed && !rig.fp.inflight_; }));
    rig.screen.startFingerprint();
    rig.miss(qypr::FingerprintAuthenticator::kMaxMisses - 3);
    EXPECT_TRUE(rig.lockedOutAndReleased());
    EXPECT_TRUE(rig.fp.lockedOut());

    int const claims = rig.fprintd.count("Claim");
    ready = false;
    rig.fp.suspend([&] { ready = true; });
    EXPECT_TRUE(pumpUntil(rig.loop, [&] { return ready; }));
    rig.fp.resume();
    rig.screen.startFingerprint();  // what App's awake handler does
    bool refused = false;
    rig.fp.stop();
    rig.fp.start([&](qypr::FingerprintAuthenticator::Result result, const std::string&) {
        refused = result == qypr::FingerprintAuthenticator::Result::LockedOut;
    });
    EXPECT_TRUE(pumpUntil(rig.loop, [&] { return refused; }));
    pumpFor(rig.loop, 20);
    EXPECT_EQ(rig.fprintd.count("Claim"), claims);
    EXPECT_FALSE(rig.fp.running());
    EXPECT_TRUE(rig.unlockedBy("open-sesame"));
}

// "Try again" statuses keep the same scan running (done = false).
TEST(FingerprintRetryPromptKeepsScanning) {
    FpRig rig;
    rig.screen.startFingerprint();
    EXPECT_TRUE(rig.scanning());
    rig.fprintd.emit("verify-finger-not-centered", false);
    EXPECT_TRUE(pumpUntil(rig.loop, [&] { return rig.screen.controller_.fingerprintError_; }));
    EXPECT_EQ(rig.fprintd.count("VerifyStop"), 0);
    rig.fprintd.emit("verify-match");
    EXPECT_TRUE(pumpUntil(rig.loop, [&] { return rig.host.unlocks == 1; }));
}

// The race policy: a correct password unlocks exactly once, the scan is
// stopped and the reader released, and a late finger result changes nothing.
TEST(PasswordSuccessStopsFingerprintRace) {
    FpRig rig;
    rig.screen.startFingerprint();
    EXPECT_TRUE(rig.scanning());
    EXPECT_TRUE(rig.unlockedBy("open-sesame"));
    EXPECT_TRUE(pumpUntil(rig.loop, [&] { return !rig.fprintd.claimed; }));
    EXPECT_EQ(rig.fprintd.sequence(), std::string("Claim,VerifyStart,VerifyStop,Release"));

    rig.fprintd.emit("verify-match");
    pumpFor(rig.loop, 20);
    EXPECT_EQ(rig.host.unlocks, 1);
}

// The password must be checked on a stack without pam_fprintd, or a typed
// password waits behind a fingerprint scan. The dedicated qypr-lock service is
// preferred when installed; otherwise "login" — never a missing service, which
// PAM would resolve to the deny-all "other" stack.
TEST(PasswordServicePrefersDedicatedStack) {
    std::string dirTemplate = "/tmp/qypr-pam-d-XXXXXX";
    char* const dir = ::mkdtemp(dirTemplate.data());
    EXPECT_TRUE(dir != nullptr);
    if (dir == nullptr) { return; }
    const std::filesystem::path pamDir(dir);

    EXPECT_EQ(std::string(qypr::PamAuthenticator::passwordService(pamDir.string())),
              std::string("login"));

    // A directory of that name is not a PAM service file.
    std::filesystem::create_directory(pamDir / "qypr-lock");
    EXPECT_EQ(std::string(qypr::PamAuthenticator::passwordService(pamDir.string())),
              std::string("login"));
    std::filesystem::remove(pamDir / "qypr-lock");

    std::ofstream(pamDir / "qypr-lock") << "auth include system-auth\n";
    EXPECT_EQ(std::string(qypr::PamAuthenticator::passwordService(pamDir.string())),
              std::string("qypr-lock"));

    // The chosen service is the one pam_start() sees.
    qypr::EventLoop loop;
    qypr::PamAuthenticator pam(
        loop, std::string(qypr::PamAuthenticator::passwordService(pamDir.string())));
    mock_pam_reset();
    mock_pam_set_expected_password("open-sesame");
    qypr::SecureBuffer pw;
    pw.append("open-sesame");
    auto result = qypr::PamAuthenticator::Result::Error;
    EXPECT_TRUE(pam.authenticate(
        std::move(pw), [&](qypr::PamAuthenticator::Result r, const std::string&) { result = r; }));
    while (pam.busy()) {}
    loop.dispatchPosted();
    EXPECT_EQ(std::string(mock_pam_last_service()), std::string("qypr-lock"));
    EXPECT_TRUE(result == qypr::PamAuthenticator::Result::Success);

    std::filesystem::remove_all(pamDir);
}

// A reader that never becomes reachable (claimed by another client the whole
// time) must not be retried forever: back off, then report Unavailable after
// kMaxSilentFailures attempts in a row — fingerprint UI off, password works.
TEST(FingerprintSilentFailuresGiveUp) {
    FpRig rig;
    for (int i = 0; i < qypr::FingerprintAuthenticator::kMaxSilentFailures; ++i) {
        rig.fprintd.claimErrors.push_back(readerBusy());
    }
    rig.screen.startFingerprint();
    EXPECT_TRUE(rig.screen.controller_.fingerprintActive());
    EXPECT_TRUE(pumpUntil(rig.loop, [&] { return !rig.fp.running(); }));
    pumpFor(rig.loop, 20);
    EXPECT_EQ(rig.fprintd.count("Claim"), qypr::FingerprintAuthenticator::kMaxSilentFailures);
    EXPECT_FALSE(rig.screen.controller_.fingerprintActive());
    EXPECT_EQ(rig.host.unlocks, 0);

    EXPECT_TRUE(rig.unlockedBy("open-sesame"));
}

// A reader briefly held by another client (sudo's pam_fprintd, say) is picked
// up once it is free, as long as that happens inside the give-up window.
TEST(FingerprintSurvivesBrieflyBusyReader) {
    FpRig rig;
    int const busy = qypr::FingerprintAuthenticator::kMaxSilentFailures - 1;
    for (int i = 0; i < busy; ++i) { rig.fprintd.claimErrors.push_back(readerBusy()); }
    rig.screen.startFingerprint();
    EXPECT_TRUE(rig.scanning());
    EXPECT_EQ(rig.fprintd.count("Claim"), busy + 1);
    rig.fprintd.emit("verify-match");
    EXPECT_TRUE(pumpUntil(rig.loop, [&] { return rig.host.unlocks == 1; }));
}

// Suspend (SleepMonitor -> suspend()): the live scan is stopped before the
// machine sleeps and `ready` fires only once fprintd has acknowledged; the
// claim is kept, so resume() just starts a new scan. fprintd's own report that
// it ended the scan for the suspend is not a failure.
TEST(FingerprintPausesForSuspendAndResumes) {
    FpRig rig;
    rig.screen.startFingerprint();
    EXPECT_TRUE(rig.scanning());

    // Real ordering (journal, 2026-11-11): fprintd reports the scan's end
    // while our VerifyStop is still in flight. That report is not a failure.
    rig.fprintd.hold = true;
    bool ready = false;
    rig.fp.suspend([&] { ready = true; });
    rig.fprintd.emit("verify-unknown-error");  // "Cannot run while suspended"
    pumpFor(rig.loop, 10);
    EXPECT_FALSE(ready);  // still waiting for VerifyStop's reply
    rig.fprintd.hold = false;
    rig.fprintd.flush();
    EXPECT_TRUE(pumpUntil(rig.loop, [&] { return ready; }));
    EXPECT_FALSE(rig.fprintd.verifying);
    EXPECT_TRUE(rig.fprintd.claimed);
    EXPECT_EQ(rig.fprintd.count("Release"), 0);
    EXPECT_EQ(rig.fp.silentFailures_, 0);

    pumpFor(rig.loop, 20);
    EXPECT_TRUE(rig.fp.running());
    EXPECT_EQ(rig.fprintd.count("VerifyStart"), 1);  // nothing starts while asleep

    rig.fp.resume();
    EXPECT_TRUE(rig.scanning());
    EXPECT_EQ(rig.fprintd.sequence(), std::string("Claim,VerifyStart,VerifyStop,VerifyStart"));
    rig.fprintd.emit("verify-match");
    EXPECT_TRUE(pumpUntil(rig.loop, [&] { return rig.host.unlocks == 1; }));
}

// A suspend that lands while a call is in flight waits for its reply, and a
// claim granted meanwhile does not turn into a scan until resume.
TEST(FingerprintSuspendWaitsForCallInFlight) {
    FpRig rig;
    rig.fprintd.hold = true;
    rig.screen.startFingerprint();
    pumpFor(rig.loop, 5);
    EXPECT_EQ(rig.fprintd.count("Claim"), 1);

    bool ready = false;
    rig.fp.suspend([&] { ready = true; });
    pumpFor(rig.loop, 10);
    EXPECT_FALSE(ready);

    rig.fprintd.hold = false;
    rig.fprintd.flush();
    EXPECT_TRUE(pumpUntil(rig.loop, [&] { return ready; }));
    EXPECT_TRUE(rig.fprintd.claimed);
    EXPECT_EQ(rig.fprintd.count("VerifyStart"), 0);

    rig.fp.resume();
    EXPECT_TRUE(rig.scanning());
}

// Resume restores the full retry budget: failures from before the suspend do
// not count against the reader after it.
TEST(FingerprintResumeRestoresRetryBudget) {
    FpRig rig;
    int const before = 3;
    for (int i = 0; i < before; ++i) { rig.fprintd.claimErrors.push_back(readerBusy()); }
    rig.screen.startFingerprint();
    EXPECT_TRUE(pumpUntil(
        rig.loop, [&] { return rig.fprintd.count("Claim") == before && !rig.fp.inflight_; }));
    bool ready = false;
    rig.fp.suspend([&] { ready = true; });
    EXPECT_TRUE(pumpUntil(rig.loop, [&] { return ready; }));

    int const after = qypr::FingerprintAuthenticator::kMaxSilentFailures - 1;
    for (int i = 0; i < after; ++i) { rig.fprintd.claimErrors.push_back(readerBusy()); }
    rig.fp.resume();
    EXPECT_TRUE(rig.scanning());
    EXPECT_TRUE(rig.fp.running());
    EXPECT_EQ(rig.fprintd.count("Claim"), before + after + 1);
}

// fprintd restarting drops our claim; the authenticator claims again and the
// next touch still unlocks.
TEST(FingerprintReclaimsAfterFprintdRestart) {
    FpRig rig;
    rig.screen.startFingerprint();
    EXPECT_TRUE(rig.scanning());
    rig.fprintd.claimed = false;
    rig.fprintd.verifying = false;
    rig.fprintd.vanished();
    EXPECT_TRUE(pumpUntil(rig.loop, [&] { return rig.fprintd.count("Claim") == 2; }));
    EXPECT_TRUE(rig.scanning());
    rig.fprintd.emit("verify-match");
    EXPECT_TRUE(pumpUntil(rig.loop, [&] { return rig.host.unlocks == 1; }));
}

// stop() while the claim is still in flight: once it is granted the reader is
// released straight away, and no scan is ever started.
TEST(FingerprintStopDuringClaimReleasesReader) {
    FpRig rig;
    rig.fprintd.hold = true;
    rig.screen.startFingerprint();
    pumpFor(rig.loop, 5);
    rig.screen.stopFingerprint();
    rig.fprintd.hold = false;
    rig.fprintd.flush();
    EXPECT_TRUE(pumpUntil(rig.loop, [&] { return rig.fprintd.count("Release") == 1; }));
    pumpFor(rig.loop, 10);
    EXPECT_FALSE(rig.fprintd.claimed);
    EXPECT_EQ(rig.fprintd.count("VerifyStart"), 0);
    EXPECT_EQ(rig.host.unlocks, 0);
}
