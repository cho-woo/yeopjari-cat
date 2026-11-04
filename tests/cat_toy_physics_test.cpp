#include "../src/cat_physics.hpp"
#include "../src/cat_toy.hpp"
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {
int checks = 0;
#define CHECK(condition) do { ++checks; if (!(condition)) throw std::runtime_error( \
    std::string(__FILE__) + ":" + std::to_string(__LINE__) + " failed: " #condition); } while (false)

// Use the actual Box2D motor and interpolated display position, matching Tick.
// This catches an approach controller that looks correct algebraically but
// oscillates or never satisfies the low-speed contact gate in the application.
void Approach(cat::toy::Kind kind, double fps, double start, double destination,
              double growth, double initialVelocity, bool movingBall) {
    constexpr double low = 96, high = 1824;
    const double dt = 1.0 / fps;
    cat::physics::Controller cat;
    cat.Reset(start, low, high);
    cat.SetTargetVelocity(initialVelocity);
    for (int i = 0; i < static_cast<int>(fps * .5); ++i) cat.Update(dt);
    cat::toy::Ball ball{destination, 0};
    if (movingBall) ball.Kick(start > destination ? -1 : 1, 90);
    double target = destination, distance = 0;
    bool arrived = false;
    for (int i = 0; i < static_cast<int>(fps * 60); ++i) {
        const double center = cat.RenderPose().x;
        target = cat::toy::ApproachTarget(kind, ball.x, center, 36 * growth, low, high);
        distance = target - center;
        if (std::abs(distance) <= 2 && std::abs(cat.CurrentPose().velocityX) <= 3) {
            arrived = true;
            break;
        }
        cat.SetTargetVelocity(cat::toy::ApproachVelocity(distance));
        if (kind == cat::toy::Kind::Ball) {
            const auto [ballLow, ballHigh] = cat::toy::RollingBounds(1920, low, high, 36 * growth);
            ball.Advance(dt, ballLow, ballHigh);
        }
        cat.Update(dt);
        CHECK(cat.CurrentPose().x >= low && cat.CurrentPose().x <= high);
        CHECK(cat.CurrentPose().grounded);
    }
    if (!arrived) throw std::runtime_error("Approach failed: fps=" + std::to_string(fps) +
        " start=" + std::to_string(start) + " target=" + std::to_string(destination));
    CHECK(arrived);
    CHECK(std::abs(cat.RenderPose().x - target) <= 2);
    CHECK(std::abs(cat.CurrentPose().velocityX) <= 3);
    if (kind == cat::toy::Kind::Ball)
        CHECK(std::abs(cat.RenderPose().x - ball.x) <= 55 * growth);
    else
        CHECK(std::abs(cat.RenderPose().x - destination) <= 2);
}
}

int main() {
    try {
        for (double fps : {30.0, 60.0, 120.0})
            for (auto kind : {cat::toy::Kind::Ball, cat::toy::Kind::Box})
                for (double growth : {.82, 1.0}) {
                    Approach(kind, fps, 500, 700, growth, 60, false);
                    Approach(kind, fps, 800, 600, growth, -60, false);
                    Approach(kind, fps, 500, 96, growth, 110, false);
                    Approach(kind, fps, 1300, 1824, growth, -110, false);
                    Approach(kind, fps, 700, 700, growth, 0, false);
                    if (kind == cat::toy::Kind::Ball) {
                        Approach(kind, fps, 500, 750, growth, 60, true);
                        Approach(kind, fps, 130, 96, growth, 0, true);
                        Approach(kind, fps, 1790, 1824, growth, 0, true);
                    }
                }
        std::cout << "Toy/Box2D approach: " << checks << " checks passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
