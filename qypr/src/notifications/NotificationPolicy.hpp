// NotificationPolicy.hpp - Notification privacy policy (bus-free).
//
// Sensitive-app list (path-injectable, so tests use temp files) and the
// Desktop Notifications hint fold. No sd-bus, no filesystem globals: one
// instance per monitor, loaded once.

#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace qypr {

struct NotifyHints {
    uint8_t urgency = 1;
    bool transient = false;
    bool sensitive = false;
    std::string desktopEntry;
};

// Fold one hint-dict entry into the accumulator; unknown keys return the
// input unchanged. `numeric` selects the num/str reading (see readVariant).
NotifyHints applyHint(const std::string& key, bool numeric, uint64_t num, const std::string& str,
                      NotifyHints h);

class NotificationPolicy {
public:
    static std::vector<std::string> defaultSensitiveApps();

    // Load the sensitive-app list from `path`; a missing file falls back to
    // the defaults. Loads once — repeated calls are no-ops.
    void load(const std::string& path);
    // Case-folded substring match against the loaded list.
    bool isSensitiveApp(const std::string& app) const;

private:
    std::vector<std::string> sensitiveApps_;
    bool loaded_ = false;
};

}  // namespace qypr
