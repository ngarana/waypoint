// NightLightIndicator.cpp - Night Light applet implementation.
#include "ui/indicators/NightLightIndicator.hpp"

#include "core/Config.hpp"
#include "system/NightLightBackend.hpp"
#include "ui/Theme.hpp"
#include "ui/statusbar/IndicatorRegistry.hpp"
#include "ui/statusbar/QSTile.hpp"
#include "ui/statusbar/SliderPopover.hpp"

namespace qypr {

namespace {
constexpr const char* kMoonGlyph = "\uf186";  // nf-fa-moon_o
}  // namespace

// ─────────────────────────────────────────────────────────────────────────
// NightLightIndicator
// ─────────────────────────────────────────────────────────────────────────

NightLightIndicator::NightLightIndicator(const SystemBackends& backends)
    : StatusIndicator("night-light", Zone::Right, 230),
      backend_(backends.nightLight) {
    visible = false;
    // Read the configured colour temperature; applied on first enable.
    if (backends.config && backend_) {
        const int k = backends.config->getInt("night-light", "temperature",
                                              static_cast<int>(backend_->temperature()));
        backend_->setTemperature(static_cast<uint32_t>(k));
    }
}

std::string NightLightIndicator::icon() const {
    if (!backend_ || !backend_->enabled()) return kMoonGlyph;
    uint32_t k = backend_->temperature();
    if (k <= 3200) return "\uf186";  // warm
    return kMoonGlyph;
}

std::string NightLightIndicator::themedIcon() const {
    // night/day symbolic variant resolved against the active icon theme.
    if (!backend_ || !backend_->enabled()) return "weather-clear-night-symbolic";
    return "weather-clear-night-symbolic";
}

std::string NightLightIndicator::tooltip() const {
    if (!backend_ || !backend_->available()) return "Night Light";
    if (!backend_->enabled()) return "Night Light: off";
    return "Night Light: " + std::to_string(backend_->temperature()) + " K";
}

Color NightLightIndicator::iconColor() const {
    if (backend_ && backend_->enabled()) return theme::color::primary;
    return theme::color::textSubtle;
}

void NightLightIndicator::onBackendUpdate() {
    visible = backend_ && backend_->available();
}

bool NightLightIndicator::onScroll(double dx, double dy, double, double) {
    if (!backend_ || !backend_->available()) return false;
    double d = dy != 0.0 ? dy : dx;
    double cur = backend_->sliderValue();
    backend_->setSliderValue(cur + (d < 0 ? 0.05 : -0.05));
    return true;
}

std::unique_ptr<QSTile> NightLightIndicator::createTile() {
    auto* backend = backend_;
    auto tile = std::make_unique<QSToggleTile>(
        "Night Light", kMoonGlyph, [backend]() { return backend && backend->enabled(); },
        [backend]() {
            if (backend && backend->available()) backend->toggle();
        },
        [backend]() -> std::string {
            if (!backend || !backend->available()) return "Unavailable";
            if (!backend->enabled()) return "Off";
            return std::to_string(backend->temperature()) + " K";
        },
        Color{0, 0, 0, 0}, QSTile::Role::NightLight);
    tile->setOnScroll([backend](double dx, double dy) {
        if (!backend || !backend->available()) return false;
        const double delta = dy != 0.0 ? dy : dx;
        if (delta == 0.0) return false;
        const double step = delta < 0.0 ? 0.05 : -0.05;
        backend->setSliderValue(backend->sliderValue() + step);
        return true;
    });
    return tile;
}

std::unique_ptr<DetailedPopover> NightLightIndicator::createDetailedView() {
    if (!backend_) return nullptr;

    auto backend = backend_;
    auto tile = std::make_unique<QSSliderTile>(
        kMoonGlyph, [backend]() { return backend ? backend->sliderValue() : 0.0; },
        [backend](double value) {
            if (backend) backend->setSliderValue(value);
        },
        [backend]() { return std::string(kMoonGlyph); }, nullptr, nullptr, "Night Light");
    return std::make_unique<SliderPopover>(std::move(tile));
}

REGISTER_INDICATOR("night-light", Zone::Right, 230, NightLightIndicator)

}  // namespace qypr
