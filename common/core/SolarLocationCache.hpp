// SolarLocationCache.hpp - Shared private cache for manual or IP-derived solar coordinates.
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include "core/SolarLocation.hpp"

namespace qypr {

struct CachedSolarLocation {
    SolarLocation location;
    int64_t fetchedAt = 0;  // Unix seconds.
};

std::optional<SolarLocation> parseIpLocationResponse(std::string_view response);
std::optional<CachedSolarLocation> loadSolarLocationCache(const std::string& path = "");
bool storeSolarLocationCache(const CachedSolarLocation& cached, const std::string& path = "");
bool solarLocationCacheFresh(const CachedSolarLocation& cached, int64_t now, int64_t maxAgeSeconds);
std::string solarLocationCachePath();

}  // namespace qypr
