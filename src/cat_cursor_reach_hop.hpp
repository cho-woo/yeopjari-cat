#pragma once

#include "cat_physics.hpp"
#include <algorithm>
#include <cmath>

namespace cat::cursor {
inline bool LaunchRunningReachHop(physics::Controller& body,double targetHeight,
        double minX,double maxX,double scale,double jumpModifier) {
    const auto pose=body.CurrentPose();
    if(!pose.grounded||pose.onPlatform||!std::isfinite(targetHeight)||
            !std::isfinite(minX)||!std::isfinite(maxX)||!std::isfinite(scale)||scale<=0||
            !std::isfinite(jumpModifier)||jumpModifier<=0||std::abs(pose.velocityX)<8)return false;
    const double apex=std::clamp((targetHeight-55*scale)*jumpModifier,std::min(28*scale,180.),180.);
    const double duration=2*std::sqrt(2*apex/900.);
    const double landing=pose.x+pose.velocityX*duration;
    const double stop=pose.velocityX*std::abs(pose.velocityX)/(2*680.);
    if(landing+stop<minX+12||landing+stop>maxX-12)return false;
    return body.JumpWithMomentum(apex);
}
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
    if(std::abs(x-pose.x)<.5&&std::abs(pose.velocityX)<1e-6)
        return body.JumpWithMomentum(std::clamp((targetHeight-55*scale)*jumpModifier,std::min(28*scale,180.),180.));
    const double clearance=std::clamp(std::clamp(targetHeight-55*scale,28*scale,145*scale)*jumpModifier,10.,120.);
    return body.LaunchTo(x,pose.lift,clearance);
}
} // namespace cat::cursor
