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
    return matugen_.poll(refresh(repo_config, config_path));
}

} // namespace waylaunch
