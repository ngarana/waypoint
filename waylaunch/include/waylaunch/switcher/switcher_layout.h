#pragma once

#include <vector>

namespace waylaunch {

struct SwitcherRect {
    int x = 0;
    int y = 0;
    int w = 0;
    int h = 0;
};

struct SwitcherLayoutEntry {
    double aspect = 0.0;
};

struct SwitcherCardLayout {
    SwitcherRect card;
    SwitcherRect header;
    SwitcherRect icon;
    SwitcherRect title;
    SwitcherRect thumb;
};

struct SwitcherLayoutConfig {
    int preview_height = 160;
    int corner_radius = 20;
    bool show_app_names = true;
};

struct SwitcherLayout {
    SwitcherRect panel;
    std::vector<SwitcherCardLayout> cards;
    std::vector<int> columns_per_row;
    int columns = 1;
    int preview_height = 160;
    int corner_radius = 20;
};

SwitcherLayout switcher_layout(const std::vector<SwitcherLayoutEntry>& entries, int screen_w,
                               int screen_h, const SwitcherLayoutConfig& config);

} // namespace waylaunch
