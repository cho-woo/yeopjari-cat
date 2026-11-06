#pragma once

#include "cat_animation.hpp"
#include "care_sprites.generated.hpp"
#include <array>
#include <limits>

namespace cat::animation {

// Transient rendered joints only. Never changes the action clock, physics,
// saved state or contact timing. Different drawings cannot share this rig.
class PoseTransition {
public:
    static constexpr std::uint64_t DurationMs = 180;
    void Reset() { valid_ = active_ = false; }
    void Pause(std::uint64_t milliseconds) {
        if (active_) started_ += std::min(milliseconds,
            std::numeric_limits<std::uint64_t>::max() - started_);
    }
    Pose Apply(Pose target, std::uint64_t ticks, std::uint64_t actionSerial,
               unsigned identity = 0, bool enabled = true,
               std::uint64_t actionStartedMs=std::numeric_limits<std::uint64_t>::max()) {
        const bool compatible = enabled && Canonical(target,identity) && valid_ &&
            Canonical(previous_,identity_) && previous_.flip == target.flip && identity_ == identity;
        if (!compatible) active_ = false;
        else if (serial_ != actionSerial) {
            // Interrupted transitions continue from what was actually drawn.
            from_ = previous_.joints;
            // A late first paint must not replay an already-completed action
            // transition or erase a moving-platform balance correction.
            started_ = std::min(ticks,actionStartedMs);
            active_ = true;
        }
        if (active_) {
            const auto elapsed = ticks >= started_ ? ticks - started_ : 0;
            if (elapsed >= DurationMs) active_ = false;
            else {
                const double weight = SmoothStep(static_cast<double>(elapsed) / DurationMs);
                for (auto member : Controls())
                    target.joints.*member = from_.*member * (1-weight) + target.joints.*member * weight;
                target.joints = cat::deform::Bounded(target.joints);
            }
        }
        previous_ = target;
        serial_ = actionSerial;
        identity_ = identity;
        valid_ = enabled && Canonical(target,identity);
        return target;
    }

private:
    using Control = double cat::deform::Joints::*;
    static std::array<Control, 8> Controls() {
        return {{&cat::deform::Joints::leftEar, &cat::deform::Joints::rightEar,
            &cat::deform::Joints::headNod, &cat::deform::Joints::frontLeft,
            &cat::deform::Joints::frontRight, &cat::deform::Joints::tailRaise,
            &cat::deform::Joints::breath, &cat::deform::Joints::crouch}};
    }
    static bool Canonical(const Pose& pose,unsigned identity) {
        const auto clip=pose.fishMeal?cat::sprite_sequence::Clip::Fish:pose.spriteClip;
        if(cat::sprite_assets::Find(clip,identity,cat::sprite_assets::kCareSprites))return false;
        return pose.frame == 4 && pose.blendFrame == -1 && !pose.bellyRig &&
            pose.groundContact && !pose.centerPivot && pose.x == 0 && pose.lift == 0 &&
            pose.angle == 0 && pose.scaleX == 1 && pose.scaleY == 1 &&
            cat::deform::Finite(pose.joints) && pose.joints.pawReachX == 0 &&
            pose.joints.pawReachY == 0 && pose.joints.pawAnchorX == 0;
    }
    bool valid_ = false, active_ = false;
    unsigned identity_ = 0;
    std::uint64_t serial_ = 0, started_ = 0;
    Pose previous_;
    cat::deform::Joints from_;
};
} // namespace cat::animation
