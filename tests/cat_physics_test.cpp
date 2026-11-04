#include "../src/cat_physics.hpp"
#include <box2d/box2d.h>

#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>

namespace {
using cat::physics::Controller;
constexpr double step = cat::physics::kFixedStepSeconds;
int checks = 0;
#define CHECK(condition) do { ++checks; if (!(condition)) throw std::runtime_error( \
    std::string(__FILE__) + ":" + std::to_string(__LINE__) + " failed: " #condition); } while (false)

bool Near(double a, double b, double tolerance) { return std::abs(a-b) <= tolerance; }

void Run(Controller& cat, double seconds, double dt = step) {
    for (int i = 0; i < static_cast<int>(std::round(seconds/dt)); ++i) cat.Update(dt);
}

void ActualEngineAndRestingContact() {
    const b2Version version = b2GetVersion();
    CHECK(version.major == 3 && version.minor == 1 && version.revision == 1);
    Controller cat;
    cat.Reset(400, 40, 1560);
    Run(cat, 10);
    const auto pose = cat.CurrentPose();
    CHECK(pose.grounded);
    CHECK(Near(pose.x, 400, 0.02));
    CHECK(pose.lift <= 0.6);
    CHECK(std::abs(pose.velocityY) < 0.1);
}

void JumpGravityAndLanding() {
    Controller cat;
    cat.Reset(500, 40, 1560);
    Run(cat, 0.2);
    CHECK(cat.Jump());
    CHECK(!cat.Jump()); // Ground contact is required; no double jump.
    CHECK(!cat.CurrentPose().grounded);
    double peak = 0, landingTime = 0, impact = 0;
    int landings = 0;
    bool falling = false;
    for (int i = 1; i <= 90; ++i) {
        cat.Update(step);
        const auto pose = cat.CurrentPose();
        peak = std::max(peak, pose.lift);
        falling = falling || (pose.lift > 5 && pose.velocityY < -20);
        CHECK(pose.lift >= 0 && std::isfinite(pose.lift));
        if (pose.landed) {
            ++landings;
            landingTime = i*step;
            impact = pose.landingSpeed;
            CHECK(cat.CurrentPose().landed); // Reading does not consume event.
            CHECK(cat.RenderPose().landed);
        }
    }
    CHECK(peak > 52 && peak < 57);
    CHECK(falling);
    CHECK(landingTime > 0.65 && landingTime < 0.78);
    CHECK(landings == 1);
    CHECK(impact > 250 && impact < 340);
    CHECK(cat.CurrentPose().grounded);
    CHECK(!cat.CurrentPose().landed);
    CHECK(Near(cat.CurrentPose().x, 500, 0.02));
    CHECK(cat.Jump(25));
}

void PounceDirectionAndHeight() {
    for (int direction : {-1, 1}) {
        Controller cat;
        cat.Reset(500, 40, 1560);
        CHECK(cat.Pounce(direction));
        CHECK(!cat.Pounce(direction));
        double peak = 0;
        bool landed = false;
        for (int i = 0; i < 60; ++i) {
            cat.Update(step);
            peak = std::max(peak, cat.CurrentPose().lift);
            landed = landed || cat.CurrentPose().landed;
        }
        CHECK(peak > 27 && peak < 32);
        CHECK(landed);
        CHECK((cat.CurrentPose().x-500)*direction > 60);
        CHECK(cat.CurrentPose().grounded);
    }
}

void AccelerationBrakingAndReversal() {
    Controller cat;
    cat.Reset(500, 40, 1560);
    cat.SetTargetVelocity(60);
    cat.Update(step);
    CHECK(cat.CurrentPose().velocityX > 0 && cat.CurrentPose().velocityX < 15);
    Run(cat, 1);
    CHECK(Near(cat.CurrentPose().velocityX, 60, 1));
    const double startStopX = cat.CurrentPose().x;
    cat.SetTargetVelocity(0);
    cat.Update(step);
    CHECK(cat.CurrentPose().velocityX > 0); // Deceleration takes real time.
    Run(cat, 0.6);
    CHECK(std::abs(cat.CurrentPose().velocityX) < 1);
    CHECK(cat.CurrentPose().x > startStopX && cat.CurrentPose().x < startStopX+10);
    cat.SetTargetVelocity(-60);
    Run(cat, 1);
    CHECK(Near(cat.CurrentPose().velocityX, -60, 1));
    CHECK(cat.CurrentPose().grounded);
}

void FloorAndScreenEdgeCollisions() {
    for (int direction : {-1, 1}) {
        Controller cat;
        cat.Reset(100, 80, 120);
        cat.SetTargetVelocity(150*direction);
        Run(cat, 3);
        const auto hit = cat.CurrentPose();
        CHECK(hit.x >= 80 && hit.x <= 120);
        CHECK(Near(hit.x, direction < 0 ? 80 : 120, 0.6));
        CHECK(std::abs(hit.velocityX) < 1); // Collider stops the body itself.
        CHECK(cat.Pounce(direction, 55, 200));
        double highest = 0;
        for (int i = 0; i < 120; ++i) {
            cat.Update(step);
            CHECK(cat.CurrentPose().x >= 80 && cat.CurrentPose().x <= 120);
            highest = std::max(highest, cat.CurrentPose().lift);
        }
        CHECK(highest > 50);
        CHECK(cat.CurrentPose().grounded);
    }
}

void FrameRateIndependenceAndInterpolation() {
    Controller sixty, thirty, fast;
    for (Controller* cat : {&sixty, &thirty, &fast}) {
        cat->Reset(500, 40, 1560);
        cat->SetTargetVelocity(54);
        CHECK(cat->Jump());
    }
    Run(sixty, 0.4, 1.0/60);
    Run(thirty, 0.4, 1.0/30);
    Run(fast, 0.4, 1.0/120);
    CHECK(Near(sixty.CurrentPose().x, thirty.CurrentPose().x, 0.0001));
    CHECK(Near(sixty.CurrentPose().lift, thirty.CurrentPose().lift, 0.0001));
    CHECK(Near(sixty.CurrentPose().x, fast.CurrentPose().x, 0.0001));
    CHECK(Near(sixty.CurrentPose().lift, fast.CurrentPose().lift, 0.0001));
    const double currentX = fast.CurrentPose().x;
    const double previousRenderX = fast.RenderPose().x;
    fast.Update(step*0.5);
    CHECK(Near(fast.CurrentPose().x, currentX, 0.0001));
    CHECK(fast.RenderPose().x > previousRenderX);
    CHECK(fast.RenderPose().x < currentX);
    CHECK(Near(fast.RenderPose().x, (previousRenderX+currentX)*0.5, 0.0001));
}

void PauseStallAndCatchUpLimit() {
    Controller cat, sixSteps;
    for (Controller* item : {&cat, &sixSteps}) {
        item->Reset(500, 40, 1560);
        item->SetTargetVelocity(60);
        CHECK(item->Jump());
    }
    cat.Update(0.2);
    Run(sixSteps, 0.1);
    CHECK(Near(cat.CurrentPose().x, sixSteps.CurrentPose().x, 0.0001));
    CHECK(Near(cat.CurrentPose().lift, sixSteps.CurrentPose().lift, 0.0001));
    const auto before = cat.CurrentPose();
    cat.Update(7200.0); // PC suspend must not fast-forward physics.
    CHECK(Near(cat.CurrentPose().x, before.x, 0.0001));
    CHECK(Near(cat.CurrentPose().lift, before.lift, 0.0001));
    for (int i = 0; i < 60; ++i) cat.Update(step, true);
    CHECK(Near(cat.CurrentPose().lift, before.lift, 0.0001));
    CHECK(Near(cat.RenderPose().lift, before.lift, 0.0001));
    cat.Update(step);
    CHECK(cat.CurrentPose().lift > before.lift);
    CHECK(cat.CurrentPose().x < before.x+2);
}

void ResetBoundsAndInvalidInput() {
    Controller cat;
    cat.Reset(500, 40, 1560);
    CHECK(cat.Pounce(1));
    Run(cat, 0.2);
    cat.Reset(200, 80, 300); // Drag/monitor change clears the old trajectory.
    CHECK(cat.CurrentPose().grounded && cat.CurrentPose().lift == 0);
    CHECK(cat.CurrentPose().velocityX == 0 && cat.CurrentPose().velocityY == 0);
    CHECK(!cat.CurrentPose().landed);
    Run(cat, 1);
    CHECK(Near(cat.CurrentPose().x, 200, 0.02));
    cat.SetBounds(80, 120);
    CHECK(Near(cat.CurrentPose().x, 120, 0.001));
    Run(cat, 1);
    CHECK(cat.CurrentPose().x <= 120 && cat.CurrentPose().grounded);
    const double nan = std::numeric_limits<double>::quiet_NaN();
    CHECK(!cat.Jump(nan));
    CHECK(!cat.Jump(-1));
    cat.SetTargetVelocity(nan);
    cat.Update(nan);
    cat.Update(-1);
    CHECK(std::isfinite(cat.CurrentPose().x));
    cat.Reset(nan, nan, nan);
    Run(cat, 1);
    CHECK(std::isfinite(cat.CurrentPose().x));
    CHECK(std::isfinite(cat.CurrentPose().lift));
}

void MovingPlatformPreservesJumpAndMotor() {
    Controller moved, reference;
    for (Controller* cat : {&moved, &reference}) {
        cat->Reset(500, 100, 1000);
        cat->SetTargetVelocity(60);
        CHECK(cat->Jump());
        cat->Update(step * 3.5);
    }
    const auto before = moved.CurrentPose();
    const auto beforeRender = moved.RenderPose();
    moved.MovePlatform(300, 1200, 200);
    const auto translated = moved.CurrentPose();
    CHECK(Near(translated.x, before.x + 200, .001));
    CHECK(Near(moved.RenderPose().x, beforeRender.x + 200, .001));
    CHECK(Near(translated.lift, before.lift, .0001));
    CHECK(Near(translated.velocityY, before.velocityY, .0001));
    CHECK(Near(translated.velocityX, before.velocityX, .0001));
    CHECK(!translated.grounded && !translated.landed);
    // High-frequency window movement must not discard half-step remainders.
    for (int i = 0; i < 70; ++i) {
        moved.MovePlatform(301 + i, 1201 + i, 1);
        moved.Update(step * .5);
        reference.Update(step * .5);
        CHECK(Near(moved.CurrentPose().lift, reference.CurrentPose().lift, .001));
        CHECK(Near(moved.CurrentPose().velocityY, reference.CurrentPose().velocityY, .001));
        CHECK(Near(moved.CurrentPose().x - 201 - i, reference.CurrentPose().x, .01));
    }
    CHECK(moved.CurrentPose().velocityY < 0); // Flight advanced through its apex.
}

void ResizeClipsHorizontallyWithoutDroppingCat() {
    Controller cat;
    cat.Reset(500, 100, 1000);
    CHECK(cat.Pounce(1));
    Run(cat, .2);
    const auto before = cat.CurrentPose();
    cat.MovePlatform(100, 200, 0);
    CHECK(Near(cat.CurrentPose().x, 200, .001));
    CHECK(Near(cat.CurrentPose().lift, before.lift, .0001));
    CHECK(Near(cat.CurrentPose().velocityY, before.velocityY, .0001));
    CHECK(cat.CurrentPose().velocityX == 0);
    CHECK(!cat.CurrentPose().grounded);
    CHECK(!cat.CurrentPose().landed);
    Run(cat, 1);
    CHECK(cat.CurrentPose().grounded);
    CHECK(cat.CurrentPose().x <= 200);
    cat.MovePlatform(100, 200, std::numeric_limits<double>::quiet_NaN());
    CHECK(std::isfinite(cat.CurrentPose().x));
}

void WorldLifetime() {
    const int before = b2GetByteCount();
    for (int i = 0; i < 10; ++i) {
        Controller cat;
        cat.Reset(100, 20, 500);
        cat.Jump();
        Run(cat, 0.1);
    }
    CHECK(b2GetByteCount() == before);
}
} // namespace

int main() {
    try {
        ActualEngineAndRestingContact();
        JumpGravityAndLanding();
        PounceDirectionAndHeight();
        AccelerationBrakingAndReversal();
        FloorAndScreenEdgeCollisions();
        FrameRateIndependenceAndInterpolation();
        PauseStallAndCatchUpLimit();
        ResetBoundsAndInvalidInput();
        MovingPlatformPreservesJumpAndMotor();
        ResizeClipsHorizontallyWithoutDroppingCat();
        WorldLifetime();
        std::cout << "Box2D physics: " << checks << " checks passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
