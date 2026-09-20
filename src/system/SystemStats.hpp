// SystemStats.hpp - Cheap /proc sampler for the system-monitor indicator.
//
// The one place a timer tick is legitimate (design doc): CPU/RAM have no push
// source, so the indicator re-samples on the bar's existing 1s poll() tick — no
// new wakeups, no blocking D-Bus. The parse helpers are pure and static so they
// unit-test without touching the real /proc.
#pragma once

#include <cstdint>
#include <string>

namespace qypr {

struct SysSample {
    bool valid = false;
    double cpuPercent = 0.0;  // busy % since the previous sample()
    double memPercent = 0.0;  // used = (MemTotal - MemAvailable) / MemTotal
};

class SystemStats {
public:
    // Reads /proc/stat + /proc/meminfo. CPU is the busy fraction since the last
    // call (the first call has no baseline → cpuPercent 0 but valid).
    SysSample sample();

    // --- pure helpers (static; unit-tested without /proc) ---
    // Parse the aggregate "cpu ..." line into busy + total jiffies. False if the
    // line is not a cpu total line.
    static bool parseCpuLine(const std::string& line, uint64_t& busy, uint64_t& total);
    // Used-memory percent from a /proc/meminfo body. Prefers MemAvailable;
    // returns -1 when the required fields are absent.
    static double parseMemUsedPercent(const std::string& meminfo);

private:
    static std::string readFile(const char* path);

    uint64_t prevBusy_ = 0;
    uint64_t prevTotal_ = 0;
    bool havePrev_ = false;
};

}  // namespace qypr
