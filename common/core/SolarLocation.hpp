// SolarLocation.hpp - Coarse coordinates used by local sunrise/sunset math.
#pragma once

namespace qypr {

struct SolarLocation {
    double latitude = 0.0;
    double longitude = 0.0;

    bool operator==(const SolarLocation&) const = default;
};

inline bool validSolarLocation(const SolarLocation& location) {
    return location.latitude >= -90.0 && location.latitude <= 90.0 &&
           location.longitude >= -180.0 && location.longitude <= 180.0;
}

}  // namespace qypr
