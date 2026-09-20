// bar_main.cpp - qypr-bar entry point.
//
// A standalone, general-purpose Wayland status bar (wlr-layer-shell) for the
// unlocked desktop — a waybar replacement that shares the lock screen's
// StatusBar and indicators. Runs until the compositor or the user tears it down.

#include <cstring>
#include <iostream>

#include "core/BarApp.hpp"
#include "Version.hpp"

int main(int argc, char** argv) {
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--version") == 0) {
            std::cout << "qypr-bar " << QYPR_VERSION_STRING << '\n';
            return 0;
        }
        // Offline preview: render bar frames to PNG without a Wayland connection.
        //   qypr-bar --preview [path.png]
        if (std::strcmp(argv[i], "--preview") == 0) {
            qypr::BarApp app;
            std::string const path = (i + 1 < argc) ? argv[i + 1] : "qypr-bar-preview.png";
            return app.preview(path);
        }
    }

    qypr::BarApp app;
    return app.run();
}
