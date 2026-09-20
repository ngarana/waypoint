// Process.cpp - see the header for the rationale (QL-5, QL-6).
#include "core/Process.hpp"

#include <fcntl.h>
#include <spawn.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cerrno>
#include <csignal>
#include <cstdlib>

#include "core/EventLoop.hpp"

// posix_spawn() takes the environment explicitly; environ is the caller's.
extern char** environ;

namespace qypr {

namespace {
// SIGCHLD/SIGPIPE at their default in every child we start. SIGCHLD is the one
// that matters (an inherited SIG_IGN survives execve and breaks waitpid() in the
// helper); SIGPIPE is reset for the same reason — a program told to ignore it
// behaves differently from the one the user expects.
void setChildAttributes(posix_spawnattr_t& attr, bool newSession) {
    sigset_t defaults;
    sigemptyset(&defaults);
    sigaddset(&defaults, SIGCHLD);
    sigaddset(&defaults, SIGPIPE);
    posix_spawnattr_setsigdefault(&attr, &defaults);
    posix_spawnattr_setflags(&attr, newSession
                                       ? static_cast<short>(POSIX_SPAWN_SETSIGDEF |
                                                            POSIX_SPAWN_SETSID)
                                       : static_cast<short>(POSIX_SPAWN_SETSIGDEF));
}

// The directories searched after PATH: a login-less environment (a systemd user
// service, a display manager session) may carry a PATH without sbin.
const char* const kFallbackDirs[] = {"/usr/local/bin", "/usr/bin", "/bin",
                                     "/usr/local/sbin", "/usr/sbin", "/sbin"};
}  // namespace

std::string resolveToolPath(const std::string& name) {
    if (name.empty()) return {};
    if (name.front() == '/') { return ::access(name.c_str(), X_OK) == 0 ? name : std::string{}; }

    auto search = [&](const std::string& dir) -> std::string {
        if (dir.empty()) return {};
        const std::string candidate = dir + "/" + name;
        return ::access(candidate.c_str(), X_OK) == 0 ? candidate : std::string{};
    };

    if (const char* path = std::getenv("PATH")) {
        std::string dirs = path;
        size_t start = 0;
        while (start <= dirs.size()) {
            const size_t sep = dirs.find(':', start);
            const size_t end = (sep == std::string::npos) ? dirs.size() : sep;
            if (std::string found = search(dirs.substr(start, end - start)); !found.empty()) {
                return found;
            }
            if (sep == std::string::npos) { break; }
            start = sep + 1;
        }
    }
    for (const char* dir : kFallbackDirs) {
        if (std::string found = search(dir); !found.empty()) { return found; }
    }
    return {};
}

pid_t spawnDetached(const std::string& path, const std::vector<std::string>& args,
                    bool newSession, const std::vector<std::string>& env_add,
                    bool devnull_stdio) {
    if (path.empty()) return -1;

    std::vector<char*> argv;
    argv.reserve(args.size() + 1);
    for (const auto& a : args) { argv.push_back(const_cast<char*>(a.c_str())); }
    argv.push_back(nullptr);

    // Extra environment, assembled in the parent: appending callers' strings
    // to a copy of environ needs no fork and respects the no-allocation
    // discipline the old fork-based launchers required between fork and exec.
    // env_owned holds the copies so envp pointers stay stable across the call.
    std::vector<std::string> env_owned = env_add;
    std::vector<char*> envp;
    for (char** e = environ; e && *e; ++e) envp.push_back(*e);
    for (auto& e : env_owned) envp.push_back(e.data());
    envp.push_back(nullptr);

    posix_spawn_file_actions_t actions;
    bool actions_ready = posix_spawn_file_actions_init(&actions) == 0;
    if (actions_ready && devnull_stdio) {
        // Open /dev/null once, fan out to 0/1/2, close the original: the
        // hook inherits no terminal and logs nothing into the parent's fds.
        actions_ready =
            posix_spawn_file_actions_addopen(&actions, 3, "/dev/null", O_RDWR, 0) == 0 &&
            posix_spawn_file_actions_adddup2(&actions, 3, STDIN_FILENO) == 0 &&
            posix_spawn_file_actions_adddup2(&actions, 3, STDOUT_FILENO) == 0 &&
            posix_spawn_file_actions_adddup2(&actions, 3, STDERR_FILENO) == 0 &&
            posix_spawn_file_actions_addclose(&actions, 3) == 0;
    }

    posix_spawnattr_t attr;
    bool attrs_ready = posix_spawnattr_init(&attr) == 0;
    if (attrs_ready) {
        setChildAttributes(attr, newSession);
    } else {
        // Attribute setup failed: without SIGDEF/setsid guarantees, refuse
        // rather than spawn a misconfigured child.
        if (actions_ready) posix_spawn_file_actions_destroy(&actions);
        return -1;
    }

    pid_t pid = -1;
    const int rc =
        actions_ready
            ? posix_spawn(&pid, path.c_str(), &actions, &attr, argv.data(), envp.data())
            : -1;
    posix_spawnattr_destroy(&attr);
    if (actions_ready) posix_spawn_file_actions_destroy(&actions);
    return rc == 0 ? pid : -1;
}

pid_t spawnReaped(EventLoop& loop, const std::string& path, const std::vector<std::string>& args,
                  bool newSession, const std::vector<std::string>& env_add,
                  bool devnull_stdio) {
    const pid_t pid = spawnDetached(path, args, newSession, env_add, devnull_stdio);
    if (pid <= 0) return pid;

#if defined(SYS_pidfd_open)
    const int fd = static_cast<int>(::syscall(SYS_pidfd_open, pid, 0));
    if (fd >= 0) {
        // The pidfd reports readable the moment the child exits; reaping there
        // is a waitpid() that cannot block.
        loop.addFd(fd, [&loop, fd, pid](uint32_t) {
            (void)::waitpid(pid, nullptr, WNOHANG);
            loop.removeFd(fd);
            ::close(fd);
        });
        return pid;
    }
#endif
    // No pidfd (pre-5.3 kernel): best-effort non-blocking reap. Blocking here
    // would stall the lock UI on a systemctl/loginctl that may never return,
    // and a stale zombie is strictly better than a stalled locker.
    (void)::waitpid(pid, nullptr, WNOHANG);
    return pid;
}

}  // namespace qypr