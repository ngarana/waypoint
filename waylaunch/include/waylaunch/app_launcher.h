#pragma once

#include "system/DesktopIndex.hpp" // shared index (libwl-common subtree)
#include <string>
#include <vector>

namespace waylaunch {

// AppLauncher owns the shared libwl-common desktop index and its scan roots.
// Results are the common qypr::DesktopEntry model directly — no local copy
// struct (ARCHITECTURE_REVIEW finding 6): every field the provider reads
// (name/exec/icon/comment/desktopPath/...) lives in exactly one place.
class AppLauncher {
  public:
    AppLauncher();
    ~AppLauncher();

    void scan();
    void set_search_paths(const std::vector<std::string>& paths);

    std::vector<const qypr::DesktopEntry*> search(const std::string& query) const;

  private:
    std::vector<std::string> get_desktop_dirs() const;

    qypr::DesktopIndex index_;
    std::vector<std::string> search_paths_;
};

} // namespace waylaunch
