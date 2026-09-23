// SystemActions.cpp - see the header for why there is no fork() and no
// process-wide SIGCHLD disposition (QL-5, QL-6).
#include "power/SystemActions.hpp"

#include <cstdio>

#include "core/EventLoop.hpp"
#include "core/Process.hpp"

namespace qypr {

SystemActions::SystemActions(EventLoop& loop)
    : loop_(loop),
      systemctl_(resolveToolPath("systemctl")),
      loginctl_(resolveToolPath("loginctl")) {
    // One diagnosis, at startup, for the whole run: a missing systemctl makes
    // every power action a silent no-op otherwise.
    if (systemctl_.empty()) {
        std::fprintf(stderr, "qypr: systemctl not found in PATH; power actions disabled\n");
    }
}

void SystemActions::lock() {
    if (loginctl_.empty()) {
        std::fprintf(stderr, "qypr: loginctl not found in PATH; lock action disabled\n");
        return;
    }
#ifdef TESTING
    // TESTING: spawning is a no-op
#else
    spawnReaped(loop_, loginctl_, {loginctl_, "lock-session"});
#endif
}

void SystemActions::run(const char* verb) {
#ifdef TESTING
    (void)verb;  // TESTING: the action must not touch the test host
#else
    if (systemctl_.empty()) { return; }  // warned once at construction
    spawnReaped(loop_, systemctl_, {systemctl_, verb});
#endif
}

}  // namespace qypr
