// SystemMonitorIndicator.hpp - Compact CPU/RAM meters (Phase 15).
//
// A text/meter indicator that re-samples /proc on the bar's existing 1s poll()
// tick (the sanctioned timer exception — CPU/RAM have no push source). Purely
// opt-in: it stays hidden unless a config is present and lists it, so it never
// appears on the lock screen or a config-less bar.
#pragma once

#include <string>
#include <vector>

#include "system/SystemStats.hpp"
#include "ui/statusbar/StatusIndicator.hpp"

namespace qypr {

class SystemMonitorIndicator : public StatusIndicator {
public:
    explicit SystemMonitorIndicator(const SystemBackends& backends);

    std::string icon() const override { return ""; }  // custom multi-metric draw
    std::string tooltip() const override;

    double measureWidth(Painter& p) override;
    void draw(Painter& p, int64_t now) override;

    void poll(int64_t now) override;

private:
    enum class Metric { Cpu, Mem };
    struct Seg {
        Metric metric;
        std::string glyph;
        double value = 0.0;  // 0-100
    };

    SystemStats stats_;
    std::vector<Seg> segs_;  // configured metric order
    int64_t intervalMs_ = 2000;
    int64_t lastSample_ = 0;
    bool hasConfig_ = false;
};

}  // namespace qypr
