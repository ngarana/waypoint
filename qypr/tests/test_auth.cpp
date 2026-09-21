// test_auth.cpp - Auth, power actions, media, secure buffers, spawn.
// Split verbatim from tests/unit_tests.cpp; bodies unchanged.
#include "test_framework.hpp"

#include <array>
#include <csignal>

TEST(PamAuthenticator) {
    qypr::EventLoop loop;
    qypr::PamAuthenticator auth(loop, "qypr-lock-test");

    mock_pam_reset();
    mock_pam_set_expected_password("s3cret");

    qypr::PamAuthenticator::Result res = qypr::PamAuthenticator::Result::Failure;
    std::string lastMsg;
    // The secret is handed over as a SecureBuffer, moved into the worker (QL-3):
    // authenticate() has no std::string overload, so no unwiped copy can be made.
    auto secret = [](const std::string& s) {
        qypr::SecureBuffer b;
        b.append(s);
        return b;
    };
    auto runAttempt = [&](const std::string& password) {
        bool doneCalled = false;
        res = qypr::PamAuthenticator::Result::Failure;
        auth.authenticate(secret(password),
                          [&](qypr::PamAuthenticator::Result r, const std::string& msg) {
                              res = r;
                              lastMsg = msg;
                              doneCalled = true;
                              loop.quit();
                          });
        loop.run();
        EXPECT_TRUE(doneCalled);
    };

    // ---- Correct password authenticates the *real* current user ----------
    // This is the regression guard for the "unknown user" bug: a prior change
    // hardcoded the PAM username to "test-user", so every real unlock failed.
    // The authenticator must resolve and pass the actual login user.
    runAttempt("s3cret");
    EXPECT_EQ(static_cast<int>(res), static_cast<int>(qypr::PamAuthenticator::Result::Success));

    // Single-threaded test binary: no concurrent getpwuid callers exist.
    // NOLINTNEXTLINE(concurrency-mt-unsafe)
    const passwd* pw = getpwuid(getuid());
    EXPECT_TRUE(pw != nullptr);
    if (pw != nullptr) { EXPECT_EQ(std::string(mock_pam_last_user()), std::string(pw->pw_name)); }

    // ---- Wrong password -> Failure (real PAM_AUTH_ERR mapping) -----------
    runAttempt("wrong");
    EXPECT_EQ(static_cast<int>(res), static_cast<int>(qypr::PamAuthenticator::Result::Failure));

    // ---- PAM subsystem error -> Error (not a plain auth failure) ---------
    // PAM_USER_UNKNOWN is neither SUCCESS nor AUTH_ERR, so it must surface as
    // Error rather than being conflated with a wrong password.
    mock_pam_force_rc(PAM_USER_UNKNOWN);
    runAttempt("s3cret");
    EXPECT_EQ(static_cast<int>(res), static_cast<int>(qypr::PamAuthenticator::Result::Error));
    mock_pam_force_rc(-1);

    // ---- Empty password is rejected before any PAM call ------------------
    bool const started = auth.authenticate(
        qypr::SecureBuffer{}, [&](qypr::PamAuthenticator::Result, const std::string&) {});
    EXPECT_FALSE(started);
}
TEST(SystemActions) {
    qypr::EventLoop loop;
    qypr::SystemActions pm(loop);
    pm.run("suspend");
    pm.run("reboot");
    // Should run instantly and safely in mock mode
}
TEST(MprisController) {
    qypr::MprisController mpris;
    EXPECT_TRUE(mpris.available());

    mpris.refresh();
    EXPECT_TRUE(mpris.active());
    EXPECT_TRUE(mpris.playing());
    EXPECT_EQ(mpris.title(), std::string("Mock Song"));
    EXPECT_EQ(mpris.artist(), std::string("Mock Artist"));
    EXPECT_EQ(mpris.album(), std::string("Mock Album"));
    EXPECT_EQ(mpris.sourceLabel(), std::string("Mock Player"));
    EXPECT_NEAR(mpris.volume(), 0.8, 0.01);
    EXPECT_NEAR(mpris.positionSeconds(), 60.0, 0.01);
    EXPECT_NEAR(mpris.durationSeconds(), 180.0, 0.01);
    EXPECT_TRUE(mpris.canGoNext());
    EXPECT_TRUE(mpris.canGoPrevious());
    EXPECT_TRUE(mpris.canTogglePlaying());
    EXPECT_TRUE(mpris.canSetVolume());

    mpris.togglePlaying();
    EXPECT_FALSE(mpris.playing());  // toggled to paused

    mpris.togglePlaying();
    EXPECT_TRUE(mpris.playing());  // toggled back to playing

    mpris.next();
    EXPECT_EQ(mpris.title(), std::string("Next Song"));

    mpris.previous();
    EXPECT_EQ(mpris.title(), std::string("Previous Song"));

    mpris.setVolume(0.4);
    EXPECT_NEAR(mpris.volume(), 0.4, 0.01);
}
TEST(SecureBufferMoveSemanticsAndSecret) {
    qypr::SecureBuffer b;
    b.append("s3cret");
    EXPECT_EQ(b.size(), size_t(6));
    EXPECT_EQ(std::string(b.cStr()), "s3cret");

    qypr::SecureBuffer moved = std::move(b);
    EXPECT_EQ(std::string(moved.cStr()), "s3cret");
    // The source must still be usable: LockScreen keeps receiving keystrokes
    // after submitPassword() moves the field into the authenticator.
    EXPECT_TRUE(b.empty());
    // Exercising the moved-from object is the point — SecureBuffer
    // guarantees a valid empty state.
    // NOLINTNEXTLINE(bugprone-use-after-move,clang-analyzer-cplusplus.Move,hicpp-invalid-access-moved)
    b.append("x");
    EXPECT_EQ(std::string(b.cStr()), "x");

    // Move-assign the same way and verify nothing leaks across.
    qypr::SecureBuffer other;
    other.append("old");
    other = std::move(moved);
    EXPECT_EQ(std::string(other.cStr()), "s3cret");
    // NOLINTNEXTLINE(bugprone-use-after-move,clang-analyzer-cplusplus.Move,hicpp-invalid-access-moved)
    EXPECT_TRUE(moved.empty());

    // append() must keep a NUL after the live prefix even after appends.
    other.append("1");
    EXPECT_EQ(other.size(), size_t(7));
    EXPECT_EQ(std::string(other.cStr()), "s3cret1");
    EXPECT_EQ(std::string_view(other.view()).size(), size_t(7));

    // popBack() removes whole UTF-8 code points and keeps the view consistent.
    other.clear();
    other.append("aé");  // 3 bytes: 'a' + 2-byte é
    EXPECT_EQ(other.size(), size_t(3));
    other.popBack();
    EXPECT_EQ(other.size(), size_t(1));
    EXPECT_EQ(std::string(other.cStr()), "a");
}

