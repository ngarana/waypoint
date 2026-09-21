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

// PATH executability probe (X_OK, same lookup as resolveToolPath): feature
// detection for optional helpers (pdftotext, wl-copy, systemctl). Prefer this
// over resolving-then-checking-empty — the intent reads at the call site.
bool commandExists(const std::string& name);

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
                    bool newSession = false, const std::vector<std::string>& env_add = {},
                    bool devnull_stdio = false);

// spawnDetached() plus reaping: the child's pidfd is watched through the event
// loop, so the loop is woken when the child exits and waitpid() reaps it there.
// Push-based — no polling, no zombie, no process-wide SIGCHLD disposition.
// Returns the child's pid, or -1.
pid_t spawnReaped(EventLoop& loop, const std::string& path, const std::vector<std::string>& args,
                  bool newSession = false, const std::vector<std::string>& env_add = {},
                  bool devnull_stdio = false);

// Captured run: absolute `path` with `args` (args[0] is the argv[0] the child
// sees), stdin fed from `stdinData` (empty = immediate EOF), stdout/stderr
// captured. Synchronous: pumps all three pipes through one poll loop, so a
// child filling stdout while awaiting stdin cannot deadlock. Returns exit -1
// with the cause in stdErr when spawning itself fails (same shape as a child
// that wrote to stderr).
//
// Do NOT use this for self-daemonizing helpers (wl-copy and friends inherit
// the pipes and never close them, so EOF never arrives and the caller hangs
// forever): spawnReaped()/spawnDaemon() those instead. The child inherits
// signal dispositions (unlike spawnDetached) and runs with an empty signal
// mask, so a blocked mask in the parent never deafens the helper.
struct ProcessResult {
    int exitCode = -1;
    std::string stdOut;
    std::string stdErr;
};

ProcessResult runCapture(const std::string& path, const std::vector<std::string>& args,
                         const std::string& stdinData = "");

// Daemon launch for contexts with no EventLoop (transient processes, offline
// use): double-fork + setsid, reparented to init so no zombie ever needs
// reaping. Fire-and-forget — deliberately no pid, no output, no exit status.
// Bare names are PATH-searched like execvp. Prefer spawnReaped() wherever a
// loop exists: a tracked child with an exit is always easier to debug than a
// reparented one.
//
// fork() in a multithreaded process (see QL-6) is only safe here because the
// child touches nothing but setsid/fork/exec between fork and exec.
void spawnDaemon(const std::vector<std::string>& argv);

}  // namespace qypr