#pragma once
#include "cat_animation.hpp"
#include <cmath>
#include <cstdint>

namespace cat::motion {
inline constexpr double kStoppedSpeed = 2.0;

inline bool ReadyToStart(bool grounded, double velocityX) {
    return grounded && std::isfinite(velocityX) && std::abs(velocityX) <= kStoppedSpeed;
}

inline bool NeedsStop(animation::Motion requested, bool grounded, double velocityX,
                      bool windowTransfer = false) {
    // A pounce travels only after its planted crouch/takeoff preparation.
    const bool stationaryStart = !animation::Travels(requested) || requested == animation::Motion::Pounce;
    return !windowTransfer && stationaryStart && grounded && !ReadyToStart(grounded, velocityX);
}

// Request ordinary motor braking before a finite ledge's safe walking edge.
// The 680px/s² limit matches grounded braking in the Box2D controller; 12px
// reserves its easing tail and the interval before the next rendered frame.
// Velocity and desired speed are relative to the supporting window. The
// caller owns grounded/travel checks and latches the resulting stop action.
inline bool BrakeBeforeEdge(double x, double velocity, double desired,
                            double low, double high) {
    if (!std::isfinite(x) || !std::isfinite(velocity) || !std::isfinite(desired) ||
            !std::isfinite(low) || !std::isfinite(high) || low > high) return false;
    const double distance = velocity * velocity / (2 * 680.0) + 12.0;
    if (desired < 0 && velocity <= 0) return x - low <= distance;
    if (desired > 0 && velocity >= 0) return high - x <= distance;
    return false;
}

// Move both clocks together, keeping the complete planned action duration.
// The caller brakes via Box2D while retaining the previous walking gait.
// Return true once when the pending action can begin on planted feet.
inline bool AdvanceStopGate(bool& waiting, bool grounded, double velocityX,
                            std::uint64_t ticks, std::uint64_t& started,
                            std::uint64_t& decision) {
    if (!waiting) return false;
    const auto frozen = ticks >= started ? ticks - started : 0;
    started += frozen;
    decision += frozen;
    if (!ReadyToStart(grounded, velocityX)) return false;
    waiting = false;
    return true;
}
} // namespace cat::motion
