// IdleInhibitorIndicator.cpp - "Keep awake" toggle implementation.
#include "ui/indicators/IdleInhibitorIndicator.hpp"

#include "system/IdleInhibitor.hpp"
#include "ui/Theme.hpp"
#include "ui/statusbar/IndicatorRegistry.hpp"
#include "ui/statusbar/QSTile.hpp"

namespace qypr {

namespace {
constexpr const char* kAwake = "󰅶";   // nf-md-coffee — awake / inhibiting
constexpr const char* kAsleep = "󰛊";  // nf-md-coffee_off — normal idle allowed
}  // namespace

IdleInhibitorIndicator::IdleInhibitorIndicator(const SystemBackends& backends)
    : StatusIndicator("idle-inhibitor", Zone::Right, 250),
      backend_(backends.idleInhibitor) {
    // Hidden until the backend reports the protocol is available (after the
    // display binds it); never present on the lock screen (backend is null).
    visible = false;
}

std::string IdleInhibitorIndicator::icon() const {
    return (backend_ && backend_->active()) ? kAwake : kAsleep;
}

std::string IdleInhibitorIndicator::themedIcon() const {
    // "keep awake" caffeine-cup icons; themes that lack them fall back to the glyph.
    return (backend_ && backend_->active()) ? "caffeine-cup-full-symbolic"
                                            : "caffeine-cup-empty-symbolic";
}

std::string IdleInhibitorIndicator::tooltip() const {
    if (!backend_ || !backend_->available()) return "Keep awake";
    return backend_->active() ? "Keep awake: on (idle inhibited)" : "Keep awake: off";
}

Color IdleInhibitorIndicator::iconColor() const {
    return (backend_ && backend_->active()) ? theme().colors.primary : theme().colors.textSubtle;
}

void IdleInhibitorIndicator::onBackendUpdate() {
    visible = backend_ && backend_->available();
}

bool IdleInhibitorIndicator::onClick(double, double) {
    if (!backend_ || !backend_->available()) return false;
    backend_->toggle();
    return true;  // consume — no popover/tile activation
}

std::unique_ptr<QSTile> IdleInhibitorIndicator::createTile() {
    auto* backend = backend_;
    return std::make_unique<QSToggleTile>(
        "Keep awake", kAwake, [backend]() { return backend && backend->active(); },
        [backend]() {
            if (backend) backend->toggle();
        },
        [backend]() -> std::string {
            if (!backend || !backend->available()) return "Unavailable";
            return backend->active() ? "On" : "Off";
        },
        Color{0, 0, 0, 0}, QSTile::Role::KeepAwake);
}

REGISTER_INDICATOR("idle-inhibitor", Zone::Right, 250, IdleInhibitorIndicator)

}  // namespace qypr
