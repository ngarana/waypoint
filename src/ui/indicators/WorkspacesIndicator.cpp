// WorkspacesIndicator.cpp - Workspaces bar widget implementation.
#include "ui/indicators/WorkspacesIndicator.hpp"

#include "render/Painter.hpp"
#include "system/WorkspaceBackend.hpp"
#include "ui/Theme.hpp"
#include "ui/statusbar/IndicatorRegistry.hpp"

namespace qypr {

namespace {
constexpr double kPillPadX = 9.0;  // horizontal padding inside each pill
constexpr double kPillGap = 5.0;   // between pills
constexpr double kSidePad = 6.0;   // indicator edge padding
constexpr double kPillH = 24.0;    // pill height (within the 36px bar)
constexpr double kFont = 13.0;
constexpr double kExpandDuration = 200.0;  // ms
}  // namespace

WorkspacesIndicator::WorkspacesIndicator(const SystemBackends& backends)
    : StatusIndicator("workspaces", Zone::Left, -100),
      backend_(backends.workspace) {
    visible = false;  // hidden until the backend reports workspaces
}

std::string WorkspacesIndicator::tooltip() const {
    for (const auto& w : snap_.workspaces) {
        if (w.active) return "Workspace " + w.name;
    }
    return "Workspaces";
}

double WorkspacesIndicator::pillWidth(Painter& p, const std::string& name) const {
    TextStyle st{theme::font::family, kFont, PANGO_WEIGHT_MEDIUM, theme::color::text};
    return p.measureText(name, st).w + 2 * kPillPadX;
}

double WorkspacesIndicator::measureCollapsedWidth(Painter& p) const {
    if (snap_.workspaces.empty()) return 0;

    // Find the active workspace name.
    std::string activeName;
    for (const auto& w : snap_.workspaces) {
        if (w.active) {
            activeName = w.name;
            break;
        }
    }
    if (activeName.empty()) activeName = snap_.workspaces.front().name;

    // Active pill width.
    TextStyle st{theme::font::family, kFont, PANGO_WEIGHT_MEDIUM, theme::color::text};
    double pw = p.measureText(activeName, st).w + 2 * kCollapsedPadX;

    // Superscript total count (only when >1 workspace).
    if (snap_.workspaces.size() > 1) {
        TextStyle supSt{theme::font::family, kSuperscriptSize, PANGO_WEIGHT_NORMAL,
                        theme::color::textSubtle};
        std::string count = std::to_string(snap_.workspaces.size());
        double supW = p.measureText(count, supSt).w;
        pw += kSuperscriptOffsetX + supW;
    }

    return pw;
}

double WorkspacesIndicator::measureExpandedWidth(Painter& p) const {
    if (snap_.workspaces.empty()) return 0;
    double w = 2 * kSidePad;
    for (size_t i = 0; i < snap_.workspaces.size(); ++i) {
        if (i) w += kPillGap;
        w += pillWidth(p, snap_.workspaces[i].name);
    }
    return w;
}

double WorkspacesIndicator::measureWidth(Painter& p) {
    if (snap_.workspaces.empty()) return 0;
    double t = expandProgress_.value(nowMs());
    return lerp(measureCollapsedWidth(p), measureExpandedWidth(p), t);
}

void WorkspacesIndicator::drawCollapsed(Painter& p) {
    if (snap_.workspaces.empty()) return;

    // Find the active workspace.
    const WorkspaceInfo* active = &snap_.workspaces.front();
    for (const auto& w : snap_.workspaces) {
        if (w.active) {
            active = &w;
            break;
        }
    }

    // Measure the active pill.
    TextStyle st{theme::font::family, kFont, PANGO_WEIGHT_MEDIUM, theme::color::background};
    Size ts = p.measureText(active->name, st);
    double pw = ts.w + 2 * kCollapsedPadX;
    double pillY = bounds.y + (bounds.h - kCollapsedPillH) / 2.0;

    // Draw the active pill.
    p.fillRoundedRect({bounds.x, pillY, pw, kCollapsedPillH}, kCollapsedPillH / 2.0,
                      theme::color::primary);
    double tx = bounds.x + (pw - ts.w) / 2.0;
    double ty = bounds.y + (bounds.h - ts.h) / 2.0;
    p.drawText(tx, ty, active->name, st, HAlign::Left);

    // Superscript total count (only when >1 workspace).
    if (snap_.workspaces.size() > 1) {
        TextStyle supSt{theme::font::family, kSuperscriptSize, PANGO_WEIGHT_NORMAL,
                        theme::color::textSubtle};
        std::string count = std::to_string(snap_.workspaces.size());
        double sx = bounds.x + pw + kSuperscriptOffsetX;
        double sy = pillY + kSuperscriptOffsetY;
        p.drawText(sx, sy, count, supSt, HAlign::Left);
    }

    // Hit-test: entire collapsed pill is a single hit (no workspace switch).
    hits_.clear();
    hits_.push_back({bounds.x, bounds.x + pw, active->name});
}

void WorkspacesIndicator::draw(Painter& p, int64_t now) {
    if (!visible) return;

    // Drive the accordion animation based on hover state.
    double target = hovered ? 1.0 : 0.0;
    expandProgress_.animateTo(target, kExpandDuration, hovered ? ease::outBack : ease::inOutQuad);

    double t = expandProgress_.value(now);

    // Threshold: below 0.5 draw collapsed, above draw expanded.
    if (t < 0.5) {
        drawCollapsed(p);
    } else {
        // Expanded state: draw all pills.
        hits_.clear();
        double x = bounds.x + kSidePad;
        double pillY = bounds.y + (bounds.h - kPillH) / 2.0;
        for (const auto& w : snap_.workspaces) {
            double pw = pillWidth(p, w.name);

            Color txtColor;
            bool onFill = false;
            if (w.active) {
                p.fillRoundedRect({x, pillY, pw, kPillH}, kPillH / 2.0, theme::color::primary);
                txtColor = theme::color::background;
                onFill = true;
            } else if (w.urgent) {
                p.fillRoundedRect({x, pillY, pw, kPillH}, kPillH / 2.0,
                                  theme::color::warning.withAlpha(0.28));
                txtColor = theme::color::warning;
            } else {
                txtColor = theme::color::textSubtle;
            }

            TextStyle st{theme::font::family, kFont, PANGO_WEIGHT_MEDIUM, txtColor};
            Size ts = p.measureText(w.name, st);
            double tx = x + (pw - ts.w) / 2.0;
            double ty = bounds.y + (bounds.h - ts.h) / 2.0;
            if (onFill) {
                p.drawText(tx, ty, w.name, st, HAlign::Left);
            } else {
                p.drawTextShadowed(tx, ty, w.name, st, HAlign::Left, theme::effects::shadowOpacity,
                                   theme::effects::shadowOffset);
            }

            hits_.push_back({x, x + pw, w.name});
            x += pw + kPillGap;
        }
    }
}

void WorkspacesIndicator::onBackendUpdate() {
    if (!backend_) {
        visible = false;
        return;
    }
    snap_ = backend_->snapshot();
    visible = snap_.available && !snap_.workspaces.empty();
}

bool WorkspacesIndicator::onClick(double x, double y) {
    (void)y;
    if (!backend_) return false;
    for (const auto& h : hits_) {
        if (x >= h.x0 && x <= h.x1) {
            backend_->activate(h.name);
            return true;
        }
    }
    return false;
}

bool WorkspacesIndicator::animating(int64_t now) const {
    return expandProgress_.active(now);
}

REGISTER_INDICATOR("workspaces", Zone::Left, -100, WorkspacesIndicator)

}  // namespace qypr
