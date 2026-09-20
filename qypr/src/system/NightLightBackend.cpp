// NightLightBackend.cpp - wlr-gamma-control implementation.
#include "system/NightLightBackend.hpp"

#include "wlr-gamma-control-unstable-v1-client-protocol.h"

#include <sys/mman.h>
#include <sys/syscall.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <memory>

namespace qypr {

namespace {

constexpr uint16_t kGammaMax = UINT16_MAX;

// Tanner Helland's algorithm: approximate the RGB multipliers of a blackbody
// radiator at a given colour temperature (Kelvin).  Each channel is [0, 1].
// Returns {red, green, blue}.
std::array<double, 3> temperatureToRgb(uint32_t kelvin) {
    double t = static_cast<double>(kelvin) / 100.0;

    // Red
    double r;
    if (t <= 66.0) {
        r = 1.0;
    } else {
        r = t - 60.0;
        r = 329.698727446 * std::pow(r, -0.1332047592);
        r = std::clamp(r / 255.0, 0.0, 1.0);
    }

    // Green
    double g;
    if (t <= 66.0) {
        g = 99.4708025861 * std::log(t) - 161.1195681661;
    } else {
        g = t - 60.0;
        g = 288.1221695283 * std::pow(g, -0.0755148492);
    }
    g = std::clamp(g / 255.0, 0.0, 1.0);

    // Blue
    double b;
    if (t >= 66.0) {
        b = 1.0;
    } else if (t <= 19.0) {
        b = 0.0;
    } else {
        b = t - 10.0;
        b = 138.5177312231 * std::log(b) - 305.0447927307;
        b = std::clamp(b / 255.0, 0.0, 1.0);
    }

    return {r, g, b};
}

// Fill a gamma ramp (one channel, `size` entries) by scaling the identity
// ramp by `multiplier`.  The endpoint must not be pinned back to 65535: doing
// so makes white neutral while lower values are tinted, which produces a
// discontinuous hue shift instead of a colour-temperature change.
void fillChannelRamp(uint16_t* dst, uint32_t size, double multiplier) {
    if (size == 0) return;
    if (size == 1) {
        dst[0] = 0;
        return;
    }
    for (uint32_t i = 0; i < size; ++i) {
        double val = multiplier * static_cast<double>(i) / (size - 1);
        dst[i] =
            static_cast<uint16_t>(std::clamp(val * kGammaMax, 0.0, static_cast<double>(kGammaMax)));
    }
    // Preserve the black point without undoing the channel colour balance at
    // the white point.
    dst[0] = 0;
}

// Build the three 16-bit ramps (R, G, B) for the given temperature into the
// pre-sized buffer `dst` (3 * size * sizeof(uint16_t) bytes).
void fillRampForTemperature(uint16_t* dst, uint32_t size, uint32_t kelvin) {
    const auto rgb = temperatureToRgb(kelvin);
    // The blackbody approximation is not exactly neutral at 6500 K. Normalize
    // against that daylight reference so the slider's 6500 K/off position is
    // an actual identity ramp instead of applying a small permanent tint.
    const auto daylight = temperatureToRgb(NightLightBackend::kMaxTemperature);
    const double r = std::clamp(rgb[0] / std::max(daylight[0], 1e-9), 0.0, 1.0);
    const double g = std::clamp(rgb[1] / std::max(daylight[1], 1e-9), 0.0, 1.0);
    const double b = std::clamp(rgb[2] / std::max(daylight[2], 1e-9), 0.0, 1.0);
    fillChannelRamp(dst + 0 * size, size, r);
    fillChannelRamp(dst + 1 * size, size, g);
    fillChannelRamp(dst + 2 * size, size, b);
}

// Create an anonymous memfd the compositor can read via set_gamma(fd).
int createGammaFd(const char* name, size_t len) {
    int fd = static_cast<int>(syscall(SYS_memfd_create, name, 0));
    if (fd >= 0) {
        if (ftruncate(fd, static_cast<off_t>(len)) < 0) {
            close(fd);
            return -1;
        }
    }
    return fd;
}

// Write `len` bytes to `fd` via mmap + memcpy (one-shot). Returns false on
// any failure, with fd closed.
bool writeViaMmap(int fd, const uint16_t* src, size_t len) {
    void* mem = mmap(nullptr, len, PROT_WRITE, MAP_SHARED, fd, 0);
    if (mem == MAP_FAILED) {
        close(fd);
        return false;
    }
    std::memcpy(mem, src, len);
    munmap(mem, len);
    return true;
}

const zwlr_gamma_control_v1_listener kGammaControlListener = {
    .gamma_size = NightLightBackend::onGammaSize,
    .failed = NightLightBackend::onFailed,
};

}  // namespace

NightLightBackend::~NightLightBackend() {
    setEnabled(false);  // destroy all controls, restore compositor gamma
}

void NightLightBackend::init(zwlr_gamma_control_manager_v1* mgr, wl_display* display) {
    mgr_ = mgr;
    display_ = display;
}

void NightLightBackend::setOutputs(const std::vector<wl_output*>& outputs) {
    // Tear down any controls whose wl_output has disappeared from the list.
    for (auto& p : outputs_) {
        auto it = std::find(outputs.begin(), outputs.end(), p.output);
        if (it == outputs.end()) teardownOutput(p);
    }
    // Build PerOutput entries for new outputs.
    std::vector<PerOutput> next;
    next.reserve(outputs.size());
    for (wl_output* o : outputs) {
        PerOutput* existing = nullptr;
        for (auto& p : outputs_) {
            if (p.output == o) {
                existing = &p;
                break;
            }
        }
        if (existing) {
            next.push_back(std::move(*existing));
        } else {
            next.push_back(PerOutput{.output = o});
        }
    }
    outputs_ = std::move(next);
    // If the toggle is already enabled we must arm the new outputs now so the
    // hotplugged screen gets its warm ramp immediately.
    if (enabled_) armAll();
}

bool NightLightBackend::active() const {
    for (const auto& p : outputs_) {
        if (p.control && p.rampSize > 0 && !p.failed) return true;
    }
    return false;
}

void NightLightBackend::setEnabled(bool on) {
    if (!available()) return;
    if (on == enabled_) return;
    enabled_ = on;
    if (on) {
        armAll();
    } else {
        // Destroy every control: the compositor restores the original gamma
        // table for each output as each zwlr_gamma_control_v1 disappears.
        for (auto& p : outputs_) teardownOutput(p);
    }
    if (display_) wl_display_flush(display_);
    fireChange();
}

void NightLightBackend::setTemperature(uint32_t k) {
    k = std::clamp(k, kMinTemperature, kMaxTemperature);
    if (k == temperature_) return;
    temperature_ = k;
    // Re-apply ramps with the new temperature (if we're already enabled).
    if (enabled_) {
        for (auto& p : outputs_) applyRamp(p);
        if (display_) wl_display_flush(display_);
        fireChange();
    }
}

double NightLightBackend::sliderValue() const {
    if (!enabled_) return 0.0;
    // 0.0 = off (6500 K), 1.0 = warmest (2700 K).
    return 1.0 - static_cast<double>(temperature_ - kMinTemperature) /
                     static_cast<double>(kMaxTemperature - kMinTemperature);
}

void NightLightBackend::setSliderValue(double v) {
    v = std::clamp(v, 0.0, 1.0);

    // Treat the zero end of the slider as off, just like the backlight and
    // volume controls treat their minimum as a usable endpoint. Keeping the
    // temperature at daylight while disabled also makes the next drag start
    // from a neutral reference instead of the previous warm setting.
    if (v <= 0.0) {
        temperature_ = kMaxTemperature;
        if (enabled_) setEnabled(false);
        return;
    }

    // Map slider 0.0 → 6500 K (off), 1.0 → 2700 K (warmest).
    uint32_t k = static_cast<uint32_t>(kMaxTemperature - v * (kMaxTemperature - kMinTemperature));
    setTemperature(k);
    if (!enabled_) setEnabled(true);
}

void NightLightBackend::armAll() {
    for (auto& p : outputs_) {
        if (p.control || p.failed) continue;
        p.control = zwlr_gamma_control_manager_v1_get_gamma_control(mgr_, p.output);
        zwlr_gamma_control_v1_add_listener(p.control, &kGammaControlListener, &p);
    }
    if (display_) wl_display_roundtrip(display_);  // collect gamma_size events
    for (auto& p : outputs_) {
        if (p.rampSize > 0 && !p.failed) applyRamp(p);
    }
    fireChange();
}

void NightLightBackend::applyRamp(PerOutput& p) {
    if (!p.control || p.rampSize == 0 || p.failed) return;

    const size_t elemBytes = sizeof(uint16_t);
    const size_t totalBytes = 3 * p.rampSize * elemBytes;
    const size_t totalWords = 3 * p.rampSize;

    auto buf = std::make_unique<uint16_t[]>(totalWords);
    fillRampForTemperature(buf.get(), p.rampSize, temperature_);

    int fd = createGammaFd("qypr-nightlight", totalBytes);
    if (fd < 0) return;
    if (!writeViaMmap(fd, buf.get(), totalBytes)) return;

    zwlr_gamma_control_v1_set_gamma(p.control, fd);
    close(fd);
    if (display_) wl_display_flush(display_);
}

void NightLightBackend::teardownOutput(PerOutput& p) {
    if (p.control) zwlr_gamma_control_v1_destroy(p.control);
    p.control = nullptr;
    p.rampSize = 0;
    p.failed = false;
}

void NightLightBackend::fireChange() {
    if (onChange_) onChange_();
}

// --- listeners ---

void NightLightBackend::onGammaSize(void* data, zwlr_gamma_control_v1*, uint32_t size) {
    auto* p = static_cast<PerOutput*>(data);
    p->rampSize = size;
}

void NightLightBackend::onFailed(void* data, zwlr_gamma_control_v1*) {
    auto* p = static_cast<PerOutput*>(data);
    p->failed = true;
    zwlr_gamma_control_v1_destroy(p->control);
    p->control = nullptr;
    p->rampSize = 0;
}

}  // namespace qypr