// QL-3: capacity is a hard ceiling. A paste beyond 512 bytes must be silently
// truncated, never overflow, and the live view must stay exactly the stored
// prefix (the extra byte is reserved for the NUL that keeps cStr() valid).
TEST(SecureBufferCapacityCeiling) {
    qypr::SecureBuffer b;  // 512-byte capacity, 511 usable for the secret
    std::string const big(1024, 'A');
    b.append(big);
    EXPECT_EQ(b.size(), size_t(511));
    EXPECT_EQ(b.view().size(), size_t(511));
    EXPECT_EQ(std::string_view(b.view()).substr(0, 8), "AAAAAAAA");
    // A subsequent append must stay a no-op, not write out of bounds.
    b.append("more");
    EXPECT_EQ(b.size(), size_t(511));
    EXPECT_EQ(b.capacity(), size_t(512));
}

// QL-3: whatever path the secret takes, it must end up empty. submitPassword()
// moves it into the authenticator; a failed attempt clears it explicitly.
TEST(ProcessSpawnDetachedAndReap) {
    // Resolution: a standard tool resolves to an absolute path; a nonsense name
    // resolves to nothing.
    const std::string sh = qypr::resolveToolPath("sh");
    EXPECT_TRUE(sh.rfind('/', 0) == 0);  // starts with '/'
    EXPECT_TRUE(qypr::resolveToolPath("definitely-not-a-tool-xyz").empty());

    // spawnDetached: a real, short-lived child that this test never waits for.
    const pid_t detached = qypr::spawnDetached(sh, {sh, "-c", "exit 0"}, /*newSession=*/true);
    EXPECT_TRUE(detached > 0);

    // spawnReaped: same, but the exit is reaped via the pidfd watched by the
    // loop. Drive the loop briefly so the reap actually runs.
    qypr::EventLoop loop;
    const pid_t reaped = qypr::spawnReaped(loop, sh, {sh, "-c", "exit 0"});
    EXPECT_TRUE(reaped > 0);
    ::usleep(200000);  // let the child exit; the loop reaps on its next dispatch
    loop.dispatchPosted();

    // A missing binary must be reported, not silently swallowed.
    EXPECT_TRUE(qypr::spawnDetached("/nonexistent/binary-xyz", {}, false) < 0);
    EXPECT_TRUE(qypr::spawnReaped(loop, "/nonexistent/binary-xyz", {}) < 0);
}

