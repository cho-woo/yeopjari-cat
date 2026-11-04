// Developer diagnostic: every path sample comes from the actual Box2D Controller.
// stdout CSV is consumed by render_physics_trace.py; no application state or saves.
#include "../src/cat_physics.hpp"
#include <box2d/box2d.h>
#include <algorithm>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {
void Scenario(const std::string& scene, int fps) {
    cat::physics::Controller controller;
    controller.Reset(200, 40, 960);
    controller.SetPlatform(450, 750, 220);
    controller.SetTargetVelocity(0);
    bool launched = false, removed = false;
    constexpr double duration = 5.0;
    const int frames = static_cast<int>(duration * fps);
    for (int frame = 0; frame <= frames; ++frame) {
        const double time = static_cast<double>(frame) / fps;
        // These are scene inputs, not a predicted trajectory. All resulting
        // motion and contact rows below are read back from Box2D.
        const double shift = scene == "moving" ? std::clamp(time - 1.0, 0.0, 2.0) * 48.0 : 0.0;
        if (scene == "closed" && time >= 1.0 && !removed) {
            controller.RemovePlatform();
            removed = true;
        }
        if (!removed) controller.SetPlatform(450 + shift, 750 + shift, 220);
        if (!launched && time >= .5) {
            if (!controller.CanLaunchTo(600, 220, 45) || !controller.LaunchTo(600, 220, 45))
                throw std::runtime_error("diagnostic launch rejected: " + scene);
            launched = true;
        }
        const auto pose = controller.CurrentPose();
        const auto rendered = controller.RenderPose();
        std::cout << scene << ',' << fps << ',' << time << ',' << pose.x << ',' << pose.lift << ','
            << pose.velocityX << ',' << pose.velocityY << ',' << pose.grounded << ',' << pose.landed << ','
            << pose.onPlatform << ',' << 450 + shift << ',' << 750 + shift << ',' << 220 << ',' << !removed << ','
            << rendered.x << ',' << rendered.lift << '\n';
        if (frame != frames) controller.Update(1.0 / fps);
    }
}
}
int main() {
    try {
        const auto version = b2GetVersion();
        std::cerr << "Box2D " << version.major << '.' << version.minor << '.' << version.revision << '\n';
        std::cout << std::setprecision(15);
        std::cout << "scenario,fps,time,x,lift,vx,vy,grounded,landed,on_platform,ledge_left,ledge_right,ledge_height,ledge_visible,render_x,render_lift\n";
        for (const auto& scene : {"rise", "moving", "closed"})
            for (int fps : {30, 60, 120, 144}) Scenario(scene, fps);
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
