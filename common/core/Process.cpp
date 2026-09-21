// Process.cpp - see the header for the rationale (QL-5, QL-6).
#include "core/Process.hpp"

#include <fcntl.h>
#include <poll.h>
#include <spawn.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <unistd.h>

#include <array>
#include <cerrno>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

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
    posix_spawnattr_setflags(
        &attr, newSession ? static_cast<short>(POSIX_SPAWN_SETSIGDEF | POSIX_SPAWN_SETSID)
                          : static_cast<short>(POSIX_SPAWN_SETSIGDEF));
}

// The directories searched after PATH: a login-less environment (a systemd user
// service, a display manager session) may carry a PATH without sbin.
const char* const kFallbackDirs[] = {"/usr/local/bin",  "/usr/bin",  "/bin",
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

pid_t spawnDetached(const std::string& path, const std::vector<std::string>& args, bool newSession,
                    const std::vector<std::string>& env_add, bool devnull_stdio) {
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
    const int rc = actions_ready
                       ? posix_spawn(&pid, path.c_str(), &actions, &attr, argv.data(), envp.data())
                       : -1;
    posix_spawnattr_destroy(&attr);
    if (actions_ready) posix_spawn_file_actions_destroy(&actions);
    return rc == 0 ? pid : -1;
}

pid_t spawnReaped(EventLoop& loop, const std::string& path, const std::vector<std::string>& args,
                  bool newSession, const std::vector<std::string>& env_add, bool devnull_stdio) {
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

bool commandExists(const std::string& name) {
    return !resolveToolPath(name).empty();
}

ProcessResult runCapture(const std::string& path, const std::vector<std::string>& args,
                         const std::string& stdinData) {
    if (args.empty() || path.empty()) {
        return {.exitCode = -1, .stdOut = "", .stdErr = "Failed to spawn: empty argv"};
    }

    int stdinPipe[2] = {-1, -1};
    int stdoutPipe[2] = {-1, -1};
    int stderrPipe[2] = {-1, -1};
    auto closePipe = [](int pipeFds[2]) {
        if (pipeFds[0] >= 0) close(pipeFds[0]);
        if (pipeFds[1] >= 0) close(pipeFds[1]);
        pipeFds[0] = pipeFds[1] = -1;
    };
    auto closeAll = [&]() {
        closePipe(stdinPipe);
        closePipe(stdoutPipe);
        closePipe(stderrPipe);
    };
    auto pipeFailure = [&](int error) {
        closeAll();
        return ProcessResult{.exitCode = -1,
                             .stdOut = "",
                             .stdErr = "Failed to create pipe: " + std::string(strerror(error))};
    };

    if (pipe(stdinPipe) < 0) return pipeFailure(errno);
    if (pipe(stdoutPipe) < 0) return pipeFailure(errno);
    if (pipe(stderrPipe) < 0) return pipeFailure(errno);

    posix_spawn_file_actions_t actions;
    int actionError = posix_spawn_file_actions_init(&actions);
    if (actionError != 0) {
        closeAll();
        return {.exitCode = -1,
                .stdOut = "",
                .stdErr = "Failed to prepare spawn: " + std::string(strerror(actionError))};
    }
    // Fresh signal mask for the child: our processes block signals for
    // signalfd multiplexing, and an inherited blocked mask would make the
    // child undeaf to SIGTERM/SIGINT. Dispositions stay inherited (all
    // default/ignored here, never custom handlers).
    posix_spawnattr_t attrs;
    bool attrsReady = posix_spawnattr_init(&attrs) == 0;
    if (attrsReady) {
        sigset_t empty;
        sigemptyset(&empty);
        attrsReady = posix_spawnattr_setsigmask(&attrs, &empty) == 0 &&
                     posix_spawnattr_setflags(&attrs, POSIX_SPAWN_SETSIGMASK) == 0;
        if (!attrsReady) posix_spawnattr_destroy(&attrs);
    }
    if (!attrsReady) {
        posix_spawn_file_actions_destroy(&actions);
        closeAll();
        return {.exitCode = -1, .stdOut = "", .stdErr = "Failed to prepare spawn attrs"};
    }
    auto addAction = [&](int result) {
        if (result == 0) return true;
        actionError = result;
        return false;
    };
    // Child plumbing: it reads stdinPipe[0], writes stdoutPipe[1] and
    // stderrPipe[1]; every other pipe end is closed. (An earlier revision
    // had stdin backwards — duping the write end onto stdin — which surfaced
    // as `cat: -: Bad file descriptor` wherever asserts were enabled.)
    const bool actionsReady =
        addAction(posix_spawn_file_actions_addclose(&actions, stdinPipe[1])) &&
        addAction(posix_spawn_file_actions_addclose(&actions, stdoutPipe[0])) &&
        addAction(posix_spawn_file_actions_addclose(&actions, stderrPipe[0])) &&
        addAction(posix_spawn_file_actions_adddup2(&actions, stdinPipe[0], STDIN_FILENO)) &&
        addAction(posix_spawn_file_actions_adddup2(&actions, stdoutPipe[1], STDOUT_FILENO)) &&
        addAction(posix_spawn_file_actions_adddup2(&actions, stderrPipe[1], STDERR_FILENO)) &&
        addAction(posix_spawn_file_actions_addclose(&actions, stdinPipe[0])) &&
        addAction(posix_spawn_file_actions_addclose(&actions, stdoutPipe[1])) &&
        addAction(posix_spawn_file_actions_addclose(&actions, stderrPipe[1]));
    if (!actionsReady) {
        posix_spawn_file_actions_destroy(&actions);
        posix_spawnattr_destroy(&attrs);
        closeAll();
        return {.exitCode = -1,
                .stdOut = "",
                .stdErr = "Failed to prepare spawn: " + std::string(strerror(actionError))};
    }

    std::vector<char*> cArgv;
    cArgv.reserve(args.size());
    for (const auto& s : args) cArgv.push_back(const_cast<char*>(s.c_str()));
    cArgv.push_back(nullptr);

    pid_t pid = -1;
    const int rc = posix_spawn(&pid, path.c_str(), &actions, &attrs, cArgv.data(), environ);
    posix_spawn_file_actions_destroy(&actions);
    posix_spawnattr_destroy(&attrs);

    if (rc != 0) {
        closeAll();
        return {.exitCode = -1,
                .stdOut = "",
                .stdErr = "Failed to spawn: " + std::string(strerror(rc))};
    }

    close(stdinPipe[0]);  // child's read end; parent keeps the write end
    stdinPipe[0] = -1;
    close(stdoutPipe[1]);
    stdoutPipe[1] = -1;
    close(stderrPipe[1]);
    stderrPipe[1] = -1;
    if (stdinData.empty()) {
        close(stdinPipe[1]);  // child sees EOF immediately
        stdinPipe[1] = -1;
    }

    std::string stdOut;
    std::string stdErr;
    std::array<char, 4096> readBuf;
    std::size_t stdinOffset = 0;
    std::array<pollfd, 3> pfds = {{{.fd = stdinData.empty() ? -1 : stdinPipe[1],
                                    .events = static_cast<short>(stdinData.empty() ? 0 : POLLOUT),
                                    .revents = 0},
                                   {.fd = stdoutPipe[0], .events = POLLIN, .revents = 0},
                                   {.fd = stderrPipe[0], .events = POLLIN, .revents = 0}}};

    auto setNonblocking = [](int fd) {
        int flags = fcntl(fd, F_GETFL, 0);
        if (flags < 0) return false;
        return fcntl(fd, F_SETFL, flags | O_NONBLOCK) == 0;
    };
    std::string stdinError;
    if (pfds[0].fd >= 0 && !setNonblocking(pfds[0].fd)) {
        stdinError = "stdin fcntl: " + std::string(strerror(errno));
        close(pfds[0].fd);
        pfds[0].fd = -1;
    }
    // Nonblocking output reads let us drain both streams without ever waiting
    // on one pipe while the other is full.
    if (!setNonblocking(pfds[1].fd)) pfds[1].events = POLLIN;
    if (!setNonblocking(pfds[2].fd)) pfds[2].events = POLLIN;

    // A child may close stdin before all input is written. Ignore SIGPIPE for
    // this synchronous write window so the error can be returned instead of
    // terminating the caller.
    struct sigaction ignoreSigpipe{};
    struct sigaction oldSigpipe{};
    sigemptyset(&ignoreSigpipe.sa_mask);
    ignoreSigpipe.sa_handler = SIG_IGN;
    const bool sigpipeChanged = sigaction(SIGPIPE, &ignoreSigpipe, &oldSigpipe) == 0;

    auto closeStdin = [&]() {
        if (pfds[0].fd >= 0) close(pfds[0].fd);
        pfds[0].fd = -1;
        pfds[0].events = 0;
    };
    auto readOutput = [&](pollfd& pfd, std::string& output) {
        if (pfd.fd < 0 || !(pfd.revents & (POLLIN | POLLHUP | POLLERR | POLLNVAL))) return;
        ssize_t n = 0;
        do { n = read(pfd.fd, readBuf.data(), readBuf.size()); } while (n < 0 && errno == EINTR);
        if (n > 0) {
            output.append(readBuf.data(), static_cast<std::size_t>(n));
        } else if (n == 0 || (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK)) {
            close(pfd.fd);
            pfd.fd = -1;
        }
    };

    while (true) {
        if (pfds[0].fd < 0 && pfds[1].fd < 0 && pfds[2].fd < 0) break;
        int pollRet = 0;
        do { pollRet = poll(pfds.data(), pfds.size(), -1); } while (pollRet < 0 && errno == EINTR);
        if (pollRet < 0) {
            stdinError = "poll: " + std::string(strerror(errno));
            closeStdin();
            if (pfds[1].fd >= 0) {
                close(pfds[1].fd);
                pfds[1].fd = -1;
            }
            if (pfds[2].fd >= 0) {
                close(pfds[2].fd);
                pfds[2].fd = -1;
            }
            kill(pid, SIGTERM);
            break;
        }

        if (pfds[0].fd >= 0 && pfds[0].revents != 0) {
            if ((pfds[0].revents & (POLLERR | POLLHUP | POLLNVAL)) != 0) {
                stdinError = "stdin pipe closed before all input was written";
                closeStdin();
            } else if ((pfds[0].revents & POLLOUT) != 0) {
                const std::size_t remaining = stdinData.size() - stdinOffset;
                const std::size_t chunk = std::min(remaining, static_cast<std::size_t>(64) * 1024);
                ssize_t n = 0;
                do {
                    n = write(pfds[0].fd, stdinData.data() + stdinOffset, chunk);
                } while (n < 0 && errno == EINTR);
                if (n > 0) {
                    stdinOffset += static_cast<std::size_t>(n);
                    if (stdinOffset == stdinData.size()) closeStdin();
                } else if (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
                    stdinError = "stdin write: " + std::string(strerror(errno));
                    closeStdin();
                }
            }
        }
        readOutput(pfds[1], stdOut);
        readOutput(pfds[2], stdErr);
    }

    if (sigpipeChanged) sigaction(SIGPIPE, &oldSigpipe, nullptr);

    int status = 0;
    pid_t waited = -1;
    do { waited = waitpid(pid, &status, 0); } while (waited < 0 && errno == EINTR);
    const int exitCode = waited == pid && WIFEXITED(status) ? WEXITSTATUS(status) : -1;
    if (!stdinError.empty()) {
        if (!stdErr.empty() && stdErr.back() != '\n') stdErr.push_back('\n');
        stdErr += stdinError;
        stdErr.push_back('\n');
    }
    return {.exitCode = exitCode, .stdOut = std::move(stdOut), .stdErr = std::move(stdErr)};
}

void spawnDaemon(const std::vector<std::string>& argv) {
    if (argv.empty()) return;
    // Double-fork + setsid so the launched process outlives the caller and is
    // reparented to init; the intermediate child is reaped immediately, so no
    // EventLoop (and no zombie) is ever involved.
    const pid_t pid = fork();
    if (pid == 0) {
        setsid();
        if (fork() == 0) {
            std::vector<char*> cArgv;
            cArgv.reserve(argv.size() + 1);
            for (const auto& s : argv) cArgv.push_back(const_cast<char*>(s.c_str()));
            cArgv.push_back(nullptr);
            execvp(cArgv[0], cArgv.data());
            _exit(127);
        }
        _exit(0);
    } else if (pid > 0) {
        int status = 0;
        (void)waitpid(pid, &status, 0);
    }
}

}  // namespace qypr