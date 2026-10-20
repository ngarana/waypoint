// test_lock.cpp - Lock-screen input, unlock gating, secret wiping.
// Split verbatim from tests/unit_tests.cpp; bodies unchanged.
#include "test_framework.hpp"

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
// chronometer lockup, the facts line and the auth card.
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
        EXPECT_TRUE(renderer.facts_.bounds.h > 0);
        EXPECT_TRUE(renderer.auth_.bounds.w > 0);
        // The clock column stays inside the output.
        EXPECT_TRUE(renderer.clock_.bounds.x + renderer.clock_.bounds.w <= width);
        EXPECT_TRUE(renderer.clock_.bounds.y >= 0);
        EXPECT_TRUE(renderer.clock_.bounds.y + renderer.clock_.bounds.h <= height);

        cairo_destroy(cr);
        cairo_surface_destroy(surf);
    }
}

// The facts line reports the session's real state, not decoration.
TEST(LockFactsRowReportsSessionState) {
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

    const auto& items = screen.renderer_.facts_.items;
    EXPECT_TRUE(items.size() >= 2);
    EXPECT_EQ(items.at(0).label, std::string("FINGERPRINT"));
    EXPECT_EQ(items.at(1).label, std::string("PAM"));
    // The PAM stack is whatever the authenticator guards the session with.
    EXPECT_EQ(items.at(1).value, screen.controller_.pamService());
    EXPECT_TRUE(!items.at(1).value.empty());

    cairo_destroy(cr);
    cairo_surface_destroy(surf);
}

