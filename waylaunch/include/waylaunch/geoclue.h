#pragma once

// Minimal org.freedesktop.GeoClue2 client: one city-accurate fix for the
// solar theme tracker. All bus names and IPC live in geoclue.cpp; this
// header stays dependency-free (no <systemd> include).
//
// GeoClue consent is enforced platform-side (geoclue.conf whitelist or agent
// prompt): start() simply reports false when denied or absent, and the theme
// falls back to dark. RequestedAccuracyLevel is always City — street-level
// and below are never asked for.

#include <optional>
#include <string>

namespace waylaunch {

struct geo_fix {
    double latitude = 0.0;
    double longitude = 0.0;
    double accuracy = 0.0; // radius of the fix, metres
};

class geoclue_client {
  public:
    geoclue_client() = default;
    ~geoclue_client();

    geoclue_client(const geoclue_client&) = delete;
    geoclue_client& operator=(const geoclue_client&) = delete;

    // Open the system bus, fetch our client, configure it (DesktopId, City
    // accuracy, coarse thresholds) and Start() it. Returns false when
    // GeoClue is absent or denies us. Idempotent.
    bool start();
    // Re-read the Location object; returns true while a fix is held
    // (unchanged fix included). False when never started or fix lost.
    bool refresh();

    const std::optional<geo_fix>& fix() const { return fix_; }

  private:
    void* bus_ = nullptr; // sd_bus*, pimpl'd to keep <systemd> out of the header
    std::string client_path_;
    std::optional<geo_fix> fix_;
};

} // namespace waylaunch
