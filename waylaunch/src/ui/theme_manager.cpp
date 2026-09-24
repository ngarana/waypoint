#include "waylaunch/theme_manager.h"

#include <system_error>

namespace waylaunch {

ThemeConfig ThemeManager::refresh(Config& repo_config, const std::string& config_path) {
    if (!config_path.empty()) {
        std::error_code ec;
        auto mtime = std::filesystem::last_write_time(config_path, ec);
        if (!ec && (!config_mtime_.has_value() || *config_mtime_ != mtime)) {
            config_mtime_ = mtime;
            Config fresh;
            // Parse failures keep the running theme — never blank the
            // overlay over a half-saved file.
            if (fresh.load(config_path)) repo_config.get().theme = fresh.get().theme;
        }
    }
    ThemeConfig effective = repo_config.get().theme;
    effective.mode = solar_.effective_mode(effective.mode);
    return effective;
}

ColorConfig ThemeManager::colors(Config& repo_config, const std::string& config_path) {
    return matugen_.resolve(refresh(repo_config, config_path));
}

bool ThemeManager::poll(Config& repo_config, const std::string& config_path) {
    const ThemeConfig effective = refresh(repo_config, config_path);
    const bool colors_changed = matugen_.poll(effective);
    const bool mode_changed = !have_last_effective_mode_ || last_effective_mode_ != effective.mode;
    last_effective_mode_ = effective.mode;
    have_last_effective_mode_ = true;
    return colors_changed || mode_changed;
}

} // namespace waylaunch