TEST(LockLayoutGeometryMultipleSizes) {
    const qypr::LockLayout layout;
    const qypr::Size clock{.w = 300.0, .h = 100.0};
    const qypr::Size facts{.w = 200.0, .h = 14.0};
    const qypr::Size auth{.w = 420.0, .h = 210.0};
    const qypr::Size media{.w = 420.0, .h = 260.0};

    for (const auto [width, height] :
         {std::pair{800, 600}, std::pair{1920, 1080}, std::pair{3440, 1440}}) {
        const auto result = layout.compute(width, height, clock, facts, auth, media);
        const auto row = layout.powerRowRect(width, height);
        const auto anchor = layout.powerAnchorRect(width, height);

        EXPECT_NEAR(row.x + row.w, width - layout.theme().spacing.xlarge, 0.001);
        EXPECT_NEAR(row.y + row.h, height - layout.theme().spacing.xlarge, 0.001);
        EXPECT_TRUE(anchor.y > row.y);

        // The auth card owns the right column.
        EXPECT_NEAR(result.auth.w, layout.authColumnWidth(width), 0.001);
        EXPECT_TRUE(result.auth.x + result.auth.w <= width - layout.theme().spacing.xlarge);

        // The media card sits directly above the auth card.
        EXPECT_NEAR(result.media.y + result.media.h + layout.theme().spacing.medium, result.auth.y,
                    0.001);
        EXPECT_NEAR(result.media.x, result.auth.x, 0.001);

        // Both columns share one baseline, above the notification cards.
        const double baseline = result.facts.y + result.facts.h;
        EXPECT_NEAR(baseline, result.auth.y + result.auth.h, 0.001);
        EXPECT_TRUE(baseline <=
                    height - layout.theme().spacing.xlarge - layout.notificationZoneHeight(height));

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

// Without a media panel the right column holds only the auth card.
TEST(LockLayoutWithoutMediaKeepsAuthColumn) {
    const qypr::LockLayout layout;
    const qypr::Size clock{.w = 300.0, .h = 100.0};
    const qypr::Size facts{.w = 200.0, .h = 14.0};
    const qypr::Size auth{.w = 420.0, .h = 210.0};

    const auto result = layout.compute(1920, 1080, clock, facts, auth, qypr::Size{0, 0});
    EXPECT_NEAR(result.media.h, 0.0, 0.001);
    EXPECT_NEAR(result.auth.w, layout.authColumnWidth(1920), 0.001);
    // The auth card shares the baseline with the facts line.
    EXPECT_NEAR(result.auth.y + result.auth.h, result.facts.y + result.facts.h, 0.001);
}

// The oversized clock scales with the output but stays inside sane bounds.
TEST(LockClockFontSizeIsResponsive) {
    const qypr::LockLayout layout;
    EXPECT_TRUE(layout.clockFontSize(1920, 1080) > layout.clockFontSize(1280, 720));
    EXPECT_TRUE(layout.clockFontSize(800, 600) >= 56.0);
    EXPECT_TRUE(layout.clockFontSize(7680, 4320) <= 208.0);
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

// Fingerprint unlock (docs/FINGERPRINT.md): the background worker on the
// dedicated qypr-fingerprint service unlocks without any password typed.
// Mirrors LockScreenUnlockGating's spy-host shape; the Done + Info callbacks
// arrive via loop.post, so the test pumps dispatchPosted() while waiting.
TEST(FingerprintSuccessUnlocksWithoutPassword) {
    qypr::EventLoop loop;
    struct SpyHost : qypr::RenderHost {
        int unlocks = 0;
        void invalidate() override {}
        void requestUnlock() override { ++unlocks; }
    } host;
    qypr::PamAuthenticator pam(loop);
    qypr::FingerprintAuthenticator fp(loop);
    qypr::SystemActions power(loop);
    qypr::LockScreen screen(loop, host, pam, power, &fp);

    mock_pam_reset();
    mock_pam_set_expected_password("open-sesame");

    screen.startFingerprint();
    for (int i = 0; i < 1000 && fp.running(); ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
        loop.dispatchPosted();
    }
    loop.dispatchPosted();

    EXPECT_FALSE(fp.running());
    EXPECT_EQ(host.unlocks, 1);
    EXPECT_TRUE(screen.unlocking());
    EXPECT_EQ(std::string(mock_pam_last_service()), std::string("qypr-fingerprint"));
}

// A hard misconfiguration (missing service file, no device) reports
// Unavailable: no unlock, fingerprint UI disabled, password auth unaffected.
TEST(FingerprintUnavailableStaysPasswordOnly) {
    qypr::EventLoop loop;
    struct SpyHost : qypr::RenderHost {
        int unlocks = 0;
        void invalidate() override {}
        void requestUnlock() override { ++unlocks; }
    } host;
    qypr::PamAuthenticator pam(loop);
    qypr::FingerprintAuthenticator fp(loop);
    qypr::SystemActions power(loop);
    qypr::LockScreen screen(loop, host, pam, power, &fp);

    mock_pam_reset();
    mock_pam_set_expected_password("open-sesame");
    mock_fp_force_rc(PAM_SERVICE_ERR);

    screen.startFingerprint();
    for (int i = 0; i < 1000 && fp.running(); ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
        loop.dispatchPosted();
    }
    loop.dispatchPosted();
    mock_fp_force_rc(-1);

    EXPECT_EQ(host.unlocks, 0);
    EXPECT_FALSE(screen.unlocking());

    // Password auth still unlocks exactly once afterwards.
    screen.password().clear();
    screen.password().append("open-sesame");
    screen.submitPassword();
    while (pam.busy()) {}
    loop.dispatchPosted();
    EXPECT_EQ(host.unlocks, 1);
    EXPECT_TRUE(screen.unlocking());
}

// Transient mismatches retry: each failed attempt still drives the SDDM-style
// info conversation ("Place your finger..."), which the controller surfaces as
// the Prompting state, before a later attempt succeeds and unlocks.
TEST(FingerprintRetryShowsPromptThenUnlocks) {
    qypr::EventLoop loop;
    struct SpyHost : qypr::RenderHost {
        int unlocks = 0;
        void invalidate() override {}
        void requestUnlock() override { ++unlocks; }
    } host;
    qypr::PamAuthenticator pam(loop);
    qypr::FingerprintAuthenticator fp(loop);
    qypr::SystemActions power(loop);
    qypr::LockScreen screen(loop, host, pam, power, &fp);

    mock_pam_reset();
    mock_fp_fail_n_times(2);

    screen.startFingerprint();
    bool sawPrompt = false;
    for (int i = 0; i < 2000 && host.unlocks == 0; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
        loop.dispatchPosted();
        if (screen.controller_.fingerprint_ == qypr::FingerprintState::Prompting) {
            sawPrompt = true;
        }
    }
    loop.dispatchPosted();

    EXPECT_TRUE(sawPrompt);
    EXPECT_EQ(host.unlocks, 1);
    EXPECT_TRUE(screen.unlocking());
}

// The race policy: a correct password unlocks exactly once while the finger
// worker is still retrying, and the loser's late success never double-unlocks.
TEST(PasswordSuccessStopsFingerprintRace) {
    qypr::EventLoop loop;
    struct SpyHost : qypr::RenderHost {
        int unlocks = 0;
        void invalidate() override {}
        void requestUnlock() override { ++unlocks; }
    } host;
    qypr::PamAuthenticator pam(loop);
    qypr::FingerprintAuthenticator fp(loop);
    qypr::SystemActions power(loop);
    qypr::LockScreen screen(loop, host, pam, power, &fp);

    mock_pam_reset();
    mock_pam_set_expected_password("open-sesame");
    mock_fp_fail_n_times(50);  // finger keeps missing while the password lands

    screen.startFingerprint();
    screen.password().clear();
    screen.password().append("open-sesame");
    screen.submitPassword();
    while (pam.busy()) {}
    for (int i = 0; i < 200 && host.unlocks == 0; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
        loop.dispatchPosted();
    }
    loop.dispatchPosted();

    EXPECT_EQ(host.unlocks, 1);

    // Let the finger worker finish in the background; unlocks must stay at 1.
    mock_fp_fail_n_times(0);
    for (int i = 0; i < 200; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
        loop.dispatchPosted();
    }
    EXPECT_EQ(host.unlocks, 1);
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

// Attempts that never reach the reader (no device, no enrolled finger, reader
// claimed elsewhere, or the shipped `sufficient` stack's flat PERM_DENIED) are
// silent. They must not spin forever: back off, then report Unavailable after
// kMaxSilentFailures in a row — fingerprint UI off, password still unlocks.
TEST(FingerprintSilentFailuresGiveUp) {
    qypr::EventLoop loop;
    struct SpyHost : qypr::RenderHost {
        int unlocks = 0;
        void invalidate() override {}
        void requestUnlock() override { ++unlocks; }
    } host;
    qypr::PamAuthenticator pam(loop);
    qypr::FingerprintAuthenticator fp(loop);
    fp.retryBackoff_ = std::chrono::milliseconds(1);
    qypr::SystemActions power(loop);
    qypr::LockScreen screen(loop, host, pam, power, &fp);

    mock_pam_reset();
    mock_pam_set_expected_password("open-sesame");
    mock_fp_force_rc(PAM_PERM_DENIED);

    screen.startFingerprint();
    EXPECT_TRUE(screen.controller_.fingerprintActive());
    for (int i = 0; i < 1000 && fp.running(); ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
        loop.dispatchPosted();
    }
    loop.dispatchPosted();
    mock_fp_force_rc(-1);

    EXPECT_FALSE(fp.running());
    EXPECT_EQ(mock_fp_attempts(), qypr::FingerprintAuthenticator::kMaxSilentFailures);
    EXPECT_FALSE(screen.controller_.fingerprintActive());
    EXPECT_EQ(host.unlocks, 0);

    screen.password().clear();
    screen.password().append("open-sesame");
    screen.submitPassword();
    while (pam.busy()) {}
    loop.dispatchPosted();
    EXPECT_EQ(host.unlocks, 1);
}

// Attempts that did reach the reader (it said "Place your finger...") are
// ordinary misses or timeouts — pam_fprintd reports a timeout as
// PAM_AUTHINFO_UNAVAIL, the same code as a missing reader. However many pile
// up while the user is away, the worker keeps waiting and a later touch
// unlocks.
TEST(FingerprintTimeoutsNeverGiveUp) {
    qypr::EventLoop loop;
    struct SpyHost : qypr::RenderHost {
        int unlocks = 0;
        void invalidate() override {}
        void requestUnlock() override { ++unlocks; }
    } host;
    qypr::PamAuthenticator pam(loop);
    qypr::FingerprintAuthenticator fp(loop);
    fp.retryBackoff_ = std::chrono::milliseconds(1);
    qypr::SystemActions power(loop);
    qypr::LockScreen screen(loop, host, pam, power, &fp);

    mock_pam_reset();
    const int timeouts = qypr::FingerprintAuthenticator::kMaxSilentFailures * 3;
    mock_fp_fail_n_times(timeouts);

    screen.startFingerprint();
    for (int i = 0; i < 2000 && host.unlocks == 0; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
        loop.dispatchPosted();
    }
    loop.dispatchPosted();

    EXPECT_EQ(host.unlocks, 1);
    EXPECT_EQ(mock_fp_attempts(), timeouts + 1);
}

// A reader briefly held by another client (sudo's pam_fprintd, say) fails
// silently until it is released; as long as that ends inside the give-up
// window, the worker picks the reader up and a touch unlocks.
TEST(FingerprintSurvivesBrieflyBusyReader) {
    qypr::EventLoop loop;
    struct SpyHost : qypr::RenderHost {
        int unlocks = 0;
        void invalidate() override {}
        void requestUnlock() override { ++unlocks; }
    } host;
    qypr::PamAuthenticator pam(loop);
    qypr::FingerprintAuthenticator fp(loop);
    fp.retryBackoff_ = std::chrono::milliseconds(1);
    qypr::SystemActions power(loop);
    qypr::LockScreen screen(loop, host, pam, power, &fp);

    mock_pam_reset();
    const int busy = qypr::FingerprintAuthenticator::kMaxSilentFailures - 1;
    mock_fp_silent_fail_n_times(busy);

    screen.startFingerprint();
    for (int i = 0; i < 2000 && host.unlocks == 0 && fp.running(); ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
        loop.dispatchPosted();
    }
    loop.dispatchPosted();

    EXPECT_EQ(host.unlocks, 1);
    EXPECT_EQ(mock_fp_attempts(), busy + 1);
}

// Without its PAM file the worker would run against the deny-all "other"
// stack (instant failure plus a pam_warn journal line per attempt), so App
// only starts it when the service file is installed.
TEST(FingerprintServiceInstalledChecksPamDir) {
    std::string dirTemplate = "/tmp/qypr-pam-d-XXXXXX";
    char* const dir = ::mkdtemp(dirTemplate.data());
    EXPECT_TRUE(dir != nullptr);
    if (dir == nullptr) { return; }
    const std::filesystem::path pamDir(dir);

    qypr::EventLoop loop;
    qypr::FingerprintAuthenticator fp(loop);
    EXPECT_FALSE(fp.serviceInstalled(pamDir.string()));
    std::ofstream(pamDir / "qypr-fingerprint") << "auth sufficient pam_fprintd.so\n";
    EXPECT_TRUE(fp.serviceInstalled(pamDir.string()));

    qypr::FingerprintAuthenticator custom(loop, "other-finger");
    EXPECT_FALSE(custom.serviceInstalled(pamDir.string()));

    std::filesystem::remove_all(pamDir);
}
