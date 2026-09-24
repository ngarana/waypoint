// SolarLocationCache.cpp - Validated parsing and private persistence.
#include "core/SolarLocationCache.hpp"

#include <cerrno>
#include <charconv>
#include <cmath>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <fcntl.h>
#include <iomanip>
#include <limits>
#include <sstream>
#include <string>
#include <system_error>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

#include <fstream>

namespace qypr {

namespace {

bool parseNumber(std::string_view text, double* value) {
    while (!text.empty() && (text.front() == ' ' || text.front() == '\t' || text.front() == '\r' ||
                             text.front() == '\n')) {
        text.remove_prefix(1);
    }
    while (!text.empty() && (text.back() == ' ' || text.back() == '\t' || text.back() == '\r' ||
                             text.back() == '\n')) {
        text.remove_suffix(1);
    }
    if (text.empty()) { return false; }
    const auto result = std::from_chars(text.data(), text.data() + text.size(), *value);
    return result.ec == std::errc{} && result.ptr == text.data() + text.size() &&
           std::isfinite(*value);
}

std::string cacheDirectory() {
    if (const char* xdg = std::getenv("XDG_CACHE_HOME"); xdg != nullptr && *xdg != '\0') {
        return std::string(xdg) + "/qypr";
    }
    if (const char* home = std::getenv("HOME"); home != nullptr && *home != '\0') {
        return std::string(home) + "/.cache/qypr";
    }
    return ".cache/qypr";
}

bool ensureCacheDirectory(const std::string& directory) {
    std::error_code error;
    std::filesystem::create_directories(directory, error);
    if (error) { return false; }
    struct stat info{};
    return ::stat(directory.c_str(), &info) == 0 && S_ISDIR(info.st_mode);
}

bool writePrivateAtomic(const std::string& path, std::string_view body) {
    const size_t slash = path.find_last_of('/');
    const std::string directory = slash == std::string::npos ? "." : path.substr(0, slash);
    if (!ensureCacheDirectory(directory)) { return false; }

    std::string temporary = path + ".tmp.XXXXXX";
    std::vector<char> tempName(temporary.begin(), temporary.end());
    tempName.push_back('\0');
    const int fd = ::mkstemp(tempName.data());
    if (fd < 0) { return false; }
    bool ok = ::fchmod(fd, 0600) == 0;
    size_t written = 0;
    while (ok && written < body.size()) {
        const ssize_t count = ::write(fd, body.data() + written, body.size() - written);
        if (count < 0 && errno == EINTR) { continue; }
        if (count <= 0) {
            ok = false;
            break;
        }
        written += static_cast<size_t>(count);
    }
    if (ok) { ok = ::fsync(fd) == 0; }
    if (::close(fd) != 0) { ok = false; }
    if (ok) { ok = ::rename(tempName.data(), path.c_str()) == 0; }
    if (!ok) { ::unlink(tempName.data()); }
    return ok;
}

}  // namespace

std::optional<SolarLocation> parseIpLocationResponse(std::string_view response) {
    const size_t comma = response.find(',');
    if (comma == std::string_view::npos ||
        response.find(',', comma + 1) != std::string_view::npos) {
        return std::nullopt;
    }
    SolarLocation location;
    if (!parseNumber(response.substr(0, comma), &location.latitude) ||
        !parseNumber(response.substr(comma + 1), &location.longitude) ||
        !validSolarLocation(location)) {
        return std::nullopt;
    }
    return location;
}

std::string solarLocationCachePath() {
    return cacheDirectory() + "/solar-location";
}

std::optional<CachedSolarLocation> loadSolarLocationCache(const std::string& requestedPath) {
    const std::string path = requestedPath.empty() ? solarLocationCachePath() : requestedPath;
    std::ifstream input(path);
    int version = 0;
    CachedSolarLocation cached;
    if (!(input >> version >> cached.location.latitude >> cached.location.longitude >>
          cached.fetchedAt) ||
        version != 1 || !validSolarLocation(cached.location) || cached.fetchedAt <= 0) {
        return std::nullopt;
    }
    char extra = '\0';
    if (input >> extra) { return std::nullopt; }
    const auto now = static_cast<int64_t>(std::time(nullptr));
    if (cached.fetchedAt > now + 300) { return std::nullopt; }
    return cached;
}

bool storeSolarLocationCache(const CachedSolarLocation& cached, const std::string& requestedPath) {
    if (!validSolarLocation(cached.location) || cached.fetchedAt <= 0) { return false; }
    const std::string path = requestedPath.empty() ? solarLocationCachePath() : requestedPath;
    std::ostringstream body;
    body << "1 " << std::setprecision(std::numeric_limits<double>::max_digits10)
         << cached.location.latitude << ' ' << cached.location.longitude << ' ' << cached.fetchedAt
         << '\n';
    return writePrivateAtomic(path, body.str());
}

bool solarLocationCacheFresh(const CachedSolarLocation& cached, int64_t now,
                             int64_t maxAgeSeconds) {
    return validSolarLocation(cached.location) && cached.fetchedAt > 0 &&
           cached.fetchedAt <= now + 300 && maxAgeSeconds >= 0 &&
           now - cached.fetchedAt <= maxAgeSeconds;
}

}  // namespace qypr
