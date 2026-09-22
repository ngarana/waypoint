#include "waylaunch/app_launcher.h"
#include "system/DesktopIndex.hpp"
#include <cstdlib>

namespace waylaunch {

AppLauncher::AppLauncher() = default;
AppLauncher::~AppLauncher() = default;

void AppLauncher::set_search_paths(const std::vector<std::string>& paths) { search_paths_ = paths; }

std::vector<std::string> AppLauncher::get_desktop_dirs() const {
    std::vector<std::string> dirs;

    if (!search_paths_.empty()) { return search_paths_; }

    // Default XDG directories
    const char* data_home = getenv("XDG_DATA_HOME");
    if (data_home && data_home[0] != '\0') {
        dirs.push_back(std::string(data_home) + "/applications");
    }

    const char* home = getenv("HOME");
    if (home) { dirs.push_back(std::string(home) + "/.local/share/applications"); }

    // System directories
    dirs.emplace_back("/usr/share/applications");
    dirs.emplace_back("/usr/local/share/applications");
    dirs.emplace_back("/usr/share/gnome/applications");
    dirs.emplace_back("/usr/share/gnome/apps");
    dirs.emplace_back("/usr/share/mate/applications");

    return dirs;
}

void AppLauncher::scan() {
    // Shared index (libwl-common): spec parsing (NoDisplay/Hidden/Type
    // filter, Exec field codes), id dedup, name sort, and the precomputed
    // lowercase haystack. Dir selection stays here so configured paths and
    // the GNOME/MATE extras behave exactly as before.
    index_.setSearchPaths(get_desktop_dirs());
    index_.load();
}

std::vector<const qypr::DesktopEntry*> AppLauncher::search(const std::string& query) const {
    return index_.search(query);
}

} // namespace waylaunch
