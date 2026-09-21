#pragma once

#include <string>

namespace qypr {
class EventLoop;
}

namespace waylaunch {

class Clipboard {
  public:
    // Loop enables the shared reaped spawn; null keeps the legacy fork path
    // (tests, offline use).
    static bool copy_text(const std::string& text, qypr::EventLoop* loop = nullptr);
    static bool copy_file_path(const std::string& path, qypr::EventLoop* loop = nullptr);
    static std::string paste_text();
};

} // namespace waylaunch
