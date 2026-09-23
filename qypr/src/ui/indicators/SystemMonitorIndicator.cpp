// SystemMonitorIndicator.cpp - Compact CPU/RAM meters implementation.
#include "ui/indicators/SystemMonitorIndicator.hpp"

#include <algorithm>
#include <cmath>

#include "core/Config.hpp"
#include "core/Types.hpp"
#include "render/Painter.hpp"
#include "ui/Theme.hpp"
#include "ui/statusbar/IndicatorRegistry.hpp"

namespace qypr {

namespace {
constexpr const char* kCpuGlyph = "󰻠";  // nf-md-cpu_64_bit
constexpr const char* kMemGlyph = "󰍛";  // nf-md-memory
constexpr double kIconPx = 15.0;
constexpr double kGap = 4.0;      // icon → percent
constexpr double kSegGap = 12.0;  // between metrics
constexpr double kSidePad = 6.0;

Color loadColor(const theme::State& theme, double pct) {
    if (pct >= 85.0) return theme.colors.error;
    if (pct >= 60.0) return theme.colors.warning;
    return theme.colors.text;
}

std::string pctText(double v) {
    return std::to_string(static_cast<int>(std::lround(v))) + "%";
}
}  // namespace

SystemMonitorIndicator::SystemMonitorIndicator(const SystemBackends& backends)
    : StatusIndicator("system-monitor", Zone::Right, 150),
      hasConfig_(backends.config != nullptr) {

    // Opt-in only: no config (qypr-lock, config-less bar) → never shown, even
    // though the registry constructs it for the compiled-default bar.
    visible = hasConfig_;

    std::vector<std::string> metrics = {"cpu", "mem"};
    if (backends.config) {
        metrics = backends.config->getList("system-monitor", "metrics", metrics);
        intervalMs_ = std::max(500, backends.config->getInt("system-monitor", "interval", 2000));
    }
    for (const auto& m : metrics) {
        if (m == "cpu")
            segs_.push_back({Metric::Cpu, kCpuGlyph, 0.0});
        else if (m == "mem" || m == "ram")
            segs_.push_back({Metric::Mem, kMemGlyph, 0.0});
    }
    if (segs_.empty()) visible = false;  // a config that selected nothing valid
}

std::string SystemMonitorIndicator::tooltip() const {
    std::string t;
    for (const auto& s : segs_) {
        if (!t.empty()) t += "   ";
        t += (s.metric == Metric::Cpu ? "CPU " : "RAM ") + pctText(s.value);
    }
    return t.empty() ? "System monitor" : t;
}

void SystemMonitorIndicator::poll(int64_t now) {
    if (!visible) return;
    if (lastSample_ != 0 && now - lastSample_ < intervalMs_) return;
    lastSample_ = now;
    const SysSample s = stats_.sample();
    if (!s.valid) return;
    for (auto& seg : segs_) { seg.value = seg.metric == Metric::Cpu ? s.cpuPercent : s.memPercent; }
}

double SystemMonitorIndicator::measureWidth(Painter& p) {
    if (!visible || segs_.empty()) return 0;
    double w = 2 * kSidePad;
    TextStyle icon{theme().font.iconFamily, kIconPx, PANGO_WEIGHT_NORMAL, theme().colors.text};
    TextStyle txt{theme().font.family, 13.0, PANGO_WEIGHT_NORMAL, theme().colors.text};
    for (size_t i = 0; i < segs_.size(); ++i) {
        w += p.measureText(segs_[i].glyph, icon).w + kGap +
             p.measureText(pctText(segs_[i].value), txt).w;
        if (i + 1 < segs_.size()) w += kSegGap;
    }
    return w;
}

void SystemMonitorIndicator::draw(Painter& p, int64_t now) {
    if (!visible || segs_.empty()) return;

    // Hover pill + focus ring, mirroring the base indicator chrome.
    const double alpha = hoverAlpha_.value(now);
    if (alpha > 0.01) {
        Rect r = bounds;
        r.y += 2.0;
        r.h -= 4.0;
        p.fillRoundedRect(r, 8.0,
                          theme().colors.glassHover.withAlpha(alpha * theme().colors.glassHover.a));
    }
    if (focused) {
        Rect r = bounds;
        r.y += 1.0;
        r.h -= 2.0;
        p.strokeRoundedRect(r, 8.0, theme().colors.primary, 1.5);
    }

    double x = bounds.x + kSidePad;
    for (const auto& s : segs_) {
        const Color c = loadColor(theme(), s.value);
        TextStyle icon{theme().font.iconFamily, kIconPx, PANGO_WEIGHT_NORMAL, c};
        const Size isz = p.measureText(s.glyph, icon);
        p.drawText(x, bounds.y + (bounds.h - isz.h) / 2.0, s.glyph, icon);
        x += isz.w + kGap;

        TextStyle txt{theme().font.family, 13.0, PANGO_WEIGHT_NORMAL, c};
        const std::string t = pctText(s.value);
        const Size tsz = p.measureText(t, txt);
        p.drawText(x, bounds.y + (bounds.h - tsz.h) / 2.0, t, txt);
        x += tsz.w + kSegGap;
    }
}

REGISTER_INDICATOR("system-monitor", Zone::Right, 150, SystemMonitorIndicator)

}  // namespace qypr
