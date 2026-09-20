// Process.hpp - Spawning short-lived helper programs without fork().
//
// Why not fork() (QL-6 of docs/LOCK_SECURITY_REVIEW.md): qypr-lock and qypr-bar
// are multithreaded (the PAM worker, libmpv, libpulse). After fork() only the
// calling thread exists in the child, and any lock another thread held stays
// held forever; until exec, only async-signal-safe functions are legal there.
// posix_spawn() is the sanctioned replacement (glibc implements it with
// CLONE_VM|CLONE_VFORK, so no address space is copied and no lock is inherited).
//
// Why the SIGCHLD/SIGPIPE reset (QL-5): Linux preserves an *ignored* signal
// disposition across execve(), so a helper started from a process that ignores
// SIGCHLD inherits that and sees ECHILD from its own waitpid(). qypr has already
// been bitten by this (grimblast's "Clipboard error"); setting the disposition
// explicitly per child removes the whole class of problem and lets the parent
// keep a normal SIGCHLD (so nothing leaks into unrelated children).

#pragma once

#include <sys/types.h>

#include <string>
#include <vector>

namespace qypr {

class EventLoop;

// Absolute path of `name`, looked up on PATH first and in the usual bin/sbin
// directories after that. Empty when the program is not installed. Resolve once
// at startup so a spawn never depends on a PATH lookup happening in the child.
std::string resolveToolPath(const std::string& name);

// Spawn `path` (absolute) with `args` and the caller's environment plus
// `env_add` (`KEY=VALUE` entries appended in the parent, so no fork-time
// allocation or signal-handler constraints — e.g. waylaunch exports the
// activated window as WL_APP_ID/WL_CLASS/WL_TITLE). `args[0]` is
// the argv[0] the child sees. Returns the child's pid, or -1.
//
// The child always starts with SIGCHLD and SIGPIPE at SIG_DFL, whatever the
// parent's dispositions are. `newSession` gives the child its own session
// (setsid), so a command that outlives the panel is not tied to it.
// `devnull_stdio` rewires stdin/stdout/stderr to /dev/null (for hooks whose
// output must not pollute the parent's terminal or logs).
//
// NOTE: this deliberately does NOT double-fork. A detached child needs
// reaping — use spawnReaped() with the loop. Fire-and-forget without a loop
// leaks a zombie when the child exits before its parent.
pid_t spawnDetached(const std::string& path, const std::vector<std::string>& args,
                    bool newSession = false,
                    const std::vector<std::string>& env_add = {},
                    bool devnull_stdio = false);

// spawnDetached() plus reaping: the child's pidfd is watched through the event
// loop, so the loop is woken when the child exits and waitpid() reaps it there.
// Push-based — no polling, no zombie, no process-wide SIGCHLD disposition.
// Returns the child's pid, or -1.
pid_t spawnReaped(EventLoop& loop, const std::string& path,
                  const std::vector<std::string>& args, bool newSession = false,
                  const std::vector<std::string>& env_add = {},
                  bool devnull_stdio = false);

}  // namespace qypr