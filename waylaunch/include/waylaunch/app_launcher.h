#pragma once

#include "system/DesktopIndex.hpp" // shared index (libwl-common subtree)
#include <string>
#include <vector>

namespace waylaunch {

struct DesktopEntry {
    std::string name;
    std::string exec;
    std::string icon;
    std::string categories;
    std::string comment;
    std::string generic_name;
    std::string desktop_path; // source .desktop file (for "reveal in files")
    bool no_display = false;
    bool hidden = false;
    // Lowercased "name generic comment categories", built once at scan time so
    // search() is a single find() per entry instead of re-lowercasing 4 fields
    // on every keystroke.
    std::string search_key;
};

class AppLauncher {
  public:
    AppLauncher();
    ~AppLauncher();

    void scan();
    void set_search_paths(const std::vector<std::string>& paths);

    std::vector<DesktopEntry> search(const std::string& query) const;

  private:
    std::vector<std::string> get_desktop_dirs() const;

    qypr::DesktopIndex index_;
    std::vector<std::string> search_paths_;
};

} // namespace waylaunch
