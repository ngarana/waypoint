// PowerManager.hpp - System power actions via systemctl (port of PowerManager.qml).
//
// Fire-and-forget: each action spawns `systemctl <verb>` (or `loginctl`) and
// never waits for it — the action either takes over the machine or fails
// silently.
//
// Two things are deliberate here (QL-5/QL-6 of docs/LOCK_SECURITY_REVIEW.md):
//
//   * The children are spawned with posix_spawn(), never fork()+execlp().
//     qypr-lock is multithreaded (the PAM worker, libmpv), and only the calling
//     thread survives a fork() — any lock another thread held stays held.
//   * This class does NOT set a process-wide signal disposition. The old
//     `signal(SIGCHLD, SIG_IGN)` leaked into every child qypr ever spawned (an
//     ignored disposition survives execve, so a helper's own waitpid() returned
//     ECHILD) and raced pam_unix, which flips SIGCHLD from the PAM worker thread.
//     Instead the child's defaults are set per spawn (core/Process.hpp) and the
//     exited child is reaped through the event loop via its pidfd.
//
// Spawning here is deliberate and within the footprint principle (STATUS_BAR.md
// decision D1): these are user-initiated, one-shot actions with a bounded
// lifetime — never a way to *read* state, and never on a timer.

#pragma once

#include <string>

namespace qypr {

class EventLoop;

class PowerManager {
public:
    // The loop reaps spawned children (pidfd + EventLoop::addFd) — it is not
    // optional: without it a child that exits would stay a zombie until qypr
    // exits.
    explicit PowerManager(EventLoop& loop);

    PowerManager(const PowerManager&) = delete;
    PowerManager& operator=(const PowerManager&) = delete;

    void suspend() { run("suspend"); }
    void reboot() { run("reboot"); }
    void shutdown() { run("poweroff"); }
    void hibernate() { run("hibernate"); }

    // Lock the session via logind, which asks whatever locker the session has
    // configured to run (the standard, daemon-agnostic path — qypr never
    // launches itself directly).
    void lock();

private:
    void run(const char* verb);  // systemctl <verb>

    EventLoop& loop_;
    // Absolute paths resolved once at startup: the child is spawned by absolute
    // path, so nothing depends on a PATH lookup between fork and exec.
    const std::string systemctl_;
    const std::string loginctl_;
};

}  // namespace qypr
