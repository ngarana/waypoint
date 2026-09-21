#include "waylaunch/clipboard.h"
#include "waylaunch/subprocess.h"
#include <string>
#include <vector>

namespace waylaunch {

// wl-copy forks a background process to keep serving the clipboard selection
// after it returns, and that daemon inherits (and never closes) our pipe fds.
// Subprocess::run() waits for those pipes to hit EOF, so with wl-copy it never
// returns — it blocks forever, and since every activation runs through here
// first, the whole launcher (and its exclusive keyboard grab) hangs with it.
// wl-copy takes the content as a trailing argument, so no stdin pipe is
// needed at all: spawn_reaped() (pidfd-reaped, no pipes) is correct here —
// and unlike Subprocess::run() it never waits on pipe EOF, so wl-copy's
// self-daemonizing (it inherits fds and never closes them) cannot hang the
// launcher and its exclusive keyboard grab the way run() did.
bool Clipboard::copy_text(const std::string& text, qypr::EventLoop* loop) {
    if (!Subprocess::command_exists("wl-copy")) return false;
    Subprocess::spawn_reaped(loop, {"wl-copy", "--type", "text/plain", text});
    return true;
}

bool Clipboard::copy_file_path(const std::string& path, qypr::EventLoop* loop) {
    if (!Subprocess::command_exists("wl-copy")) return false;
    Subprocess::spawn_reaped(loop, {"wl-copy", "--type", "text/uri-list", path});
    return true;
}

std::string Clipboard::paste_text() {
    if (!Subprocess::command_exists("wl-paste")) return "";
    std::vector<std::string> argv = {"wl-paste", "--no-newline"};
    auto result = Subprocess::run(argv);
    return result.stdout;
}

} // namespace waylaunch
