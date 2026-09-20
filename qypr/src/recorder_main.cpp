// recorder_main.cpp - Lightweight standalone qypr-record daemon.
//
// Separating this from the main qypr-lock binary prevents loading heavy graphical
// and security dependencies (Wayland, Cairo, Pango, PAM, mpv, etc.) in the background
// service, reducing the idle RSS dramatically.

#include <cstdio>
#include <cstring>
#include <iostream>
#include <malloc.h>
#include "core/EventLoop.hpp"
#include "notifications/NotificationLog.hpp"
#include "notifications/NotificationMonitor.hpp"
#include "Version.hpp"

namespace qypr {
static int runRecorder() {
    EventLoop loop;
    NotificationMonitor monitor(loop);
    NotificationLog log(loop, monitor);
    if (!monitor.start() || !log.start()) { return 1; }
    // Trim memory after initialization to release startup allocations back to OS.
    malloc_trim(0);
    loop.run();
    return 0;
}
}  // namespace qypr

#ifdef TESTING
static int qyprRecordMain(int argc, char** argv) {
#else
int main(int argc, char** argv) {
#endif
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--version") == 0) {
            std::cout << "qypr-record " << QYPR_VERSION_STRING << '\n';
            return 0;
        }
    }
    return qypr::runRecorder();
}
