// Audit.hpp - Lock-screen security events to the system journal.
//
// hypridle starts qypr-lock with stdout and stderr on /dev/null, so anything
// worth reading after the fact — the session locked, how it was unlocked,
// a fingerprint lockout — goes to syslog under LOG_AUTHPRIV, the facility
// login and other screen lockers use. Read with `journalctl -t qypr-lock`.
// Test builds (TESTING) log nothing.

#pragma once

#ifndef TESTING
#    include <syslog.h>
#endif

namespace qypr::audit {

enum class Level { Info, Notice };

inline void log(Level level, const char* message) {
#ifndef TESTING
    syslog(LOG_AUTHPRIV | (level == Level::Notice ? LOG_NOTICE : LOG_INFO), "%s", message);
#else
    (void)level;
    (void)message;
#endif
}

}  // namespace qypr::audit
