// Types.hpp - Small value types shared across the UI and render layers.
//
// Kept header-only and dependency-free (cairo aside) so every module can
// reuse the same geometry/colour/easing primitives (DRY).

#pragma once

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <string>

namespace qypr {

// Monotonic millisecond clock used to drive all animations.
inline int64_t nowMs() {
    using namespace std::chrono;
    return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}

// -----------------------------------------------------------------------------
// Colour
// -----------------------------------------------------------------------------
struct Color {
    double r = 0, g = 0, b = 0, a = 1;

    // Parse Qt-style hex: "#RRGGBB" (opaque) or "#AARRGGBB" (alpha first).
    static Color fromHex(const std::string& hex) {
        auto h = hex;
        if (!h.empty() && h[0] == '#') h.erase(0, 1);
        auto byte = [&](int i) {
            return static_cast<double>(std::stoul(h.substr(i, 2), nullptr, 16)) / 255.0;
        };
        Color c;
        if (h.size() == 8) {  // AARRGGBB
            c.a = byte(0);
            c.r = byte(2);
            c.g = byte(4);
            c.b = byte(6);
        } else if (h.size() == 6) {  // RRGGBB
            c.r = byte(0);
            c.g = byte(2);
            c.b = byte(4);
            c.a = 1.0;
        }
        return c;
    }

    Color withAlpha(double alpha) const {
        Color c = *this;
        c.a = alpha;
        return c;
    }
    static Color rgba(double r, double g, double b, double a) { return {r, g, b, a}; }
};

// -----------------------------------------------------------------------------
// Geometry
// -----------------------------------------------------------------------------
struct Rect {
    double x = 0, y = 0, w = 0, h = 0;

    bool contains(double px, double py) const {
        return px >= x && px <= x + w && py >= y && py <= y + h;
    }
    double cx() const { return x + w / 2.0; }
    double cy() const { return y + h / 2.0; }
    bool valid() const { return w > 0 && h > 0; }
};

// -----------------------------------------------------------------------------
// Interpolation & easing
// -----------------------------------------------------------------------------
inline double clamp01(double v) {
    return std::clamp(v, 0.0, 1.0);
}
inline double lerp(double a, double b, double t) {
    return a + (b - a) * t;
}

namespace ease {
inline double linear(double t) {
    return t;
}

inline double inOutQuad(double t) {
    return t < 0.5 ? 2.0 * t * t : 1.0 - std::pow(-2.0 * t + 2.0, 2.0) / 2.0;
}

inline double outBack(double t) {
    constexpr double c1 = 1.70158;
    constexpr double c3 = c1 + 1.0;
    return 1.0 + c3 * std::pow(t - 1.0, 3.0) + c1 * std::pow(t - 1.0, 2.0);
}
}  // namespace ease

// -----------------------------------------------------------------------------
// Animated scalar: eases from its current value toward a target over a
// duration. Stateless readers call value(now); active(now) reports motion.
// -----------------------------------------------------------------------------
class Animated {
public:
    Animated() = default;
    explicit Animated(double initial) : from_(initial), to_(initial) {}

    // Retarget. If the target changed, (re)start the animation from the
    // currently displayed value for a smooth interruption.
    void animateTo(double target, int64_t durationMs, double (*easing)(double)) {
        if (std::abs(target - to_) < 1e-6) return;
        from_ = value(nowMs());
        to_ = target;
        start_ = nowMs();
        duration_ = std::max<int64_t>(1, durationMs);
        easing_ = easing;
    }

    // Jump instantly, cancelling any motion.
    void set(double v) {
        from_ = to_ = v;
        start_ = 0;
    }

    double value(int64_t now) const {
        if (duration_ <= 0) return to_;
        double t = clamp01(static_cast<double>(now - start_) / static_cast<double>(duration_));
        return lerp(from_, to_, easing_(t));
    }

    bool active(int64_t now) const { return duration_ > 0 && (now - start_) < duration_; }

    double target() const { return to_; }

private:
    double from_ = 0, to_ = 0;
    int64_t start_ = 0, duration_ = 0;
    double (*easing_)(double) = ease::linear;
};

}  // namespace qypr
