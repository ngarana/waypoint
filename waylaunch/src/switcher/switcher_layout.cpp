#include "waylaunch/switcher/switcher_layout.h"
#include <algorithm>
#include <cmath>
#include <limits>

namespace waylaunch {

namespace {

constexpr int kPanelPadding = 24;
constexpr int kCardPadding = 10;
constexpr int kCardGap = 16;
constexpr int kRowGap = 16;
constexpr int kHeaderHeight = 28;
constexpr int kHeaderGap = 6;

double clamp_aspect(double aspect, double fallback) {
    if (!std::isfinite(aspect) || aspect <= 0) aspect = fallback;
    return std::clamp(aspect, 0.6, 2.2);
}

struct Row {
    std::vector<size_t> indices;
    int width = 0;
};

std::vector<Row> make_rows(const std::vector<double>& aspects, int preview_height,
                           int content_width) {
    std::vector<Row> rows;
    Row current;
    for (size_t i = 0; i < aspects.size(); ++i) {
        int const thumb_width =
            std::max(1, static_cast<int>(std::lround(aspects[i] * preview_height)));
        int const card_width = thumb_width + (2 * kCardPadding);
        int const next_width =
            current.indices.empty() ? card_width : current.width + kCardGap + card_width;
        if (!current.indices.empty() && next_width > content_width) {
            rows.push_back(std::move(current));
            current = {};
        }
        if (!current.indices.empty()) current.width += kCardGap;
        current.indices.push_back(i);
        current.width += card_width;
    }
    if (!current.indices.empty()) rows.push_back(std::move(current));
    return rows;
}

} // namespace

SwitcherLayout switcher_layout(const std::vector<SwitcherLayoutEntry>& entries, int screen_w,
                               int screen_h, const SwitcherLayoutConfig& config) {
    SwitcherLayout out;
    out.corner_radius = std::max(0, config.corner_radius);
    out.preview_height = std::max(64, config.preview_height);
    if (entries.empty() || screen_w <= 0 || screen_h <= 0) return out;

    double const fallback = static_cast<double>(screen_w) / screen_h;
    std::vector<double> aspects;
    aspects.reserve(entries.size());
    double max_aspect = 0.6;
    for (const auto& entry : entries) {
        double const aspect = clamp_aspect(entry.aspect, fallback);
        aspects.push_back(aspect);
        max_aspect = std::max(max_aspect, aspect);
    }

    int const max_panel_width = std::max(1, static_cast<int>(screen_w * 0.9));
    int const content_width = std::max(1, max_panel_width - (2 * kPanelPadding));
    int const width_limited_height =
        std::max(64, static_cast<int>((content_width - (2 * kCardPadding)) / max_aspect));
    int const requested_height =
        std::max(64, std::min(config.preview_height, width_limited_height));
    int const max_panel_height = std::max(1, static_cast<int>(screen_h * 0.8));
    std::vector<Row> rows;
    int preview_height = requested_height;
    int card_height = 0;
    int panel_height = std::numeric_limits<int>::max();
    for (; preview_height > 64; --preview_height) {
        rows = make_rows(aspects, preview_height, content_width);
        card_height = preview_height + (2 * kCardPadding);
        if (config.show_app_names) card_height += kHeaderHeight + kHeaderGap;
        panel_height = (2 * kPanelPadding) + (static_cast<int>(rows.size()) * card_height) +
                       (std::max(0, static_cast<int>(rows.size()) - 1) * kRowGap);
        if (panel_height <= max_panel_height) break;
    }
    if (preview_height == 64 || rows.empty()) {
        rows = make_rows(aspects, 64, content_width);
        preview_height = 64;
        card_height = preview_height + (2 * kCardPadding);
        if (config.show_app_names) card_height += kHeaderHeight + kHeaderGap;
        panel_height = (2 * kPanelPadding) + (static_cast<int>(rows.size()) * card_height) +
                       (std::max(0, static_cast<int>(rows.size()) - 1) * kRowGap);
    }

    int max_row_width = 0;
    for (const Row& row : rows) {
        out.columns_per_row.push_back(static_cast<int>(row.indices.size()));
        out.columns = std::max(out.columns, static_cast<int>(row.indices.size()));
        max_row_width = std::max(max_row_width, row.width);
    }
    int const panel_width = std::min(max_panel_width, max_row_width + (2 * kPanelPadding));
    out.panel = {
        .x = (screen_w - panel_width) / 2,
        .y = (screen_h - panel_height) / 2,
        .w = panel_width,
        .h = panel_height,
    };
    out.preview_height = preview_height;
    out.cards.resize(entries.size());

    int y = out.panel.y + kPanelPadding;
    for (const Row& row : rows) {
        int x = out.panel.x + ((out.panel.w - row.width) / 2);
        for (size_t index : row.indices) {
            int const thumb_width =
                std::max(1, static_cast<int>(std::lround(aspects[index] * preview_height)));
            int const card_width = thumb_width + (2 * kCardPadding);
            SwitcherCardLayout& card = out.cards[index];
            card.card = {.x = x, .y = y, .w = card_width, .h = card_height};
            int thumb_y = y + kCardPadding;
            if (config.show_app_names) {
                card.header = {
                    .x = x + kCardPadding,
                    .y = y + kCardPadding,
                    .w = card_width - (2 * kCardPadding),
                    .h = kHeaderHeight,
                };
                card.icon = {
                    .x = card.header.x,
                    .y = card.header.y + ((kHeaderHeight - 20) / 2),
                    .w = 20,
                    .h = 20,
                };
                card.title = {
                    .x = card.icon.x + 28,
                    .y = card.header.y,
                    .w = std::max(0, card.header.w - 28),
                    .h = card.header.h,
                };
                thumb_y += kHeaderHeight + kHeaderGap;
            }
            card.thumb = {
                .x = x + kCardPadding,
                .y = thumb_y,
                .w = thumb_width,
                .h = preview_height,
            };
            x += card_width + kCardGap;
        }
        y += card_height + kRowGap;
    }
    return out;
}

} // namespace waylaunch
