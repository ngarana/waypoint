// SystemStats.cpp - /proc sampler implementation.
#include "system/SystemStats.hpp"

#include <array>
#include <cstdio>
#include <sstream>

namespace qypr {

std::string SystemStats::readFile(const char* path) {
    // NOLINTNEXTLINE(cppcoreguidelines-owning-memory) // libc C boundary owns handle
    std::FILE* f = std::fopen(path, "re");
    if (!f) return {};
    std::string out;
    std::array<char, 4096> buf{};
    // Guard every read with the error/EOF state: the stream is never read
    // after it reports EOF or an error, so its position stays determinate.
    while (std::feof(f) == 0 && std::ferror(f) == 0) {
        const size_t n = std::fread(buf.data(), 1, buf.size(), f);
        if (n == 0) break;
        out.append(buf.data(), n);
    }
    // NOLINTNEXTLINE(cppcoreguidelines-owning-memory) // libc C boundary owns handle
    std::fclose(f);
    return out;
}

bool SystemStats::parseCpuLine(const std::string& line, uint64_t& busy, uint64_t& total) {
    std::istringstream is(line);
    std::string tag;
    is >> tag;
    if (tag != "cpu") return false;  // per-core lines ("cpu0") and others: ignore
    // user nice system idle iowait irq softirq steal guest guest_nice
    std::array<uint64_t, 10> v{};
    int count = 0;
    for (; count < 10 && (is >> v.at(static_cast<size_t>(count))); ++count) {}
    if (count < 4) return false;
    const uint64_t idle = v[3] + (count > 4 ? v[4] : 0);  // idle + iowait
    uint64_t sum = 0;
    for (int i = 0; i < count; ++i) sum += v.at(static_cast<size_t>(i));
    total = sum;
    busy = sum - idle;
    return true;
}

double SystemStats::parseMemUsedPercent(const std::string& meminfo) {
    uint64_t memTotal = 0;
    uint64_t memAvailable = 0;
    bool haveTotal = false;
    bool haveAvail = false;
    std::istringstream is(meminfo);
    std::string key;
    uint64_t val = 0;
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
        uint64_t busy = 0;
        uint64_t total = 0;
        if (parseCpuLine(first, busy, total)) {
            if (havePrev_ && total > prevTotal_) {
                const auto dTotal = static_cast<double>(total - prevTotal_);
                const auto dBusy = static_cast<double>(busy - prevBusy_);
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
