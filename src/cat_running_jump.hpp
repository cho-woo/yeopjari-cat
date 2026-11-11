#pragma once

#include "cat_animation.hpp"
#include "cat_physics.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace cat::running_jump {
// Runtime-only continuation of an actual ground velocity, never a saved
// position or a prescribed screen-space curve. Box2D owns the complete arc.
struct Plan {
    static constexpr std::uint64_t PreparationMs=120;
    bool active=false,launched=false;
    animation::Motion resume=animation::Motion::Walk;
    double velocityX=0,baseSpeed=0;
    std::uint64_t resumeDurationMs=2400;
};
inline Plan Begin(animation::Motion previous,const physics::Pose& physical,double effectiveSpeedScale=1,
                  std::uint64_t remainingMs=2400) {
    Plan result;
    const bool movingIntent=animation::Walking(previous)||previous==animation::Motion::Idle||previous==animation::Motion::Watch;
    if(!movingIntent||!physical.grounded||physical.onPlatform||physical.hanging||
            !std::isfinite(physical.velocityX)||std::abs(physical.velocityX)<8||
            !std::isfinite(effectiveSpeedScale)||effectiveSpeedScale<=0)return result;
    result.active=true;
    result.resume=animation::Walking(previous)?previous:animation::Motion::Idle;
    result.velocityX=physical.velocityX;
    result.baseSpeed=std::abs(physical.velocityX)/effectiveSpeedScale;
    result.resumeDurationMs=std::clamp<std::uint64_t>(remainingMs,1200,15000);
    return result;
}
} // namespace cat::running_jump
