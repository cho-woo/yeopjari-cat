// Developer-only mock desktop trace. Motion is produced by the real Controller.
// The small physics-to-art adapter below mirrors main.cpp::Render explicitly
// and uses its shared AirbornePose helper;
// it is not a Win32 execution, window discovery test, or production state machine.
#include "../src/cat_animation.hpp"
#include "../src/cat_physics.hpp"
#include <box2d/box2d.h>
#include <algorithm>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {
using cat::animation::Motion;
constexpr int kFps = 60;
constexpr int kFrames = 270;
void Scenario(const std::string& scene) {
    cat::physics::Controller physics;
    cat::animation::Locomotion gait;
    physics.Reset(200, 70, 930);
    physics.SetPlatform(450, 750, 220);
    physics.SetTargetVelocity(0);
    bool launched = false, removed = false;
    double landedAt = -1, landingStrength = 0;
    for (int tick = 0; tick <= kFrames; ++tick) {
        const double time = static_cast<double>(tick) / kFps;
        const auto milliseconds = static_cast<std::uint64_t>(std::llround(time * 1000));
        if (!removed && ((scene == "closed_inflight" && time >= 1) ||
                         (scene == "closed_supported" && time >= 2.5))) {
            physics.RemovePlatform();
            removed = true;
        }
        if (!launched && tick >= 30) {
            if (!physics.CanLaunchTo(600, 220) || !physics.LaunchTo(600, 220))
                throw std::runtime_error("Controller rejected preview launch");
            launched = true;
        }
        const auto current = physics.CurrentPose();
        const auto physical = physics.RenderPose();
        if (current.landed) {
            landedAt = time;
            landingStrength = std::clamp(current.landingSpeed / 230.0, .35, 1.0);
        }
        const bool preparing = time >= .28 && !launched;
        const double walkingVelocity = current.velocityX - current.supportVelocityX;
        Motion motion = preparing ? Motion::Jump : Motion::Watch;
        if (!preparing && physical.grounded && std::abs(walkingVelocity) > 2) motion = Motion::Walk;
        const auto actionElapsed = preparing ? milliseconds - 280 : milliseconds;
        auto pose = cat::animation::Evaluate(motion, actionElapsed, 10000 + milliseconds, 1, gait);
        if (motion == Motion::Jump) pose.lift = 0;
        // Keep this adapter next to the diagnostic, rather than change production
        // APIs only for a preview. These rules match main.cpp::Render.
        if (!physical.grounded) {
            pose = cat::animation::AirbornePose(physical.velocityY, 1);
        }
        if (landedAt >= 0 && time - landedAt < .280) {
            const double settle = std::sin((time - landedAt) / .280 * cat::animation::kPi) * landingStrength;
            pose.scaleX *= 1 + .035 * settle;
            pose.scaleY *= 1 - .085 * settle;
        }
        std::cout << scene << ',' << time << ',' << current.x << ',' << current.lift << ','
            << physical.x << ',' << physical.lift << ',' << physical.velocityX << ',' << physical.velocityY << ','
            << current.grounded << ',' << current.onPlatform << ',' << current.landed << ',' << landingStrength << ','
            << !removed << ',' << 450 << ',' << 750 << ',' << 220;
        const double values[] = {static_cast<double>(pose.frame), pose.x, pose.lift, pose.scaleX, pose.scaleY,
            pose.angle, static_cast<double>(pose.flip), static_cast<double>(pose.centerPivot),
            static_cast<double>(pose.blendFrame), pose.blend, static_cast<double>(pose.groundContact),
            pose.joints.leftEar, pose.joints.rightEar, pose.joints.headNod, pose.joints.frontLeft,
            pose.joints.frontRight, pose.joints.tailRaise, pose.joints.breath, pose.joints.crouch,
            static_cast<double>(pose.bellyRig)};
        for (int index = 0; index < 20; ++index)
            std::cout << ',' << (index >= 11 && index < 19 ? std::lround(values[index] * 32) / 32.0 : values[index]);
        std::cout << '\n';
        if (tick == kFrames) break;
        const double previousX = current.x;
        physics.Update(1.0 / kFps);
        const auto next = physics.CurrentPose();
        gait.Advance(motion, next.x - previousX - next.supportTravelX, 1.0 / kFps);
    }
}
}

int main() {
    try {
        const auto version = b2GetVersion();
        std::cerr << "Box2D " << version.major << '.' << version.minor << '.' << version.revision << '\n';
        std::cout << std::setprecision(15);
        std::cout << "scenario,time,x,lift,render_x,render_lift,vx,vy,grounded,on_platform,landed,landing_strength,ledge_visible,ledge_left,ledge_right,ledge_height";
        for (int index = 0; index < 20; ++index) std::cout << ",p" << index;
        std::cout << '\n';
        for (const auto& scene : {"rise", "closed_inflight", "closed_supported"}) Scenario(scene);
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
