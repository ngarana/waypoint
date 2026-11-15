#pragma once

#include "waylaunch/renderer.h"
#include "waylaunch/switcher/app_switcher_manager.h"
#include "waylaunch/switcher/switcher_layout.h"
#include "waylaunch/switcher/thumbnail.h"

namespace waylaunch {

class SwitcherRenderer {
  public:
    SwitcherRenderer() = default;

    static void render(Renderer& renderer, const AppSwitcherManager& manager, const Theme& theme,
                       const SwitcherLayout& layout, const ThumbnailCache* thumbnails);
};

} // namespace waylaunch
