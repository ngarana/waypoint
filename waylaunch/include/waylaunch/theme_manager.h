#pragma once

// Theme ownership for waylaunch overlays (ARCHITECTURE_REVIEW finding 8):
// everything [theme] except the final renderer-shape assembly (which stays
// with the overlay that owns fonts/corners/opacity). Extracted from
// LauncherUI so the overlay keeps composition + event reaction while
// theming — matugen source, solar day/night, live config watch — lives here
// testably. The dropdown strip shares this exact source/mapping through its
// own instance.

#include <filesystem>
#include <optional>
#include <string>

#include "waylaunch/config.h"
#include "waylaunch/matugen_theme.h"
#include "waylaunch/solar.h"

namespace waylaunch {

class ThemeManager {
  public:
    ThemeManager() = default;

    // Effective colors for the repo config: [theme] re-read when
    // config_path moves, mode resolved through the solar tracker
    // (mode=auto), matugen scheme overlaid when source=matugen. Cheap
    // enough to call every frame (mtime-gated).
    ColorConfig colors(Config& repo_config, const std::string& config_path);

    // True when the resolved colors changed since the previous poll()
    // (config edit, matugen edit, or solar flip; also true on the first
    // call). Drives needs_redraw_.
    bool poll(Config& repo_config, const std::string& config_path);

  private:
    // Re-read [theme] when the config file moves; returns a copy with the
    // solar-effective mode. Empty config_path keeps the running theme.
    ThemeConfig refresh(Config& repo_config, const std::string& config_path);

    MatugenTheme matugen_;
    solar_tracker solar_;
    std::optional<std::filesystem::file_time_type> config_mtime_;
    std::string last_effective_mode_;
    bool have_last_effective_mode_ = false;
};

} // namespace waylaunch
