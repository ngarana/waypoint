#pragma once

#include <functional>
#include <mutex>
#include <optional>
#include <signal.h>
#include <string>
#include <sys/types.h>
#include <thread>
#include <vector>

namespace qypr {
class EventLoop;
}

namespace waylaunch {

struct ProcessResult {
    int exit_code = -1;
    std::string stdout;
    std::string stderr;
};

class Subprocess {
  public:
    // Synchronous captured run (qypr::runCapture): argv[0] resolved once in
    // the parent, pipes pumped through one poll loop. Never use for
    // self-daemonizing helpers (wl-copy): their inherited pipes never hit
    // EOF — spawn_reaped() those instead (see clipboard.cpp).
    static ProcessResult run(const std::vector<std::string>& argv,
                             const std::string& stdin_data = "");
    // PATH executability probe (qypr::commandExists): optional helpers
    // (pdftotext, wl-copy, systemctl) and init-system detection.
    static bool command_exists(const std::string& command);
    // Daemon launch with no reactor (qypr::spawnDaemon): double-fork +
    // setsid, reparented to init. Fire-and-forget for loop-less contexts
    // (Clipboard without a loop); prefer spawn_reaped() wherever a loop
    // exists — a tracked child is always easier to debug.
    static void spawn_detached(const std::vector<std::string>& argv);
    // Shared I3 spawn (libwl-common) with a reactor: argv[0] resolved to an
    // absolute path, own session, pidfd-reaped through `loop` (no fork, no
    // zombie). The loop is a reference, not a pointer: reaping needs a
    // reactor, and the choice between this and spawn_detached must be
    // explicit at the call site — never a silent null fallback.
    static void spawn_reaped(qypr::EventLoop& loop, const std::vector<std::string>& argv);
    // Ordinary fire-and-forget user launch (open app/URL/file) with an
    // explicit lifecycle policy, shared by every provider: pidfd-reaped
    // through `loop` when one is set, daemon-launched (double-fork + setsid,
    // reparented to init, no zombie) when it is null. The two outcomes are
    // this function's documented contract — never a silent fallback inside
    // spawn_reaped. Prefer spawn_reaped() directly when a loop is guaranteed.
    static void launch(qypr::EventLoop* loop, const std::vector<std::string>& argv);
    // Launch a supervised child (single fork + setsid) and return its pid for
    // waitpid/SIGCHLD reaping. Unlike spawn_detached the child stays ours, so
    // the dropdown session supervisor can respawn it. Returns -1 on failure.
    // This is the sanctioned supervisor-local exception to the shared spawn
    // contract (ARCHITECTURE_REVIEW finding 3): the vfork-clone avoids
    // pthread_atfork wedges that posix_spawn/fork hit under worker threads.
    static pid_t spawn_tracked(const std::vector<std::string>& argv);
};

} // namespace waylaunch
