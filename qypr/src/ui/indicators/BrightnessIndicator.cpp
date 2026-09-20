// BrightnessIndicator.cpp - Status bar brightness indicator implementation.
#include "ui/indicators/BrightnessIndicator.hpp"

#include "ui/statusbar/IndicatorRegistry.hpp"
#include "ui/statusbar/QSTile.hpp"
#include "ui/statusbar/SliderPopover.hpp"

#include <cmath>

namespace qypr {

namespace {
constexpr double kScrollStep = 0.05;  // ±5% per scroll tick

const char* brightnessIcon(double frac) {
    if (frac >= 0.66) { return "󰃠"; }
    if (frac >= 0.33) { return "󰃟"; }
    return "󰃞";
}

// freedesktop symbolic names — display-brightness icons.
const char* brightnessThemedIcon(double frac) {
    if (frac >= 0.66) { return "display-brightness-high-symbolic"; }
    if (frac >= 0.33) { return "display-brightness-medium-symbolic"; }
    if (frac > 0.001) { return "display-brightness-low-symbolic"; }
    return "display-brightness-off-symbolic";
}
}  // namespace

BrightnessIndicator::BrightnessIndicator(const SystemBackends& backends)
    : StatusIndicator("brightness", Zone::Right, 100),
      backend_(backends.brightness) {
    // Hidden until this indicator's own backend publishes a snapshot — no
    // placeholder glyph standing in for data we do not have. StateCache
    // normally seeds that snapshot before the first frame, so this is only
    // visibly empty on a first-ever run or when the daemon never answers.
    // Without a backend (tests, registry previews) render sample defaults.
    loaded_ = (backend_ == nullptr);
    visible = loaded_;
}

std::string BrightnessIndicator::icon() const {
    if (!loaded_) {
        return "󰃟";  // no data yet: neutral glyph for the QS tile (the bar hides)
    }
    return brightnessIcon(lastSnap_.fraction());
}

std::string BrightnessIndicator::themedIcon() const {
    if (!loaded_) {
        return "";  // no data yet: fall back to the neutral glyph
    }
    return brightnessThemedIcon(lastSnap_.fraction());
}

std::string BrightnessIndicator::tooltip() const {
    return "Brightness " +
           std::to_string(static_cast<int>(std::lround(lastSnap_.fraction() * 100.0))) + "%";
}

void BrightnessIndicator::onBackendUpdate() {
    if (backend_ == nullptr) { return; }
    lastSnap_ = backend_->snapshot();
    // Only *this* backend's readiness reveals the indicator — a push from an
    // unrelated backend must not mark us loaded with a still-empty snapshot.
    loaded_ = backend_->ready();
    visible = loaded_ && lastSnap_.available;
}

bool BrightnessIndicator::onScroll(double dx, double dy, double x, double y) {
    (void)x;
    (void)y;
    (void)dx;
    if ((backend_ == nullptr) || !lastSnap_.available) { return false; }
    // Scroll up (negative dy in Wayland) brightens.
    const double delta = dy < 0 ? kScrollStep : -kScrollStep;
    backend_->setFraction(lastSnap_.fraction() + delta);
    return true;
}

std::unique_ptr<QSTile> BrightnessIndicator::createTile() {
    auto* snap = &lastSnap_;
    auto* backend = backend_;
    return std::make_unique<QSSliderTile>(
        "󰃠", [snap]() { return snap->fraction(); },
        [backend](double v) {
            if (backend) { backend->setFraction(v); }
        });
}

std::unique_ptr<DetailedPopover> BrightnessIndicator::createDetailedView() {
    if (backend_ == nullptr) { return nullptr; }

    auto* snap = &lastSnap_;
    auto* backend = backend_;
    auto tile = std::make_unique<QSSliderTile>(
        "󰃠", [snap]() { return snap->fraction(); },
        [backend](double v) {
            if (backend) { backend->setFraction(v); }
        },
        [snap]() { return std::string(brightnessIcon(snap->fraction())); }, nullptr, nullptr,
        "Brightness");
    return std::make_unique<SliderPopover>(std::move(tile));
}

REGISTER_INDICATOR("brightness", Zone::Right, 100, BrightnessIndicator)

}  // namespace qypr
