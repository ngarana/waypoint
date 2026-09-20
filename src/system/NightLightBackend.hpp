// NightLightBackend.hpp - Screen colour-temperature shift via
// wlr-gamma-control-unstable-v1.
//
// Native Wayland protocol only (no D-Bus daemon, no compositor-specific
// API): per-output gamma ramps pushed once per toggle, never polled. When
// the toggle goes off the per-output gamma-control objects are destroyed
// and the compositor restores the original gamma tables automatically.
//
// Architecture notes:
// - No threads. The ramp is built in-process and sent via a memfd; the
//   compositor mmaps and applies it on its own schedule.
// - The control must outlive any wl_display flush, so destroy() is called
//   before roundtrip when the toggle goes off.
// - On hotplug the caller re-arms via setEnabled(true) — the new output
//   gets its own gamma-control, sized, and ramped. The existing outputs
//   keep what they have.

#pragma once

#include <cstdint>
#include <functional>
#include <vector>

struct wl_display;
struct wl_output;
struct zwlr_gamma_control_manager_v1;
struct zwlr_gamma_control_v1;

namespace qypr {

class NightLightBackend {
public:
    // Supported temperature range in Kelvin. Below 2700 K the screen gets
    // unreadably orange; above 6500 K (daylight) there's no visible shift.
    static constexpr uint32_t kMinTemperature = 2700;
    static constexpr uint32_t kMaxTemperature = 6500;

    NightLightBackend() = default;
    ~NightLightBackend();

    NightLightBackend(const NightLightBackend&) = delete;
    NightLightBackend& operator=(const NightLightBackend&) = delete;

    // Supplied by BarApp after BarDisplay::connect() (much like IdleInhibitor):
    // manager from BarDisplay::gammaControlManager(), the wl_display so we
    // can roundtrip for gamma_size, and the output list to iterate.
    void init(zwlr_gamma_control_manager_v1* mgr, wl_display* display);

    // The borrowed outputs (from BarDisplay::outputs()). Re-seeding tears down
    // and rebuilds per-output state for any that vanished. Safe to call once
    // after init() and again on hotplug; the caller keeps ownership of the
    // wl_output lifetimes.
    void setOutputs(const std::vector<wl_output*>& outputs);

    bool available() const { return mgr_ != nullptr && display_ != nullptr; }
    bool enabled() const { return enabled_; }

    // True when at least one output has a live gamma-control applied. Until
    // gamma_size arrives we hold the control but have not pushed a ramp; the
    // QS toggle and indicator read enabled(), this read active().
    bool active() const;

    void setEnabled(bool on);
    void toggle() { setEnabled(!enabled_); }

    // Colour temperature in Kelvin. Setting the temperature also arms the
    // backend (calls setEnabled(true)) so the slider can drive it directly.
    uint32_t temperature() const { return temperature_; }
    void setTemperature(uint32_t k);

    // Slider helpers: 0.0 = off (6500 K, daylight), 1.0 = warmest (2700 K).
    double sliderValue() const;
    void setSliderValue(double v);

    void setOnChange(std::function<void()> cb) { onChange_ = std::move(cb); }

    // Public so the file-scope listener table can bind them.
    static void onGammaSize(void* data, zwlr_gamma_control_v1*, uint32_t size);
    static void onFailed(void* data, zwlr_gamma_control_v1*);

private:
    struct PerOutput {
        wl_output* output = nullptr;
        zwlr_gamma_control_v1* control = nullptr;
        uint32_t rampSize = 0;
        bool failed = false;
    };

    void armAll();
    void applyRamp(PerOutput& p);
    void teardownOutput(PerOutput& p);
    void fireChange();

    zwlr_gamma_control_manager_v1* mgr_ = nullptr;
    wl_display* display_ = nullptr;
    std::vector<PerOutput> outputs_;
    bool enabled_ = false;
    uint32_t temperature_ = 4000;  // Kelvin, default warm-ish
    std::function<void()> onChange_;
};

}  // namespace qypr
