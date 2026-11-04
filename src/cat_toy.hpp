#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <utility>

namespace cat::toy {

// Transient interaction state only; this module never changes pet save records.
enum class Kind { None, Ball, Box };

// Narrow work areas can invert the requested margins. Collapse them to their
// midpoint instead of invoking std::clamp with an invalid lower/upper pair.
inline double ClampCenter(double x, double low, double high) {
    if (low > high) return low + (high - low) * .5;
    return std::clamp(x, low, high);
}

// Ball centers stay both on screen and within paw reach of the cat's allowed
// center positions. All arguments/results use work-area logical coordinates.
inline std::pair<double, double> RollingBounds(double workWidth, double minCenter,
                                               double maxCenter, double reach = 36.0,
                                               double ballRadius = 14.0) {
    workWidth = std::max(0.0, workWidth);
    reach = std::max(0.0, reach);
    const double radius = std::clamp(ballRadius, 0.0, workWidth * .5);
    if (minCenter > maxCenter) minCenter = maxCenter = minCenter + (maxCenter - minCenter) * .5;
    const double low = std::max(radius, minCenter - reach);
    const double high = std::min(workWidth - radius, maxCenter + reach);
    if (low <= high) return {low, high};
    const double center = std::clamp(minCenter + (maxCenter - minCenter) * .5,
                                     radius, workWidth - radius);
    return {center, center};
}

inline double ApproachTarget(Kind kind, double toyX, double catX, double reach,
                             double low, double high) {
    if (kind == Kind::None) return ClampCenter(catX, low, high);
    if (kind == Kind::Box) return ClampCenter(toyX, low, high);
    reach = std::max(0.0, reach);
    const double preferred = toyX + (catX > toyX ? reach : -reach);
    const double alternate = toyX + (catX > toyX ? -reach : reach);
    if (preferred >= low && preferred <= high) return preferred;
    if (alternate >= low && alternate <= high) return alternate;
    return ClampCenter(preferred, low, high);
}

inline double ApproachVelocity(double distance, double maxSpeed = 38.0,
                               double acceleration = 300.0) {
    if (!std::isfinite(distance) || !std::isfinite(maxSpeed) ||
        !std::isfinite(acceleration) || maxSpeed <= 0 || acceleration <= 0 ||
        std::abs(distance) <= 2.0) return 0.0;
    // Brake before the destination instead of overshooting and alternating sides.
    const double speed = std::min(maxSpeed, std::sqrt(2 * acceleration * (std::abs(distance) - 2.0)));
    return std::copysign(speed, distance);
}

inline constexpr std::uint64_t kPawContactMs = 330;
inline constexpr std::uint64_t kPawContactEndMs = 660;

// The extended paw frame starts at 330ms. One action can impulse a ball only
// once; a skipped contact window must not kick it after the paw has retracted.
inline bool PawContact(std::uint64_t elapsed, bool& consumed) {
    if (consumed || elapsed < kPawContactMs) return false;
    consumed = true;
    return elapsed < kPawContactEndMs;
}

struct Ball {
    double x = 0.0;
    double velocity = 0.0;
    static constexpr double friction = 2.8;

    void Kick(int direction, double speed) {
        if (!direction || !std::isfinite(speed) || speed <= 0) return;
        velocity = (direction < 0 ? -1 : 1) * std::min(speed, 500.0);
    }

    void Advance(double deltaSeconds, double low, double high, bool paused = false) {
        if (paused || !std::isfinite(deltaSeconds) || deltaSeconds <= 0 || deltaSeconds > .25) return;
        if (!std::isfinite(x) || !std::isfinite(velocity)) {
            x = ClampCenter(0.0, low, high); velocity = 0; return;
        }
        // Analytic exponential friction keeps 30/60/120Hz motion identical.
        const double decay = std::exp(-friction * deltaSeconds);
        const double next = x + velocity * (-std::expm1(-friction * deltaSeconds)) / friction;
        x = ClampCenter(next, low, high);
        velocity *= decay;
        if (x != next || std::abs(velocity) < .05) velocity = 0;
    }
};

} // namespace cat::toy
