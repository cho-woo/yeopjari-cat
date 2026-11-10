#pragma once

#include "cat_physics.hpp"
#include <algorithm>
#include <cmath>

namespace cat::cursor {
// Freeze a physical same-support hop before takeoff. The caller owns crouch,
// landing/recovery and once-only contact; the live pointer never retargets it.
inline bool LaunchReachHop(physics::Controller& body,double targetX,double targetHeight,
                          double minX,double maxX,double scale,double jumpModifier,bool allowHorizontal=true) {
    const auto pose=body.CurrentPose();
    if(!pose.grounded||!std::isfinite(targetX)||!std::isfinite(targetHeight)||
            !std::isfinite(minX)||!std::isfinite(maxX)||!std::isfinite(scale)||!std::isfinite(jumpModifier)||
            scale<=0||jumpModifier<=0||minX+16>=maxX||targetHeight<0||targetHeight>245*scale)return false;
    const double x=allowHorizontal?targetX:pose.x;
    if(x<minX+8||x>maxX-8||std::abs(x-pose.x)>150*scale)return false;
    const double clearance=std::clamp(std::clamp(targetHeight-55*scale,28*scale,145*scale)*jumpModifier,10.,120.);
    return body.LaunchTo(x,pose.lift,clearance);
}
} // namespace cat::cursor
