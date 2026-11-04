#pragma once
#include "cat_physics.hpp"
#include <algorithm>
#include <cmath>

namespace cat::cursor {

// An impulse targets a point on the current physical support, never the live
// pointer after takeoff. Bounds include the host's ledge/body safety margin.
inline bool LaunchPounce(physics::Controller& body, double targetX, double minX,
                         double maxX, double scale, double jumpModifier) {
    const auto pose = body.CurrentPose();
    if (!pose.grounded || !std::isfinite(targetX) || !std::isfinite(minX) ||
            !std::isfinite(maxX) || !std::isfinite(scale) || !std::isfinite(jumpModifier) ||
            scale <= 0 || jumpModifier <= 0 || targetX < minX+8 || targetX > maxX-8 ||
            std::abs(targetX-pose.x) > 180*scale) return false;
    const double clearance = std::clamp(30*scale*jumpModifier, 10.0, 65.0);
    return body.LaunchTo(targetX, pose.lift, clearance);
}

class PawContact {
public:
    void Reset() { contacts_ = 0; }
    bool Observe(unsigned contactIndex, bool allowed, double pawX, double pawHeight,
                 double cursorX, double cursorHeight, double radius) {
        if (!allowed || contactIndex < 1 || contactIndex > 2 ||
                (contacts_ & (1U<<contactIndex)) || !std::isfinite(pawX) ||
                !std::isfinite(pawHeight) || !std::isfinite(cursorX) ||
                !std::isfinite(cursorHeight) || !std::isfinite(radius) || radius <= 0 ||
                std::hypot(pawX-cursorX,pawHeight-cursorHeight) > radius) return false;
        contacts_ |= 1U<<contactIndex;
        return true;
    }
private:
    unsigned contacts_ = 0;
};

inline bool LandingContact(bool allowed, const physics::Pose& pose, double cursorX,
                           double cursorHeight, double scale) {
    return allowed && pose.landed && pose.grounded && std::isfinite(cursorX) &&
        std::isfinite(cursorHeight) && std::isfinite(scale) && scale > 0 &&
        std::abs(cursorX-pose.x) <= 32*scale && cursorHeight >= -8 && cursorHeight <= 30*scale;
}

} // namespace cat::cursor
