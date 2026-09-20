// WorkspacesIndicator.hpp - Compositor workspaces widget (ext-workspace-v1).
//
// Session-sensitive: hidden while the screen is locked (see
// StatusIndicator::sensitive / StatusBar::setSessionContentVisible). Renders a
// pill per workspace, the active one accented, and switches workspace on click.
//
// Accordion behaviour: collapsed by default showing the active workspace number
// with a superscript total count; expands on hover to reveal all workspace pills.
#pragma once

#include <string>
#include <vector>

#include "system/WorkspaceBackend.hpp"  // WorkspaceSnapshot
#include "ui/statusbar/StatusIndicator.hpp"

namespace qypr {

class WorkspacesIndicator : public StatusIndicator {
public:
    explicit WorkspacesIndicator(const SystemBackends& backends);

    std::string icon() const override { return ""; }  // custom pill draw
    std::string tooltip() const override;
    bool sensitive() const override { return true; }

    double measureWidth(Painter& p) override;
    void draw(Painter& p, int64_t now) override;
    void onBackendUpdate() override;
    bool onClick(double x, double y) override;
    bool animating(int64_t now) const override;

private:
    double pillWidth(Painter& p, const std::string& name) const;
    double measureCollapsedWidth(Painter& p) const;
    double measureExpandedWidth(Painter& p) const;
    void drawCollapsed(Painter& p);

    WorkspaceBackend* backend_ = nullptr;
    WorkspaceSnapshot snap_;

    // Per-pill x-extents from the last draw, for click hit-testing.
    struct Hit {
        double x0, x1;
        std::string name;
    };
    std::vector<Hit> hits_;

    // Accordion animation state.
    Animated expandProgress_{0.0};

    // Collapsed layout constants.
    static constexpr double kCollapsedPadX = 8.0;
    static constexpr double kCollapsedPillH = 20.0;
    static constexpr double kSuperscriptSize = 9.0;
    static constexpr double kSuperscriptOffsetX = 2.0;
    static constexpr double kSuperscriptOffsetY = -5.0;
};

}  // namespace qypr
