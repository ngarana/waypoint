// process_test.cpp - Unit tests for the shared process contract
// (core/Process): lookup, captured runs, daemon launches.
// Assert-based, like the other shared suites. Hermetic: only /bin/sh,
// /bin/echo and a mkdtemp probe dir; no EventLoop needed except the
// spawnReaped case, which pumps its own.
#include "core/Process.hpp"

#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <sys/stat.h>
#include <unistd.h>

#include "core/EventLoop.hpp"

namespace {

void test_lookup() {
    assert(!qypr::resolveToolPath("").empty() == false);
    assert(!qypr::resolveToolPath("sh").empty());
    assert(qypr::resolveToolPath("definitely-not-a-tool-xyz").empty());
    assert(qypr::commandExists("sh"));
    assert(!qypr::commandExists("definitely-not-a-tool-xyz"));
    // Absolute paths pass through only when executable.
    assert(qypr::resolveToolPath("/bin/sh") == "/bin/sh");
    assert(qypr::resolveToolPath("/nonexistent/binary-xyz").empty());
    std::printf("[PASS] lookup\n");
}

void test_capture() {
    const std::string sh = qypr::resolveToolPath("sh");
    assert(!sh.empty());
    // Echo roundtrip with stdin fed.
    auto echo = qypr::runCapture(sh, {sh, "-c", "cat"}, "hello\n");
    assert(echo.exitCode == 0);
    assert(echo.stdOut == "hello\n");
    // Exit codes and stderr survive.
    auto fail = qypr::runCapture(sh, {sh, "-c", "echo oops >&2; exit 3"});
    assert(fail.exitCode == 3);
    assert(fail.stdErr == "oops\n");
    // Empty argv and missing binary fail in the same shape (exit -1 plus a
    // cause in stdErr), never by crashing.
    auto empty = qypr::runCapture("", {});
    assert(empty.exitCode == -1 && !empty.stdErr.empty());
    auto missing = qypr::runCapture("/nonexistent/binary-xyz", {"/nonexistent/binary-xyz"});
    assert(missing.exitCode == -1 && !missing.stdErr.empty());
    std::printf("[PASS] capture\n");
}

void test_capture_large() {
    // More output than a pipe holds while stdin is also flowing: the old
    // single write-before-read path deadlocked here.
    const std::string sh = qypr::resolveToolPath("sh");
    const std::string big(1024 * 1024, 'x');
    auto r =
        qypr::runCapture(sh, {sh, "-c", "(yes x | head -c 1048576) & cat >/dev/null; wait"}, big);
    assert(r.exitCode == 0);
    assert(r.stdOut.size() == 1024 * 1024);
    std::printf("[PASS] capture large\n");
}

std::string g_probeDir;

void test_daemon() {
    // Fire-and-forget: the child outlives the call (reparented past us), so
    // poll for the probe file instead of any pid.
    const std::string probe = g_probeDir + "/daemon-probe";
    const std::string sh = qypr::resolveToolPath("sh");
    qypr::spawnDaemon({sh, "-c", std::string("echo daemon-ok > ") + probe});
    for (int i = 0; i < 100 && access(probe.c_str(), F_OK) != 0; ++i) usleep(20 * 1000);
    assert(access(probe.c_str(), F_OK) == 0);
    qypr::spawnDaemon({});  // empty argv is a no-op, not a crash
    std::printf("[PASS] daemon\n");
}

void test_reaped() {
    // pidfd-reaped through the loop: no zombie, no SIGCHLD disposition.
    qypr::EventLoop loop;
    const std::string sh = qypr::resolveToolPath("sh");
    const pid_t pid = qypr::spawnReaped(loop, sh, {sh, "-c", "exit 0"});
    assert(pid > 0);
    loop.addTimer(2000, false, [&] { loop.quit(); });
    loop.run();
    std::printf("[PASS] reaped\n");
}

}  // namespace

int main() {
    char tmpl[] = "/tmp/wl-process-test-XXXXXX";
    assert(::mkdtemp(tmpl) != nullptr);
    g_probeDir = tmpl;
    test_lookup();
    test_capture();
    test_capture_large();
    test_daemon();
    test_reaped();
    printf("All Process unit tests passed successfully!\n");
    return 0;
}
