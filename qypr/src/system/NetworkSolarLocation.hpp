// NetworkSolarLocation.hpp - Bar-only, cached IP location lookup.
#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <thread>

#include "core/EventLoop.hpp"
#include "core/RetryTimer.hpp"
#include "core/SolarLocation.hpp"

namespace qypr {

class NetworkSolarLocation {
public:
    explicit NetworkSolarLocation(EventLoop& loop);
    ~NetworkSolarLocation();

    NetworkSolarLocation(const NetworkSolarLocation&) = delete;
    NetworkSolarLocation& operator=(const NetworkSolarLocation&) = delete;

    // Enable lookups only for auto palette mode. Manual coordinates suppress
    // network access; the lock process never constructs this backend.
    void configure(bool enabled, std::optional<SolarLocation> manualLocation = std::nullopt);

    const std::optional<SolarLocation>& location() const { return location_; }
    void setOnChange(std::function<void()> callback) { onChange_ = std::move(callback); }

private:
    void requestLookup();
    void onLookupComplete(uint64_t generation, std::optional<SolarLocation> location);
    void publish(std::optional<SolarLocation> location);
    void scheduleRefresh(int64_t delayMs);
    void cancelRefresh();

    EventLoop& loop_;
    RetryTimer retry_;
    int refreshTimerId_ = -1;
    std::thread worker_;
    std::shared_ptr<std::atomic<bool>> alive_;
    std::shared_ptr<std::atomic<bool>> cancelCurrent_;
    std::optional<SolarLocation> location_;
    std::optional<SolarLocation> manualLocation_;
    std::function<void()> onChange_;
    uint64_t generation_ = 0;
    bool enabled_ = false;
    bool inFlight_ = false;
    bool failureReported_ = false;
};

}  // namespace qypr
