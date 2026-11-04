#pragma once
#include <algorithm>
#include <cmath>
#include <optional>

namespace cat::memory {
// Walk toward a stationary nearby hand, stopping outside its click area.
// Close hands need no approach: subtracting the stand-off there would make the
// pet walk away from the user. Coordinates are logical work-area pixels.
inline std::optional<double> ApproachTarget(double catX, double cursorX, double low, double high) {
    if (!std::isfinite(catX) || !std::isfinite(cursorX) || !std::isfinite(low) || !std::isfinite(high) ||
        low > high || catX < low || catX > high || cursorX < low || cursorX > high) return {};
    constexpr double standOff = 55, minimumTravel = 12;
    const double distance = cursorX - catX;
    if (std::abs(distance) <= standOff + minimumTravel) return {};
    const double target = std::clamp(cursorX + (distance < 0 ? standOff : -standOff), low, high);
    if (std::abs(target - catX) <= minimumTravel || (target - catX) * distance <= 0) return {};
    return target;
}
}
