// ClockIndicator.hpp - Status bar clock text indicator.
//
// Text-only (icon() is empty); formats its own time — shares no LockScreen
// widget code and ticks on StatusBar's timer, never LockScreen's.

#pragma once

#include "ui/statusbar/StatusIndicator.hpp"
#include <memory>
#include <string>
#include <vector>

namespace qypr {

class ClockIndicator : public StatusIndicator {
public:
    explicit ClockIndicator(const SystemBackends& backends);

    std::string icon() const override { return ""; }
    std::string label() const override;
    std::string tooltip() const override;
    double labelFontSize() const override;

    void poll(int64_t now) override;

    // Click the clock to drop a month calendar (with month navigation and, if
    // configured, secondary timezones). POSIX time only — no PIM/event source.
    bool hasDetailedView() const override { return true; }
    std::unique_ptr<DetailedPopover> createDetailedView() override;

private:
    std::string timeString() const;
    std::string dateString() const;

    // strftime patterns; config-overridable (`[clock] format`), else the
    // compiled defaults — so a config-less host (qypr-lock) is unaffected.
    std::string format_ = "%a %b %-d   %-I:%M %p";
    std::string tooltipFormat_ = "%A, %B %-d";
    // Optional `[clock] timezones` (IANA names, e.g. "Asia/Tokyo"); each shows
    // its current local time under the calendar. Empty by default.
    std::vector<std::string> timezones_;

    std::string cachedTime_;
    int64_t lastPoll_ = 0;
};

}  // namespace qypr
