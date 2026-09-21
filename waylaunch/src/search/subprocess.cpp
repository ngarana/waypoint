#include "waylaunch/subprocess.h"
#include "core/EventLoop.hpp"
#include "core/Process.hpp"
#include <cerrno>
#include <csignal>
#include <cstddef>
#include <cstring>
#include <sched.h>
#include <string>
#include <sys/mman.h>
#include <sys/types.h>
#include <unistd.h>
#include <vector>

namespace waylaunch {
namespace {

// Empty-argv failure, same shape as qypr::runCapture's own (callers log
// result.stderr, so the text must stay informative here too).
ProcessResult spawn_failure(const std::string& what) {
    return {.exit_code = -1, .stdout = "", .stderr = "Failed to spawn: " + what};
}

} // namespace

ProcessResult Subprocess::run(const std::vector<std::string>& argv, const std::string& stdin_data) {
    if (argv.empty()) return spawn_failure("empty argv");
    // I3 absolute-path rule: resolve once in the parent, never in the child.
    const std::string path = qypr::resolveToolPath(argv[0]);
    if (path.empty()) return spawn_failure(argv[0] + ": " + std::strerror(ENOENT));
    std::vector<std::string> resolved = argv;
    resolved[0] = path;
    const qypr::ProcessResult r = qypr::runCapture(path, resolved, stdin_data);
    return {.exit_code = r.exitCode, .stdout = r.stdOut, .stderr = r.stdErr};
}

bool Subprocess::command_exists(const std::string& command) { return qypr::commandExists(command); }

void Subprocess::spawn_detached(const std::vector<std::string>& argv) {
    if (argv.empty()) return;
    qypr::spawnDaemon(argv);
}

// Shared-spawn adapter: resolve argv[0] once (I3 absolute-path rule) and reap
// through the loop.
void Subprocess::spawn_reaped(qypr::EventLoop& loop, const std::vector<std::string>& argv) {
    if (argv.empty()) return;
    const std::string path = qypr::resolveToolPath(argv[0]);
    if (path.empty()) return;
    std::vector<std::string> resolved = argv;
    resolved[0] = path;
    qypr::spawnReaped(loop, path, resolved, /*newSession=*/true);
}

void Subprocess::launch(qypr::EventLoop* loop, const std::vector<std::string>& argv) {
    if (loop == nullptr) {
        spawn_detached(argv);
    } else {
        spawn_reaped(*loop, argv);
    }
}

// Single supervised child (new session) that stays ours for waitpid/SIGCHLD
// reaping. The caller owns respawn policy; this only spawns and execs.
//
// Implemented with a raw vfork-clone rather than fork(), deliberately: fork()
// runs pthread_atfork handlers, and forking while a worker thread (e.g.
// fontconfig, pulled in by tab-strip rendering) holds a lock wedged daemons
// mid-spawn with all signals blocked for signalfd — observed live as a deaf
// futex-parked process. The clone child touches nothing but the exec (no
// atfork handlers run, no lock state is cloned), then execve replaces it.
//
// The child also starts with an empty signal mask: our daemons block signals
// for signalfd multiplexing, and an inherited blocked mask would leave the
// child undeaf to SIGTERM/SIGINT (observed live as unkillable terminals).
namespace {

struct TrackedSpawn {
    char* const* argv;
    const char* path;
    char* const* env;
};

int tracked_child(void* raw) {
    auto* args = static_cast<TrackedSpawn*>(raw);
    sigset_t empty;
    sigemptyset(&empty);
    sigprocmask(SIG_SETMASK, &empty, nullptr);
    setsid();
    execve(args->path, args->argv, args->env);
    _exit(127);
}

// execvp-equivalent resolution (the vfork child may not allocate): absolute
// paths pass through, bare names use the shared I3 lookup, and a miss keeps
// the original name so execve fails with ENOENT (same exit code execvp
// would produce).
std::string resolve_exec(const std::string& name) {
    if (name.find('/') != std::string::npos) return name;
    const std::string resolved = qypr::resolveToolPath(name);
    return resolved.empty() ? name : resolved;
}

} // namespace

pid_t Subprocess::spawn_tracked(const std::vector<std::string>& argv) {
    if (argv.empty()) return -1;
    std::string path = resolve_exec(argv[0]);
    std::vector<char*> c_argv;
    c_argv.reserve(argv.size() + 1);
    for (const auto& s : argv) c_argv.push_back(const_cast<char*>(s.c_str()));
    c_argv.push_back(nullptr);
    TrackedSpawn args{.argv = c_argv.data(), .path = path.c_str(), .env = environ};
    // 1 MiB child stack; the vfork child suspends us until execve/_exit.
    constexpr size_t kStackSize = static_cast<size_t>(1024) * 1024;
    void* stack =
        mmap(nullptr, kStackSize, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (stack == MAP_FAILED) return -1;
    pid_t pid = clone(tracked_child, static_cast<char*>(stack) + kStackSize,
                      CLONE_VFORK | CLONE_VM | SIGCHLD, &args);
    munmap(stack, kStackSize);
    return pid;
}

} // namespace waylaunch
