// PagerIndicator.hpp - The merged workspace pager + taskbar module.
//
// One module that renders the session as a row of workspace *clusters*: every
// workspace is a rounded chip holding the app icons of the toplevels believed
// to live there (inferred by SessionMapper from focus correlation — see
// system/SessionMapper.hpp). The active workspace is accented and, collapsed,
// is the only one that shows its name; hovering accordion-expands every chip
// to reveal names and more icons (the WorkspacesIndicator gesture).
//
// Interaction:
//   left click  — an app icon focuses that window (switching workspace with
//                 it); a chip's body activates its workspace
//   middle      — on an icon: close the window
//   right       — on an icon: toggle minimize
//   scroll      — cycle workspaces
//
// Graceful degradation (never two modules for one job):
//   no ext-workspace-v1        → a flat icon taskbar (the old taskbar)
//   no foreign-toplevel        → plain workspace pills (the old workspaces)
//   neither                    → hidden
//
// Session-sensitive: hidden while locked, like its two ancestors.
#pragma once

#include <string>
#include <vector>

#include "system/SessionMapper.hpp"
#include "system/ToplevelBackend.hpp"   // ToplevelSnapshot
#include "system/WorkspaceBackend.hpp"  // WorkspaceSnapshot
#include "ui/statusbar/StatusIndicator.hpp"

namespace qypr {

class PagerIndicator : public StatusIndicator {
public:
    explicit PagerIndicator(const SystemBackends& backends);

    std::string icon() const override { return ""; }  // custom cluster draw
    std::string tooltip() const override;
    bool sensitive() const override { return true; }

    double measureWidth(Painter& p) override;
    void draw(Painter& p, int64_t now) override;
    void onBackendUpdate() override;
    bool onClick(double x, double y) override;
    bool onSecondaryClick(double x, double y) override;
    bool onMiddleClick(double x, double y) override;
    bool onScroll(double dx, double dy, double x, double y) override;
    bool animating(int64_t now) const override;

private:
    // One clickable region recorded during draw (x-extents suffice in a
    // horizontal strip). winId == 0 means the chip body itself.
    struct Hit {
        double x0, x1;
        std::string ws;
        uint64_t win = 0;
    };

    // Per-state layout metrics (collapsed ↔ expanded are lerped by t).
    struct Metrics {
        double iconPx, chipPadX, iconGap, chipGap, chipH, fontPx, badgePx, sidePad;
    };
    static Metrics metricsFor(double t);
    // Natural width of one cluster under `m`. `expanded` selects what is shown
    // (names on every chip) independently of pixel sizes, mirroring the
    // threshold-draw/lerped-reserve behaviour of the old accordion.
    static double clusterWidth(Painter& p, const SessionCluster& c, const Metrics& m,
                               bool expanded);
    double measureState(Painter& p, bool expanded) const;

    void drawChip(Painter& p, double x, const SessionCluster& c, const Metrics& m, bool expanded);
    void drawLooseIcons(Painter& p, double x, const Metrics& m);

    WorkspaceBackend* ws_ = nullptr;
    ToplevelBackend* tl_ = nullptr;
    WorkspaceSnapshot snapWs_;
    ToplevelSnapshot snapTl_;
    SessionMapper mapper_;

    std::vector<Hit> hits_;

    // Accordion animation state (hover expands the clusters).
    Animated expandProgress_{0.0};

    static constexpr size_t kMaxIconsCollapsed = 3;
    static constexpr size_t kMaxIconsExpanded = 5;
};

}  // namespace qypr