TEST(ProcessSpawnEnvAndDevnull) {
    // env_add: the child observes caller-supplied variables (waylaunch's
    // WL_APP_ID/WL_CLASS/WL_TITLE hook contract). Observed through a file:
    // the child writes, this test polls for it.
    const std::string probe = "/tmp/qypr-spawn-env-" + std::to_string(::getpid()) + ".txt";
    ::unlink(probe.c_str());
    const std::string sh = qypr::resolveToolPath("sh");
    EXPECT_TRUE(!sh.empty());
    const pid_t envChild =
        qypr::spawnDetached(sh, {sh, "-c", std::string("echo $WL_SPAWN_PROBE_TEST > ") + probe},
                            false, {"WL_SPAWN_PROBE_TEST=spawn-probe-ok"});
    EXPECT_TRUE(envChild > 0);
    std::string seen;
    for (int i = 0; i < 40 && seen.empty(); ++i) {
        ::usleep(50000);
        std::ifstream f(probe);
        if (f) { std::getline(f, seen); }
    }
    // Trim the trailing newline echo appends.
    while (!seen.empty() && (seen.back() == '\n' || seen.back() == '\r')) { seen.pop_back(); }
    EXPECT_EQ(seen, std::string("spawn-probe-ok"));
    ::unlink(probe.c_str());

    // devnull_stdio: fd 0/1/2 of the child point at /dev/null. Observed via
    // /proc while a sleep-child is alive; reaped through a local loop.
    qypr::EventLoop reapLoop;
    const pid_t sleeper = qypr::spawnReaped(reapLoop, sh, {sh, "-c", "sleep 30"}, false, {}, true);
    EXPECT_TRUE(sleeper > 0);
    for (int fd = 0; fd <= 2; ++fd) {
        std::array<char, 64> link{};
        const std::string path = "/proc/" + std::to_string(sleeper) + "/fd/" + std::to_string(fd);
        const ssize_t n = ::readlink(path.c_str(), link.data(), link.size() - 1);
        EXPECT_TRUE(n > 0);
        EXPECT_EQ(std::string(link.data(), static_cast<size_t>(n)), std::string("/dev/null"));
    }
    ::kill(sleeper, SIGKILL);
    ::usleep(200000);
    reapLoop.dispatchPosted();
}

// QL-1/QL-2/QL-4: on the lock screen an indicator is inert by default — it can
// be visible but must not react to clicks. A new indicator type is deny-by-
// default; only explicit opt-in (Volume/Brightness/Media) is interactive.
TEST(BluetoothAgentDisabledWhileLocked) {
    qypr::EventLoop loop;
    qypr::SystemBus bus(loop);
    qypr::BluetoothBackend bt(bus);
    bt.setPairingAgentEnabled(false);
    EXPECT_FALSE(bt.pairingAgentEnabled());
    // Re-enabling flips the flag; the real DBus start happens only when a
    // backend is wired (the mock here never reaches the bus).
    bt.setPairingAgentEnabled(true);
    EXPECT_TRUE(bt.pairingAgentEnabled());
    bt.setPairingAgentEnabled(false);
    EXPECT_FALSE(bt.pairingAgentEnabled());
}

// -----------------------------------------------------------------------------
