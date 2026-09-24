// NetworkSolarLocation.cpp - Coarse IP lookup with bounded async HTTPS.
#include "system/NetworkSolarLocation.hpp"

#ifndef TESTING
#    include <curl/curl.h>
#endif

#include <algorithm>
#include <cstdio>
#include <ctime>
#include <mutex>
#include <string>
#include <system_error>

#include "core/SolarLocationCache.hpp"

namespace qypr {

namespace {

constexpr int64_t kCacheTtlSeconds = 604800LL;
constexpr int64_t kRetryInitialMs = 900000LL;
constexpr int64_t kRetryMaximumMs = 21600000LL;
#ifndef TESTING
constexpr size_t kMaximumResponseBytes = 128;
constexpr const char* kLookupUrl = "https://ipapi.co/latlong/";

size_t appendResponse(char* data, size_t size, size_t count, void* userData) {
    auto* body = static_cast<std::string*>(userData);
    if (size != 0 && count > kMaximumResponseBytes / size) { return 0; }
    const size_t bytes = size * count;
    if (bytes > kMaximumResponseBytes - std::min(body->size(), kMaximumResponseBytes)) { return 0; }
    body->append(data, bytes);
    return bytes;
}

int abortIfCancelled(void* userData, curl_off_t, curl_off_t, curl_off_t, curl_off_t) {
    const auto* cancelled = static_cast<const std::atomic<bool>*>(userData);
    return cancelled->load() ? 1 : 0;
}

std::optional<SolarLocation> lookupIpLocation(const std::atomic<bool>& cancelled) {
    static std::once_flag curlInitFlag;
    static CURLcode curlInitResult = CURLE_FAILED_INIT;
    std::call_once(curlInitFlag, [] { curlInitResult = curl_global_init(CURL_GLOBAL_DEFAULT); });
    if (curlInitResult != CURLE_OK || cancelled.load()) { return std::nullopt; }

    CURL* handle = curl_easy_init();
    if (handle == nullptr) { return std::nullopt; }
    std::string body;
    curl_easy_setopt(handle, CURLOPT_URL, kLookupUrl);
    curl_easy_setopt(handle, CURLOPT_PROTOCOLS_STR, "https");
    curl_easy_setopt(handle, CURLOPT_FOLLOWLOCATION, 0L);
    curl_easy_setopt(handle, CURLOPT_CONNECTTIMEOUT_MS, 2500L);
    curl_easy_setopt(handle, CURLOPT_TIMEOUT_MS, 5000L);
    curl_easy_setopt(handle, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(handle, CURLOPT_SSL_VERIFYPEER, 1L);
    curl_easy_setopt(handle, CURLOPT_SSL_VERIFYHOST, 2L);
    curl_easy_setopt(handle, CURLOPT_USERAGENT, "qypr-bar solar theme location");
    curl_easy_setopt(handle, CURLOPT_WRITEFUNCTION, &appendResponse);
    curl_easy_setopt(handle, CURLOPT_WRITEDATA, &body);
    curl_easy_setopt(handle, CURLOPT_XFERINFOFUNCTION, &abortIfCancelled);
    curl_easy_setopt(handle, CURLOPT_XFERINFODATA, &cancelled);
    curl_easy_setopt(handle, CURLOPT_NOPROGRESS, 0L);

    const CURLcode result = curl_easy_perform(handle);
    long status = 0;
    curl_easy_getinfo(handle, CURLINFO_RESPONSE_CODE, &status);
    curl_easy_cleanup(handle);
    if (result != CURLE_OK || status != 200 || cancelled.load()) { return std::nullopt; }
    return parseIpLocationResponse(body);
}
#else
std::optional<SolarLocation> lookupIpLocation(const std::atomic<bool>& cancelled) {
    (void)cancelled;
    return std::nullopt;
}
#endif

int64_t nowSeconds() {
    return static_cast<int64_t>(std::time(nullptr));
}

}  // namespace

NetworkSolarLocation::NetworkSolarLocation(EventLoop& loop)
    : loop_(loop),
      retry_(
          loop, [this] { requestLookup(); }, kRetryInitialMs, kRetryMaximumMs),
      alive_(std::make_shared<std::atomic<bool>>(true)) {}

NetworkSolarLocation::~NetworkSolarLocation() {
    alive_->store(false);
    ++generation_;
    if (cancelCurrent_) { cancelCurrent_->store(true); }
    retry_.cancel();
    cancelRefresh();
    if (worker_.joinable()) { worker_.join(); }
}

void NetworkSolarLocation::configure(bool enabled, std::optional<SolarLocation> manualLocation) {
    if (enabled_ == enabled && manualLocation_ == manualLocation) { return; }
    ++generation_;
    if (cancelCurrent_) { cancelCurrent_->store(true); }
    retry_.cancel();
    cancelRefresh();
    enabled_ = enabled;
    manualLocation_ = manualLocation;
    failureReported_ = false;

    if (!enabled_) {
        publish(std::nullopt);
        return;
    }
    if (manualLocation_) {
        const CachedSolarLocation cached{.location = *manualLocation_, .fetchedAt = nowSeconds()};
        if (!storeSolarLocationCache(cached)) {
            std::fprintf(stderr, "qypr-bar: could not persist manual solar location\n");
        }
        publish(manualLocation_);
        return;
    }

    const auto cached = loadSolarLocationCache();
    publish(cached.has_value() ? std::optional<SolarLocation>(cached->location) : std::nullopt);
    const int64_t now = nowSeconds();
    if (cached && solarLocationCacheFresh(*cached, now, kCacheTtlSeconds)) {
        const int64_t age = std::max<int64_t>(0, now - cached->fetchedAt);
        scheduleRefresh((kCacheTtlSeconds - age) * 1000);
    } else {
        requestLookup();
    }
}

void NetworkSolarLocation::requestLookup() {
    if (!enabled_ || manualLocation_ || inFlight_) { return; }
    if (worker_.joinable()) { worker_.join(); }

    inFlight_ = true;
    const uint64_t generation = generation_;
    auto cancel = std::make_shared<std::atomic<bool>>(false);
    cancelCurrent_ = cancel;
    const auto alive = alive_;
    try {
        worker_ = std::thread([this, alive, cancel, generation] {
            const auto result = lookupIpLocation(*cancel);
            loop_.post([this, alive, generation, result] {
                if (alive->load()) { onLookupComplete(generation, result); }
            });
        });
    } catch (const std::system_error&) {
        inFlight_ = false;
        if (!failureReported_) {
            std::fprintf(stderr, "qypr-bar: solar location lookup could not start; using cached "
                                 "location or fixed hours\n");
            failureReported_ = true;
        }
        retry_.schedule();
    }
}

void NetworkSolarLocation::onLookupComplete(uint64_t generation,
                                            std::optional<SolarLocation> location) {
    inFlight_ = false;
    if (worker_.joinable()) { worker_.join(); }
    cancelCurrent_.reset();

    if (generation != generation_) {
        if (enabled_ && !manualLocation_) { requestLookup(); }
        return;
    }
    if (!enabled_ || manualLocation_) { return; }

    if (!location) {
        if (!failureReported_) {
            std::fprintf(
                stderr,
                "qypr-bar: IP location unavailable; using cached location or fixed theme hours\n");
            failureReported_ = true;
        }
        retry_.schedule();
        return;
    }

    const CachedSolarLocation cached{.location = *location, .fetchedAt = nowSeconds()};
    if (!storeSolarLocationCache(cached)) {
        std::fprintf(stderr, "qypr-bar: could not persist solar location cache\n");
    }
    retry_.reset();
    failureReported_ = false;
    publish(location);
    scheduleRefresh(kCacheTtlSeconds * 1000);
}

void NetworkSolarLocation::publish(std::optional<SolarLocation> location) {
    if (location_ == location) { return; }
    location_ = location;
    if (onChange_) { onChange_(); }
}

void NetworkSolarLocation::scheduleRefresh(int64_t delayMs) {
    cancelRefresh();
    refreshTimerId_ = loop_.addTimer(std::max<int64_t>(delayMs, 1000), false, [this] {
        refreshTimerId_ = -1;
        requestLookup();
    });
}

void NetworkSolarLocation::cancelRefresh() {
    if (refreshTimerId_ < 0) { return; }
    loop_.removeTimer(refreshTimerId_);
    refreshTimerId_ = -1;
}

}  // namespace qypr
