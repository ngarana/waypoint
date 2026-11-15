#include "waylaunch/switcher/switcher_renderer.h"
#include <algorithm>
#include <glib.h>
#include <string>

namespace waylaunch {

void SwitcherRenderer::render(Renderer& renderer, const AppSwitcherManager& manager,
                              const Theme& theme, const SwitcherLayout& layout,
                              const ThumbnailCache* thumbnails) {
    if (!manager.is_visible() || manager.app_groups().empty() || layout.panel.w <= 0 ||
        layout.panel.h <= 0) {
        return;
    }

    const SwitcherRect& panel = layout.panel;
    if (renderer.has_backdrop()) {
        renderer.draw_backdrop(panel.x, panel.y, panel.w, panel.h, layout.corner_radius);
    }
    renderer.rounded_rect(
        panel.x, panel.y, panel.w, panel.h, layout.corner_radius,
        Color::from_rgba(theme.background.r, theme.background.g, theme.background.b, 0.82));
    if (cairo_t* cr = renderer.cr()) {
        Renderer::round_rect_path(cr, panel.x + 1, panel.y + 1, panel.w - 2, panel.h - 2,
                                  layout.corner_radius);
        cairo_set_source_rgba(cr, theme.border.r, theme.border.g, theme.border.b, 0.18);
        cairo_set_line_width(cr, 1.0);
        cairo_stroke(cr);
    }

    const auto& groups = manager.app_groups();
    size_t const count = std::min(groups.size(), layout.cards.size());
    for (size_t index = 0; index < count; ++index) {
        const AppGroup& group = groups[index];
        const SwitcherCardLayout& card = layout.cards[index];
        bool const selected = index == manager.selected_index();
        if (selected) {
            renderer.rounded_rect(
                card.card.x + 1, card.card.y + 1, card.card.w - 2, card.card.h - 2,
                std::max(0, layout.corner_radius - 3),
                Color::from_rgba(theme.accent.r, theme.accent.g, theme.accent.b, 0.13));
        } else {
            renderer.rounded_rect(card.card.x + 1, card.card.y + 1, card.card.w - 2,
                                  card.card.h - 2, std::max(0, layout.corner_radius - 3),
                                  Color::from_rgba(theme.background_alt.r, theme.background_alt.g,
                                                   theme.background_alt.b, 0.18));
        }

        std::string label = group.display_name.empty() ? "?" : group.display_name.substr(0, 1);
        if (card.header.w > 0) {
            renderer.draw_icon(card.icon.x, card.icon.y, card.icon.w, group.icon_name, label,
                               theme.accent);
            std::string title = group.display_name;
            uintptr_t const primary = group.primary_handle();
            for (const ToplevelWindow& window : group.windows) {
                if (window.handle_id == primary && !window.title.empty()) {
                    title = window.title;
                    break;
                }
            }
            gchar* escaped_title = g_markup_escape_text(title.c_str(), -1);
            RenderFontConfig font = theme.result_detail_font;
            font.size = 12.0;
            renderer.draw_markup(card.title.x, card.title.y, escaped_title ? escaped_title : "",
                                 font, theme.foreground, card.title.w, 1);
            g_free(escaped_title);
        }

        const ThumbnailCache::Entry* thumbnail =
            thumbnails ? thumbnails->get(group.primary_handle()) : nullptr;
        if (thumbnail && thumbnail->surface) {
            double const alpha = group.is_all_minimized() ? 0.5 : 1.0;
            renderer.draw_surface(thumbnail->surface.get(), card.thumb.x, card.thumb.y,
                                  card.thumb.w, card.thumb.h, std::min(12, card.thumb.h / 4),
                                  alpha);
        } else {
            renderer.rounded_rect(
                card.thumb.x, card.thumb.y, card.thumb.w, card.thumb.h,
                std::min(12, card.thumb.h / 4),
                Color::from_rgba(theme.accent.r, theme.accent.g, theme.accent.b, 0.14));
            int const icon_size = std::min(52, std::min(card.thumb.w, card.thumb.h) - 16);
            if (icon_size > 0) {
                renderer.draw_icon(card.thumb.x + ((card.thumb.w - icon_size) / 2),
                                   card.thumb.y + ((card.thumb.h - icon_size) / 2), icon_size,
                                   group.icon_name, label, theme.accent);
            }
        }

        if (group.windows.size() > 1) {
            std::string badge = "×" + std::to_string(group.windows.size());
            constexpr int badge_width = 38;
            constexpr int badge_height = 22;
            int const badge_x = card.thumb.x + card.thumb.w - badge_width - 8;
            int const badge_y = card.thumb.y + 8;
            renderer.rounded_rect(badge_x, badge_y, badge_width, badge_height, 11,
                                  Color::from_rgba(theme.background_alt.r, theme.background_alt.g,
                                                   theme.background_alt.b, 0.9));
            RenderFontConfig badge_font = theme.result_detail_font;
            badge_font.size = 11.0;
            renderer.draw_markup(badge_x + 4, badge_y + 3, badge, badge_font, theme.foreground,
                                 badge_width - 8, 1, true);
        }

        if (cairo_t* cr = renderer.cr()) {
            Renderer::round_rect_path(cr, card.card.x + 2, card.card.y + 2, card.card.w - 4,
                                      card.card.h - 4, std::max(0, layout.corner_radius - 4));
            if (selected) {
                cairo_set_source_rgba(cr, theme.accent.r, theme.accent.g, theme.accent.b, 0.95);
                cairo_set_line_width(cr, 3.0);
            } else {
                cairo_set_source_rgba(cr, theme.foreground.r, theme.foreground.g,
                                      theme.foreground.b, 0.32);
                cairo_set_line_width(cr, 1.25);
            }
            cairo_stroke(cr);
        }
    }
}

} // namespace waylaunch
