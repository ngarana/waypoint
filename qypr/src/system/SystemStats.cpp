// SystemStats.cpp - /proc sampler implementation.
#include "system/SystemStats.hpp"

#include <cstdio>
#include <sstream>

namespace qypr {

std::string SystemStats::readFile(const char* path) {
    std::FILE* f = std::fopen(path, "re");
    if (!f) return {};
    std::string out;
    char buf[4096];
    size_t n;
    while ((n = std::fread(buf, 1, sizeof(buf), f)) > 0) out.append(buf, n);
    std::fclose(f);
    return out;
}

bool SystemStats::parseCpuLine(const std::string& line, uint64_t& busy, uint64_t& total) {
    std::istringstream is(line);
    std::string tag;
    is >> tag;
    if (tag != "cpu") return false;  // per-core lines ("cpu0") and others: ignore
    // user nice system idle iowait irq softirq steal guest guest_nice
    uint64_t v[10] = {0};
    int count = 0;
    for (; count < 10 && (is >> v[count]); ++count) {}
    if (count < 4) return false;
    const uint64_t idle = v[3] + (count > 4 ? v[4] : 0);  // idle + iowait
    uint64_t sum = 0;
    for (int i = 0; i < count; ++i) sum += v[i];
    total = sum;
    busy = sum - idle;
    return true;
}

double SystemStats::parseMemUsedPercent(const std::string& meminfo) {
    uint64_t memTotal = 0, memAvailable = 0;
    bool haveTotal = false, haveAvail = false;
    std::istringstream is(meminfo);
    std::string key;
    uint64_t val;
    std::string unit;
    while (is >> key >> val) {
        std::getline(is, unit);  // consume the rest of the line ("kB")
        if (key == "MemTotal:") {
            memTotal = val;
            haveTotal = true;
        } else if (key == "MemAvailable:") {
            memAvailable = val;
            haveAvail = true;
        }
        if (haveTotal && haveAvail) break;
    }
    if (!haveTotal || !haveAvail || memTotal == 0) return -1.0;
    const uint64_t used = memTotal > memAvailable ? memTotal - memAvailable : 0;
    return 100.0 * static_cast<double>(used) / static_cast<double>(memTotal);
}

SysSample SystemStats::sample() {
    SysSample s;

    const std::string stat = readFile("/proc/stat");
    if (!stat.empty()) {
        const size_t nl = stat.find('\n');
        const std::string first = stat.substr(0, nl == std::string::npos ? stat.size() : nl);
        uint64_t busy = 0, total = 0;
        if (parseCpuLine(first, busy, total)) {
            if (havePrev_ && total > prevTotal_) {
                const double dTotal = static_cast<double>(total - prevTotal_);
                const double dBusy = static_cast<double>(busy - prevBusy_);
                s.cpuPercent = dTotal > 0 ? 100.0 * dBusy / dTotal : 0.0;
            }
            prevBusy_ = busy;
            prevTotal_ = total;
            havePrev_ = true;
            s.valid = true;
        }
    }

    const double mem = parseMemUsedPercent(readFile("/proc/meminfo"));
    if (mem >= 0) {
        s.memPercent = mem;
        s.valid = true;
    }
    return s;
}

}  // namespace qypr
