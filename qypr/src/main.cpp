// main.cpp - qypr-lock entry point.
//
// A lean C++ Wayland session-lock screen (ext-session-lock-v1). Running the
// binary locks the session; successful PAM authentication unlocks and exits.

#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>

#include "core/App.hpp"
#include "core/EventLoop.hpp"
#include "notifications/NotificationMonitor.hpp"
#include "Version.hpp"

#ifdef TESTING
namespace {
// Linked into qypr-test only to share this TU; test_main.cpp provides main.
// Unused there by design (it would clash), hence anonymous + maybe_unused.
[[maybe_unused]] int qyprMain(int argc, char** argv) {
#else
int main(int argc, char** argv) {
#endif
    qypr::App app;

    // Offline preview: render frames to PNG without locking the session.
    //   qypr-lock --preview [path.png]
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--version") == 0) {
            std::cout << "qypr " << QYPR_VERSION_STRING << '\n';
            return 0;
        }
        if (std::strcmp(argv[i], "--preview") == 0) {
            std::string const path = (i + 1 < argc) ? argv[i + 1] : "qypr-preview.png";
            return app.preview(path);
        }
        // Offscreen video-pipeline test: exercise mpv without locking.
        //   qypr-lock --video-test [seconds]
        if (std::strcmp(argv[i], "--video-test") == 0) {
            // NOLINTNEXTLINE(bugprone-unchecked-string-to-number-conversion) // lenient CLI arg
            int const secs = (i + 1 < argc) ? std::atoi(argv[i + 1]) : 6;
            return app.videoTest(secs > 0 ? secs : 6);
        }
        // Idle seconds before the video pauses and the screen dims.
        //   qypr-lock --idle-timeout <seconds>
        if (std::strcmp(argv[i], "--idle-timeout") == 0 && i + 1 < argc) {
            // NOLINTNEXTLINE(bugprone-unchecked-string-to-number-conversion) // lenient CLI arg
            app.setIdleTimeout(std::atoi(argv[++i]));
        }
    }

    return app.run();
}
#ifdef TESTING
}  // namespace
#endif
