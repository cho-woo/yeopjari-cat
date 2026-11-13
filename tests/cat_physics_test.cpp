#include "../src/cat_physics.hpp"
#include "../src/cat_environment.hpp"
#include "../src/cat_animation.hpp"
#include "../src/cat_lively_motion.hpp"
#include "../src/cat_gait_transition.hpp"
#include "../src/cat_running_turn.hpp"
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

void RenderedStrideFollowsEveryRenderedSubstep() {
    using cat::animation::Motion;
    for(double dt:{1./30,1./60,1./120,1./144,.016}) {
        Controller cat;
        cat.Reset(500,96,1824);
        cat.SetTargetVelocity(120);
        Run(cat,1);
        cat::animation::Locomotion gait;
        int contactsChecked=0;
        for(int frame=0;frame<60;++frame) {
            const auto before=cat.RenderPose();
            const double oldPhase=gait.Phase();
            const auto oldFeet=gait.Feet(Motion::Walk);
            cat.Update(dt);
            const auto after=cat.RenderPose();
            const double visibleTravel=after.relativeTravelX-before.relativeTravelX;
            CHECK(Near(visibleTravel,after.x-before.x,.000001));
            CHECK(gait.Advance(Motion::Walk,visibleTravel,dt));
            const auto feet=gait.Feet(Motion::Walk);
            for(unsigned leg=0;leg<feet.size();++leg) {
                if(oldFeet[leg].stance&&feet[leg].stance&&oldFeet[leg].contact==feet[leg].contact) {
                    CHECK(Near(before.x+oldFeet[leg].x,after.x+feet[leg].x,.000001));
                    ++contactsChecked;
                }
            }
            // Cadence adapts to real speed. Even a partial Box2D render step
            // must advance it and preserve planted world coordinates.
            CHECK(std::fmod(gait.Phase()-oldPhase+1,1)>.001);
            CHECK(visibleTravel>.1); //120/144Hz cannot alternate zero/double steps.
        }
        CHECK(contactsChecked>60);
    }
}

void RenderedSupportTravelAndHalfStepPauseStayContinuous() {
    Controller cat;
    cat.Reset(500,96,1824);
    cat.SetPlatform(250,1350,180);
    CHECK(cat.LaunchTo(500,180));
    Run(cat,3);
    constexpr double dt=1./120;
    double left=250;
    for(int i=0;i<240;++i) {
        left+=30*dt;cat.SetPlatform(left,left+1100,180);
        cat.Update(dt);
    }
    for(int i=0;i<60;++i) {
        const auto before=cat.RenderPose();
        left+=30*dt;cat.SetPlatform(left,left+1100,180);
        cat.Update(dt);
        const auto after=cat.RenderPose();
        CHECK(after.onPlatform);
        CHECK(Near(after.relativeTravelX-before.relativeTravelX,after.x-before.x-30*dt,.0002));
        CHECK(std::abs(after.relativeTravelX-before.relativeTravelX)<.001); // Carrying is not walking.
    }
    cat.SetTargetVelocity(45);
    for(int i=0;i<120;++i) {left+=30*dt;cat.SetPlatform(left,left+1100,180);cat.Update(dt);}
    for(int i=0;i<60;++i) {
        const auto before=cat.RenderPose();
        left+=30*dt;cat.SetPlatform(left,left+1100,180);cat.Update(dt);
        const auto after=cat.RenderPose();
        CHECK(Near(after.relativeTravelX-before.relativeTravelX,after.x-before.x-30*dt,.0002));
        CHECK(Near(after.relativeTravelX-before.relativeTravelX,45*dt,.001));
    }

    Controller paused,reference;
    for(Controller* item:{&paused,&reference}) {
        item->Reset(500,96,1824);item->SetTargetVelocity(80);
        item->Update(1./60);item->Update(1./120); // Exactly half an interpolated step.
    }
    const auto drawn=paused.RenderPose();
    const auto actual=paused.CurrentPose();
    CHECK(drawn.x<actual.x);
    for(double stall:{0.,.1,30.,7200.}) {
        paused.Update(stall,stall<=.25);
        CHECK(Near(paused.RenderPose().x,drawn.x,0));
        CHECK(Near(paused.RenderPose().relativeTravelX,drawn.relativeTravelX,0));
        CHECK(Near(paused.CurrentPose().x,actual.x,0));
    }
    paused.Update(dt);reference.Update(dt);
    CHECK(Near(paused.RenderPose().x,reference.RenderPose().x,0));
    CHECK(Near(paused.RenderPose().relativeTravelX,reference.RenderPose().relativeTravelX,0));
    const auto travelBeforeRelocation=paused.RenderPose().relativeTravelX;
    paused.RelocateBounds(196,1924,100);
    CHECK(Near(paused.RenderPose().relativeTravelX,travelBeforeRelocation,0));
}

void PauseStallAndCatchUpLimit() {
    Controller cat, twelveSteps;
    for (Controller* item : {&cat, &twelveSteps}) {
        item->Reset(500, 40, 1560);
        item->SetTargetVelocity(60);
        CHECK(item->Jump());
    }
    cat.Update(0.2);
    Run(twelveSteps, 0.2);
    CHECK(Near(cat.CurrentPose().x, twelveSteps.CurrentPose().x, 0.0001));
    CHECK(Near(cat.CurrentPose().lift, twelveSteps.CurrentPose().lift, 0.0001));
    const auto before = cat.CurrentPose();
    const auto drawnBeforePause = cat.RenderPose();
    cat.Update(7200.0); // PC suspend must not fast-forward physics.
    CHECK(Near(cat.CurrentPose().x, before.x, 0.0001));
    CHECK(Near(cat.CurrentPose().lift, before.lift, 0.0001));
    for (int i = 0; i < 60; ++i) cat.Update(step, true);
    CHECK(Near(cat.CurrentPose().lift, before.lift, 0.0001));
    CHECK(Near(cat.RenderPose().lift, drawnBeforePause.lift, 0.0001));
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

void ViewportRelocationPreservesJumpAndMotor() {
    Controller moved, reference;
    for (Controller* cat : {&moved, &reference}) {
        cat->Reset(500, 100, 1000);
        cat->SetTargetVelocity(60);
        CHECK(cat->Jump());
        cat->Update(step * 3.5);
    }
    const auto before = moved.CurrentPose();
    const auto beforeRender = moved.RenderPose();
    moved.RelocateBounds(300, 1200, 200);
    const auto translated = moved.CurrentPose();
    CHECK(Near(translated.x, before.x + 200, .001));
    CHECK(Near(moved.RenderPose().x, beforeRender.x + 200, .001));
    CHECK(Near(translated.lift, before.lift, .0001));
    CHECK(Near(translated.velocityY, before.velocityY, .0001));
    CHECK(Near(translated.velocityX, before.velocityX, .0001));
    CHECK(!translated.grounded && !translated.landed);
    // Coordinate relocation must not discard half-step remainders.
    for (int i = 0; i < 70; ++i) {
        moved.RelocateBounds(301 + i, 1201 + i, 1);
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
    cat.RelocateBounds(100, 200, 0);
    CHECK(Near(cat.CurrentPose().x, 200, .001));
    CHECK(Near(cat.CurrentPose().lift, before.lift, .0001));
    CHECK(Near(cat.CurrentPose().velocityY, before.velocityY, .0001));
    CHECK(cat.CurrentPose().velocityX == 0);
    CHECK(!cat.CurrentPose().grounded);
    CHECK(!cat.CurrentPose().landed);
    Run(cat, 1);
    CHECK(cat.CurrentPose().grounded);
    CHECK(cat.CurrentPose().x <= 200);
    cat.RelocateBounds(100, 200, std::numeric_limits<double>::quiet_NaN());
    CHECK(std::isfinite(cat.CurrentPose().x));
}

void BallisticTransferAndOneWayLanding() {
    Controller cat;
    cat.Reset(500, 40, 960);
    cat.SetPlatform(250, 900, 180);
    CHECK(cat.CanLaunchTo(500, 180));
    CHECK(cat.LaunchTo(500, 180));
    CHECK(!cat.LaunchTo(500, 180));
    const double launchVy = cat.CurrentPose().velocityY;
    double peak = 0;
    int landings = 0;
    bool roseThrough = false;
    for (int i = 1; i <= 180; ++i) {
        cat.Update(step);
        const auto pose = cat.CurrentPose();
        peak = std::max(peak, pose.lift);
        if (pose.velocityY > 40) {
            CHECK(!pose.grounded && !pose.onPlatform);
            CHECK(Near(pose.velocityY, launchVy - 900*i*step, .03));
            roseThrough = roseThrough || pose.lift > 180;
        }
        if (pose.landed) {
            ++landings;
            CHECK(pose.grounded && pose.onPlatform);
            CHECK(pose.landingSpeed > 250 && pose.landingSpeed < 350);
        }
    }
    CHECK(roseThrough);
    CHECK(peak > 222 && peak < 226);
    CHECK(landings == 1);
    CHECK(cat.OnPlatform() && cat.CurrentPose().grounded);
    CHECK(Near(cat.CurrentPose().lift, 180, .6));
    CHECK(cat.Jump(55));
    Run(cat, 1.5);
    CHECK(cat.OnPlatform()); // Ordinary jump returns to the same real ledge.
    CHECK(Near(cat.CurrentPose().lift, 180, .6));
}

void FlightIgnoresMotorAndTargetChanges() {
    Controller cat, reference;
    for (auto* item : {&cat, &reference}) {
        item->Reset(200, 40, 960);
        item->SetPlatform(450, 750, 220);
        CHECK(item->LaunchTo(600, 220));
    }
    const double launchVx = cat.CurrentPose().velocityX;
    for (int i = 0; i < 45; ++i) {
        const auto before = cat.CurrentPose();
        const auto renderBefore = cat.RenderPose();
        cat.SetPlatform(450+i*2, 750+i*2, 220);
        CHECK(Near(cat.CurrentPose().x, before.x, 0));
        CHECK(Near(cat.CurrentPose().lift, before.lift, 0));
        CHECK(Near(cat.RenderPose().x, renderBefore.x, 0));
        cat.SetTargetVelocity(-200); // Flight momentum is not an air motor.
        cat.Update(step);
        reference.Update(step);
        CHECK(Near(cat.CurrentPose().x, reference.CurrentPose().x, .001));
        CHECK(Near(cat.CurrentPose().lift, reference.CurrentPose().lift, .001));
        CHECK(Near(cat.CurrentPose().velocityX, launchVx, .001));
    }
    const auto before = cat.CurrentPose();
    const auto renderBefore = cat.RenderPose();
    cat.RemovePlatform();
    CHECK(Near(cat.CurrentPose().x, before.x, 0));
    CHECK(Near(cat.CurrentPose().lift, before.lift, 0));
    CHECK(Near(cat.CurrentPose().velocityX, before.velocityX, 0));
    CHECK(Near(cat.CurrentPose().velocityY, before.velocityY, 0));
    CHECK(Near(cat.RenderPose().lift, renderBefore.lift, 0));
    bool landedOnFloor = false;
    for (int i = 0; i < 90; ++i) {
        cat.Update(step);
        const auto pose = cat.CurrentPose();
        CHECK(!pose.onPlatform);
        if (pose.landed) { landedOnFloor = true; CHECK(pose.lift < 1); }
    }
    CHECK(landedOnFloor && cat.CurrentPose().grounded);
}

void ActualMovingSupportAndRemoval() {
    Controller cat;
    cat.Reset(500, 40, 1560);
    cat.SetPlatform(250, 900, 180);
    CHECK(cat.LaunchTo(500, 180));
    Run(cat, 3);
    const double startX = cat.CurrentPose().x;
    double supportTravel = 0;
    for (int i = 1; i <= 120; ++i) {
        cat.SetPlatform(250+i*.5, 900+i*.5, 180+i*.5);
        cat.Update(step);
        const auto pose = cat.CurrentPose();
        CHECK(pose.grounded && pose.onPlatform);
        CHECK(Near(pose.lift, 180+i*.5, .7));
        CHECK(Near(pose.supportVelocityX, 30, .02));
        CHECK(Near(pose.supportTravelX, .5, .001));
        CHECK(Near(cat.CurrentPose().supportTravelX, pose.supportTravelX, 0));
        supportTravel += pose.supportTravelX;
    }
    CHECK(Near(supportTravel, 60, .02));
    CHECK(cat.CurrentPose().x > startX+55 && cat.CurrentPose().x < startX+60);
    CHECK(Near(cat.CurrentPose().velocityX - cat.CurrentPose().supportVelocityX, 0, .02));
    cat.Update(0); // Per-update support travel is an event, not sticky state.
    CHECK(cat.CurrentPose().supportTravelX == 0);
    const auto before = cat.CurrentPose();
    cat.RemovePlatform();
    const auto removed = cat.CurrentPose();
    CHECK(!removed.grounded && !removed.onPlatform);
    CHECK(removed.supportVelocityX == 0 && removed.supportTravelX == 0);
    CHECK(Near(removed.x, before.x, 0) && Near(removed.lift, before.lift, 0));
    CHECK(Near(removed.velocityX, before.velocityX, 0));
    CHECK(Near(removed.velocityY, before.velocityY, 0));
    cat.Update(step);
    CHECK(Near(cat.CurrentPose().velocityY, before.velocityY-15, .02));
    CHECK(Near(cat.CurrentPose().velocityX, before.velocityX, .02));
    Run(cat, 2);
    CHECK(cat.CurrentPose().grounded && !cat.OnPlatform());
}

void MovedOrResizedAirborneTargetsNeverLeaveInvisibleSupport() {
    // Move the destination just before touchdown, where a trailing kinematic
    // collider used to arrest gravity and falsely report a landing in midair.
    // The reference has no ledge at all: each missed trajectory must match it.
    for (int fps : {30, 60, 120, 144}) for (int change = 0; change < 5; ++change) {
        Controller cat, reference;
        for (auto* item : {&cat, &reference}) {
            item->Reset(500, 40, 1560);
            item->SetPlatform(250, 900, 180);
            CHECK(item->LaunchTo(500, 180));
        }
        const double dt = 1.0/fps;
        int frame = 0;
        while ((cat.CurrentPose().velocityY >= 0 || cat.CurrentPose().lift > 192) && frame++ < fps*2) {
            cat.Update(dt); reference.Update(dt);
        }
        const auto before = cat.CurrentPose(), drawn = cat.RenderPose();
        CHECK(before.lift > 180 && before.velocityY < -200);
        if (change == 0) cat.SetPlatform(1050, 1700, 180); // Same-size window moved away.
        if (change == 1) cat.SetPlatform(1050, 1500, 180); // Move and resize together.
        if (change == 2) cat.SetPlatform(250, 400, 180); // Right edge shrank past the cat.
        if (change == 3) cat.SetPlatform(0, 1600, 900); // Maximized top is above the jump apex.
        if (change == 4) cat.RemovePlatform(); // Closed/invalid top cannot remain a destination.
        reference.RemovePlatform();
        CHECK(Near(cat.CurrentPose().x, before.x, 0));
        CHECK(Near(cat.CurrentPose().lift, before.lift, 0));
        CHECK(Near(cat.CurrentPose().velocityX, before.velocityX, 0));
        CHECK(Near(cat.CurrentPose().velocityY, before.velocityY, 0));
        CHECK(Near(cat.RenderPose().lift, drawn.lift, 0));
        int landings = 0;
        for (int i = 0; i < fps*2; ++i) {
            // A missed landing must not enable the ground motor in midair.
            cat.SetTargetVelocity(-100); reference.SetTargetVelocity(-100);
            cat.Update(dt); reference.Update(dt);
            const auto actual = cat.CurrentPose(), expected = reference.CurrentPose();
            CHECK(!actual.onPlatform);
            CHECK(actual.grounded == expected.grounded);
            CHECK(Near(actual.x, expected.x, .002));
            CHECK(Near(actual.lift, expected.lift, .002));
            CHECK(Near(actual.velocityX, expected.velocityX, .002));
            CHECK(Near(actual.velocityY, expected.velocityY, .002));
            if (actual.landed) { ++landings; CHECK(actual.lift < 1); }
        }
        CHECK(landings == 1 && cat.CurrentPose().grounded);
    }
}

void ResizedOrRemovedOccupiedGeometryReleasesSupport() {
    for (bool resize : {false, true}) {
        Controller cat;
        cat.Reset(500, 40, 1560);
        cat.SetPlatform(250, 900, 180);
        CHECK(cat.LaunchTo(500, 180));
        Run(cat, 3);
        CHECK(cat.OnPlatform());
        const auto before = cat.CurrentPose();
        cat.SetPlatform(1050, resize ? 1500 : 1700, 180);
        CHECK(!cat.OnPlatform() && !cat.CurrentPose().grounded);
        CHECK(Near(cat.CurrentPose().x, before.x, 0));
        CHECK(Near(cat.CurrentPose().lift, before.lift, 0));
        cat.Update(step);
        CHECK(Near(cat.CurrentPose().velocityY, before.velocityY - 15, .03));
        CHECK(cat.CurrentPose().lift < before.lift);
        Run(cat, 2);
        CHECK(cat.CurrentPose().grounded && !cat.OnPlatform());
    }
}

void DescendingSupportDoesNotMagnetizeCat() {
    Controller cat;
    cat.Reset(500, 40, 960);
    cat.SetPlatform(250, 900, 220);
    CHECK(cat.LaunchTo(500, 220));
    Run(cat, 3);
    const auto before = cat.CurrentPose();
    cat.SetPlatform(250, 900, 100);
    CHECK(Near(cat.CurrentPose().lift, before.lift, 0));
    cat.Update(step);
    const auto falling = cat.CurrentPose();
    CHECK(!falling.grounded && !falling.onPlatform);
    CHECK(falling.lift > 219); // Window moves 25 px; cat falls under gravity.
    CHECK(Near(falling.velocityY, -15, .03));
    Run(cat, 2);
    CHECK(cat.OnPlatform());
    CHECK(Near(cat.CurrentPose().lift, 100, .6));
}

void PlatformFrameRatesAndCatchUp() {
    Controller reference;
    reference.Reset(200, 40, 960);
    reference.SetPlatform(450, 750, 220);
    CHECK(reference.LaunchTo(600, 220));
    Run(reference, 3);
    for (double dt : {1./30, 1./60, 1./120, 1./144, .2, .25}) {
        Controller cat;
        cat.Reset(200, 40, 960);
        cat.SetPlatform(450, 750, 220);
        CHECK(cat.LaunchTo(600, 220));
        Run(cat, 3, dt);
        CHECK(Near(cat.CurrentPose().x, reference.CurrentPose().x, .001));
        CHECK(Near(cat.CurrentPose().lift, reference.CurrentPose().lift, .001));
        CHECK(Near(cat.CurrentPose().velocityX, reference.CurrentPose().velocityX, .001));
        CHECK(cat.OnPlatform() && cat.CurrentPose().grounded);
    }
}

void CeilingAndReachability() {
    Controller cat;
    cat.Reset(500, 40, 960);
    cat.SetCeiling(360);
    CHECK(!cat.CanLaunchTo(500, 340)); // Apex needs headroom, too.
    CHECK(!cat.CanLaunchTo(500, 521));
    CHECK(!cat.CanLaunchTo(-20, 200));
    CHECK(!cat.CanLaunchTo(900, 10, 10)); // Horizontal speed would exceed limit.
    CHECK(!cat.CanLaunchTo(500, 100, 0));
    CHECK(!cat.CanLaunchTo(std::numeric_limits<double>::quiet_NaN(), 100));
    cat.SetPlatform(250, 900, 180);
    CHECK(cat.LaunchTo(500, 180));
    Run(cat, 3);
    cat.SetPlatform(250, 900, 300); // Fast upward window gives a real impulse.
    double peak = 0;
    bool leftSupport = false;
    for (int i = 0; i < 180; ++i) {
        cat.Update(step);
        const auto pose = cat.CurrentPose();
        CHECK(pose.lift <= 362); // Static roof contact limits upward momentum.
        peak = std::max(peak, pose.lift);
        leftSupport = leftSupport || !pose.grounded;
    }
    CHECK(peak > 355 && leftSupport);
    CHECK(cat.OnPlatform() && Near(cat.CurrentPose().lift, 300, .6));
    const double before = cat.CurrentPose().lift;
    cat.SetPlatform(250, 900, 400); // Insufficient body clearance: detach safely.
    CHECK(!cat.OnPlatform() && !cat.CurrentPose().grounded);
    CHECK(Near(cat.CurrentPose().lift, before, 0));
    Run(cat, 2);
    CHECK(cat.CurrentPose().grounded && cat.CurrentPose().lift < 1);

    Controller runway;
    runway.Reset(200, 40, 960);
    runway.SetPlatform(450, 750, 220);
    CHECK(runway.CanLaunchTo(600, 220));
    CHECK(!runway.CanLaunchTo(700, 220));
    CHECK(!runway.LaunchTo(700, 220));
    CHECK(runway.CurrentPose().grounded && runway.CurrentPose().lift == 0);
}

void TallViewportContainsRaisedPlatformLaunch() {
    for (int direction : {-1, 1}) {
        Controller cat;
        cat.Reset(direction > 0 ? 1700 : 220, 96, 1824);
        cat.SetCeiling(2000);
        const double left = direction > 0 ? 1000 : 0;
        const double right = direction > 0 ? 1920 : 920;
        cat.SetPlatform(left, right, 180);
        CHECK(cat.LaunchTo(direction > 0 ? 1700 : 220, 180));
        Run(cat, 3);
        cat.SetTargetVelocity(direction*450);
        Run(cat, 40*step);
        cat.SetPlatform(left, right, 500);
        double peak = 0;
        bool landedAgain = false;
        for (int i = 0; i < 600; ++i) {
            cat.Update(step);
            const auto pose = cat.CurrentPose();
            peak = std::max(peak, pose.lift);
            landedAgain = landedAgain || pose.landed;
            CHECK(pose.x >= 96 && pose.x <= 1824);
            CHECK(pose.lift <= 2002);
            CHECK(pose.velocityY > -2000);
        }
        CHECK(peak > 1000); // Exercises the space above the old 975 px wall.
        CHECK(landedAgain && cat.CurrentPose().grounded);
        CHECK(cat.OnPlatform());
        CHECK(Near(cat.CurrentPose().lift, 500, .6));
        CHECK(std::abs(cat.CurrentPose().velocityX) < 1);
    }
}

void DirectWindowTransfersKeepSourceUntilTakeoff() {
    for (double destinationHeight : {80., 180., 320.}) {
        Controller cat;
        cat.Reset(500, 40, 1560);
        cat.SetPlatform(250, 750, 180);
        CHECK(cat.LaunchTo(500, 180));
        Run(cat, 3);
        const auto before = cat.CurrentPose();
        for (int i = 0; i < 20; ++i) {
            CHECK(cat.CanLaunchToPlatform(650, 1100, destinationHeight, 780));
            CHECK(cat.OnPlatform());
            CHECK(Near(cat.CurrentPose().lift, before.lift, 0));
            CHECK(Near(cat.CurrentPose().x, before.x, 0));
        }
        CHECK(!cat.LaunchToPlatform(650, 1100, destinationHeight, 1090));
        CHECK(cat.OnPlatform() && Near(cat.CurrentPose().lift, before.lift, 0));
        CHECK(cat.LaunchToPlatform(650, 1100, destinationHeight, 780));
        CHECK(!cat.OnPlatform() && !cat.CurrentPose().grounded);
        CHECK(Near(cat.CurrentPose().x, before.x, 0));
        CHECK(Near(cat.CurrentPose().lift, before.lift, 0));
        int landings = 0;
        for (int i = 0; i < 240; ++i) {
            cat.Update(step);
            if (cat.CurrentPose().landed) ++landings;
        }
        CHECK(landings == 1);
        CHECK(cat.OnPlatform());
        CHECK(Near(cat.CurrentPose().lift, destinationHeight, .6));
        CHECK(cat.CurrentPose().x > 650 && cat.CurrentPose().x < 1100);
    }
}

void HighWindowsUseRealRiseAndAvailableHeadroom() {
    // Ordinary browser/explorer tops can be 700–900 logical pixels above
    // the taskbar. The former absolute 520 px limit rejected every such
    // ledge even when the same ballistic solver had sufficient headroom.
    for (double height : {700., 900.}) {
        cat::physics::Pose reference;
        for (int fps : {60, 30, 120, 144}) {
            Controller cat;
            cat.Reset(500, 96, 1824);
            cat.SetCeiling(1040);
            CHECK(cat.CanLaunchToPlatform(350, 950, height, 650));
            CHECK(cat.LaunchToPlatform(350, 950, height, 650));
            CHECK(Near(cat.CurrentPose().x, 500, 0));
            CHECK(Near(cat.CurrentPose().lift, 0, 0));
            int landings = 0;
            double peak = 0;
            for (int frame = 0; frame < fps*4; ++frame) {
                cat.Update(1./fps);
                const auto pose = cat.CurrentPose();
                peak = std::max(peak, pose.lift);
                if (pose.landed) ++landings;
                CHECK(pose.lift >= 0 && pose.lift <= height+46);
            }
            CHECK(peak > height+40 && peak < height+46);
            CHECK(landings == 1 && cat.OnPlatform());
            CHECK(Near(cat.CurrentPose().lift, height, .6));
            if (fps == 60) reference = cat.CurrentPose();
            CHECK(Near(cat.CurrentPose().x, reference.x, .001));
            CHECK(Near(cat.CurrentPose().lift, reference.lift, .001));
        }
    }

    Controller cat;
    cat.Reset(500, 96, 1824);
    cat.SetCeiling(1500);
    CHECK(!cat.CanLaunchToPlatform(350, 950, 1400, 650)); // Finite rise budget, even under a tall roof.
    CHECK(cat.LaunchToPlatform(350, 950, 700, 650));
    Run(cat, 4);
    CHECK(cat.OnPlatform());
    const auto before = cat.CurrentPose();
    CHECK(cat.CanLaunchToPlatform(900, 1500, 1400, 1100)); // Rise is 700, not absolute altitude 1400.
    CHECK(!cat.LaunchToPlatform(900, 1500, 1490, 1100)); // Apex would hit the work-area roof.
    CHECK(cat.OnPlatform());
    CHECK(Near(cat.CurrentPose().lift, before.lift, 0));
    CHECK(Near(cat.CurrentPose().x, before.x, 0));
    CHECK(cat.LaunchToPlatform(900, 1500, 1400, 1100));
    CHECK(Near(cat.CurrentPose().lift, before.lift, 0));
    CHECK(Near(cat.CurrentPose().x, before.x, 0));
    Run(cat, 4);
    CHECK(cat.OnPlatform() && Near(cat.CurrentPose().lift, 1400, .6));
    CHECK(cat.LaunchToPlatform(1000, 1750, 1400, 1350)); // Same-height transfer above the old cap.
    Run(cat, 4);
    CHECK(cat.OnPlatform() && Near(cat.CurrentPose().lift, 1400, .6));
    CHECK(cat.LaunchToPlatform(1000, 1750, 300, 1350)); // Long descent must keep the one-way top contact.
    Run(cat, 4);
    CHECK(cat.OnPlatform() && Near(cat.CurrentPose().lift, 300, .6));
    const auto supported = cat.CurrentPose();
    cat.RemovePlatform();
    CHECK(Near(cat.CurrentPose().lift, supported.lift, 0));
    CHECK(!cat.CurrentPose().grounded);
    cat.Update(step);
    CHECK(Near(cat.CurrentPose().velocityY, -15, .03));
    Run(cat, 3);
    CHECK(cat.CurrentPose().grounded && cat.CurrentPose().lift < 1);
}

void OneWayLandingDoesNotDependOnFixedClockPhase() {
    // Heights and clearance change where the feet cross a ledge relative to
    // a 60 Hz physics step. A landing must survive every such contact phase.
    for (int height = 115; height < 1000; height += 13) {
        for (double clearance : {20., 45., 80.}) {
            Controller cat;
            cat.Reset(500, 96, 1824);
            cat.SetCeiling(1200);
            CHECK(cat.LaunchToPlatform(350, 1200, height, 650, clearance));
            bool passedAbove = false, contacted = false;
            for (int frame = 0; frame < 240; ++frame) {
                cat.Update(step);
                const auto pose = cat.CurrentPose();
                if (pose.lift > height + 5) passedAbove = true;
                if (pose.onPlatform) { CHECK(passedAbove); contacted = true; }
            }
            CHECK(contacted && cat.OnPlatform());
            CHECK(Near(cat.CurrentPose().lift, height, .6));
        }
    }
}

void DisablingCursorPlayLeavesOrdinaryWindowPlanningAvailable() {
    cat::environment::CursorReaction reaction;
    cat::environment::CursorInput input;
    input.available = input.enabled = input.canStart = input.canContinue = true;
    input.catX = 500; input.cursorX = 650; input.cursorHeight = 60;
    input.minX = 40; input.maxX = 960; input.interest = 1; input.roll = 0;
    for (int i = 0; i <= 45; ++i) {
        input.nowMs = i*16;
        reaction.Step(input);
    }
    CHECK(reaction.Action() == cat::environment::CursorAction::Hunt);
    input.nowMs += 16; input.enabled = false;
    CHECK(reaction.Step(input).action == cat::environment::CursorAction::None);
    CHECK(!reaction.Active());

    // Window visits are the pre-existing roaming behavior. Cursor play's
    // default-off setting must not revoke their independent physical plan.
    Controller cat;
    cat.Reset(500, 40, 960);
    double landing = 0;
    CHECK(cat::environment::ChooseLanding(cat.CurrentPose().x, 250, 900,
        [&](double x) { return cat.CanLaunchToPlatform(250, 900, 180, x); }, landing));
    CHECK(cat.LaunchToPlatform(250, 900, 180, landing));
    Run(cat, 3);
    CHECK(cat.OnPlatform() && Near(cat.CurrentPose().lift, 180, .6));
    CHECK(!reaction.Active());
}

void CursorSitApproachesWithMotorAndWaitsForBraking() {
    for (int fps : {30, 60, 120, 144}) {
        Controller cat;
        cat.Reset(400, 96, 960);
        cat::environment::CursorReaction reaction;
        cat::environment::CursorInput input;
        input.available = input.enabled = input.canStart = input.canContinue = true;
        input.cursorX = 430; input.cursorHeight = 30; input.minX = 96; input.maxX = 960;
        input.interest = 1; input.roll = 0;
        bool braking = false, approached = false, sat = false, finished = false;
        std::uint64_t previousTime = 0;
        for (int frame = 0; frame < fps*9; ++frame) {
            input.nowMs = static_cast<std::uint64_t>(std::round(frame*1000.0/fps));
            input.catX = cat.CurrentPose().x;
            if (braking) {
                reaction.Pause(input.nowMs - previousTime);
                cat.SetTargetVelocity(0);
                braking = std::abs(cat.CurrentPose().velocityX) > 2;
            } else {
                const auto output = reaction.Step(input);
                approached = approached || output.action == cat::environment::CursorAction::ApproachSit;
                braking = output.changed && output.action == cat::environment::CursorAction::Sit &&
                    std::abs(cat.CurrentPose().velocityX) > 2;
                cat.SetTargetVelocity(braking ? 0 : output.velocity);
                if (output.action == cat::environment::CursorAction::Sit && !braking) {
                    sat = true;
                    CHECK(std::abs(cat.CurrentPose().velocityX) <= 2);
                    CHECK(std::abs(cat.CurrentPose().x - input.cursorX) <= 4);
                }
                finished = finished || (sat && output.action == cat::environment::CursorAction::None);
            }
            cat.Update(1.0/fps);
            CHECK(cat.CurrentPose().grounded);
            previousTime = input.nowMs;
        }
        CHECK(approached && sat && finished);
        CHECK(std::abs(cat.CurrentPose().x - input.cursorX) <= 4);
    }
}

void LedgeGripUsesJointAndReleasesMomentum() {
    for (int side : {-1, 1}) {
        for (bool climb : {false, true}) {
            Controller cat;
            const double x = side < 0 ? 330 : 670;
            const double edge = side < 0 ? 250 : 750;
            cat.Reset(x, 40, 960);
            cat.SetCeiling(500);
            cat.SetPlatform(250, 750, 180);
            CHECK(!cat.BeginHang(edge, 180)); // Floor is not a ledge grip.
            CHECK(cat.LaunchTo(x, 180));
            Run(cat, 3);
            const auto before = cat.CurrentPose();
            CHECK(!cat.BeginHang(500, 180)); // Grip must be an actual finite edge.
            CHECK(!cat.BeginHang(edge, 300));
            CHECK(cat.BeginHang(edge, 180));
            CHECK(cat.Hanging() && cat.CurrentPose().hanging);
            CHECK(!cat.OnPlatform() && !cat.CurrentPose().grounded);
            CHECK(Near(cat.CurrentPose().x, before.x, 0));
            CHECK(Near(cat.CurrentPose().lift, before.lift, 0));
            CHECK(!cat.BeginHang(edge, 180) && !cat.Jump());
            for (int i = 0; i < 90; ++i) {
                cat.SetTargetVelocity(450); // A joint grip suppresses the walking motor.
                cat.Update(step);
                const auto p = cat.CurrentPose();
                CHECK(p.hanging && !p.grounded);
                CHECK(p.lift > 75 && p.lift < 181);
                CHECK(std::abs(p.velocityX) < 200 && std::abs(p.velocityY) < 200);
            }
            const auto held = cat.CurrentPose();
            CHECK(held.lift > 110 && held.lift < 125);
            cat.Update(30);
            CHECK(Near(cat.CurrentPose().lift, held.lift, 0));
            CHECK(cat.EndHang(climb) == climb);
            CHECK(!cat.Hanging());
            CHECK(Near(cat.CurrentPose().x, held.x, 0));
            CHECK(Near(cat.CurrentPose().lift, held.lift, 0));
            if (!climb) {
                CHECK(Near(cat.CurrentPose().velocityX, held.velocityX, 0));
                CHECK(Near(cat.CurrentPose().velocityY, held.velocityY, 0));
                cat.Update(step);
                CHECK(Near(cat.CurrentPose().velocityY, held.velocityY - 15, .03));
            }
            cat.SetTargetVelocity(0);
            Run(cat, 3);
            CHECK(cat.CurrentPose().grounded);
            CHECK(cat.OnPlatform() == climb);
            CHECK(Near(cat.CurrentPose().lift, climb ? 180 : 0, .6));
        }
    }
}

void MovingOrClosingGripNeverTeleports() {
    Controller cat;
    cat.Reset(330, 40, 960);
    cat.SetPlatform(250, 750, 180);
    CHECK(cat.LaunchTo(330, 180));
    Run(cat, 3);
    CHECK(cat.BeginHang(250, 180));
    Run(cat, 1.5);
    const auto beforeMove = cat.CurrentPose();
    cat.SetPlatform(280, 780, 180);
    CHECK(Near(cat.CurrentPose().x, beforeMove.x, 0));
    CHECK(Near(cat.CurrentPose().lift, beforeMove.lift, 0));
    Run(cat, .5);
    CHECK(cat.Hanging());
    CHECK(cat.CurrentPose().x > beforeMove.x + 20);
    const auto held = cat.CurrentPose();
    cat.RemovePlatform();
    CHECK(!cat.Hanging() && !cat.CurrentPose().grounded);
    CHECK(Near(cat.CurrentPose().lift, held.lift, 0));
    CHECK(Near(cat.CurrentPose().velocityY, held.velocityY, 0));
    cat.Update(step);
    CHECK(Near(cat.CurrentPose().velocityY, held.velocityY - 15, .03));
    Run(cat, 2);
    CHECK(cat.CurrentPose().grounded && cat.CurrentPose().lift < 1);
}

void HangingFixedStepsAndWorldCleanup() {
    cat::physics::Pose reference;
    for (double dt : {1./60, 1./30, 1./120, 1./144, .2, .25}) {
        Controller cat;
        cat.Reset(330, 40, 960);
        cat.SetPlatform(250, 750, 180);
        CHECK(cat.LaunchTo(330, 180));
        Run(cat, 3);
        CHECK(cat.BeginHang(250, 180));
        Run(cat, 1, dt);
        if (dt == 1./60) reference = cat.CurrentPose();
        CHECK(Near(cat.CurrentPose().x, reference.x, .001));
        CHECK(Near(cat.CurrentPose().lift, reference.lift, .001));
        CHECK(Near(cat.CurrentPose().velocityX, reference.velocityX, .001));
    }
    const int before = b2GetByteCount();
    {
        Controller cat;
        cat.Reset(330, 40, 960); cat.SetPlatform(250, 750, 180);
        CHECK(cat.LaunchTo(330, 180)); Run(cat, 3);
        CHECK(cat.BeginHang(250, 180)); Run(cat, .5);
        // Destroy a live joint together with its world, as shutdown/drag does.
    }
    CHECK(b2GetByteCount() == before);
}

void AuthoredRunRampsReachTheirSpeedWithoutASecondEase() {
    for(int fps:{30,60,120})for(bool accelerate:{false,true}) {
        Controller body;body.Reset(2500,0,5000);body.SetTargetVelocity(accelerate?32:120);Run(body,2);
        cat::gait_transition::Transition transition;
        const double from=std::abs(body.CurrentPose().velocityX),target=accelerate?120:32;
        CHECK(transition.Begin(accelerate,0,500,from,target));
        double maximum=0;unsigned reached=0;
        for(unsigned frame=1;frame<=static_cast<unsigned>(fps);++frame) {
            const auto ms=static_cast<std::uint64_t>(frame*1000/fps);
            body.SetTargetVelocity(ms<=500?transition.MotorSpeed(ms):target);body.Update(1./fps);
            const double speed=body.CurrentPose().velocityX;maximum=std::max(maximum,speed);
            CHECK(speed>=target*(accelerate?0:.85)&&speed<=120*1.04);
            if(!reached&&(accelerate?speed>=target*.9:speed<=target*1.1))reached=static_cast<unsigned>(ms);
        }
        CHECK(reached&&reached<=600);CHECK(maximum<=124.8);
        CHECK(std::abs(body.CurrentPose().velocityX-target)<1);
    }
    for(int direction:{-1,1})for(int fps:{30,60,120}) {
        Controller body;body.Reset(2500,0,5000);Run(body,.3);
        cat::running_turn::Turn turn;CHECK(turn.Begin(-direction,direction,{{80,120,500}}));
        unsigned launchMs=0,reached=0;double maximum=0;
        for(unsigned frame=1;frame<=static_cast<unsigned>(fps*2);++frame) {
            const unsigned ms=frame*1000/fps;
            if(turn.Advance(1./fps,true,body.CurrentPose().velocityX,body.RenderPose().velocityX))launchMs=ms;
            body.SetTargetVelocity(turn.Motor(direction*120));body.Update(1./fps);
            const double speed=direction*body.CurrentPose().velocityX;
            CHECK(speed>=0&&speed<=124.8);maximum=std::max(maximum,speed);
            if(launchMs&&!reached&&speed>=108)reached=ms-launchMs;
        }
        CHECK(launchMs&&reached&&reached<=550);CHECK(maximum<=124.8);
    }
}

void WallClimbAndSkid() {
    for(bool running:{false,true}) {
        double low=2,high=0,previous=cat::lively::Pace(0,running,3);
        for(unsigned ms=16;ms<12000;ms+=16) {
            const double gain=cat::lively::Pace(ms,running,3);
            CHECK(gain>=.6&&gain<=1.26);CHECK(std::abs(gain-previous)<.04);
            low=std::min(low,gain);high=std::max(high,gain);previous=gain;
        }
        CHECK(high-low>(running?.15:.3));
        if(running)CHECK(low>=.9&&high<=1.1);
    }
    const int before=b2GetByteCount();
    {
        Controller cat;cat.Reset(499,20,500);cat.SetCeiling(400);Run(cat,.3);
        CHECK(cat.BeginWallClimb(150));const auto origin=cat.CurrentPose();
        Run(cat,1.5); // Initial grip can put the body to sleep before its first pull.
        cat.SetWallClimbSpeed(60);Run(cat,1.5);
        const auto raised=cat.CurrentPose();
        CHECK(cat.WallClimbing()&&raised.hanging);
        CHECK(std::abs(raised.x-origin.x)<.2);
        CHECK(raised.lift>80&&raised.lift<100);
        cat.SetWallClimbSpeed(0);Run(cat,.5);
        CHECK(std::abs(cat.CurrentPose().lift-raised.lift)<1);
        cat.Update(.2,true);CHECK(std::abs(cat.CurrentPose().lift-raised.lift)<1);
        cat.EndWallClimb();CHECK(!cat.WallClimbing()&&!cat.Hanging());
        CHECK(std::abs(cat.CurrentPose().lift-raised.lift)<1);
        Run(cat,2);CHECK(cat.CurrentPose().grounded);CHECK(cat.CurrentPose().lift<.5);
        CHECK(cat.BeginWallClimb(120));cat.SetWallClimbSpeed(50);Run(cat,.5);
        cat.SetBounds(20,520);CHECK(!cat.WallClimbing());Run(cat,2);CHECK(cat.CurrentPose().grounded);
        cat.Reset(100,20,2000);Run(cat,.2);CHECK(!cat.BeginWallClimb(150));
        double distance[2]{};
        for(int variant=0;variant<2;++variant) {
            cat.Reset(200,20,2000);cat.SetTargetVelocity(120);Run(cat,2);
            const double start=cat.CurrentPose().x;
            cat.SetBrakeGrip(variant?.18:1.25);cat.SetTargetVelocity(0);Run(cat,2);
            distance[variant]=cat.CurrentPose().x-start;
            CHECK(std::abs(cat.CurrentPose().velocityX)<4);
        }
        CHECK(distance[1]>distance[0]*3);
    }
    CHECK(b2GetByteCount()==before);
}

void WorldLifetime() {
    const int before = b2GetByteCount();
    for (int i = 0; i < 10; ++i) {
        Controller cat;
        cat.Reset(100, 20, 500);
        cat.SetPlatform(100, 450, 100);
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
        RenderedStrideFollowsEveryRenderedSubstep();
        RenderedSupportTravelAndHalfStepPauseStayContinuous();
        PauseStallAndCatchUpLimit();
        ResetBoundsAndInvalidInput();
        ViewportRelocationPreservesJumpAndMotor();
        ResizeClipsHorizontallyWithoutDroppingCat();
        BallisticTransferAndOneWayLanding();
        FlightIgnoresMotorAndTargetChanges();
        ActualMovingSupportAndRemoval();
        MovedOrResizedAirborneTargetsNeverLeaveInvisibleSupport();
        ResizedOrRemovedOccupiedGeometryReleasesSupport();
        DescendingSupportDoesNotMagnetizeCat();
        PlatformFrameRatesAndCatchUp();
        CeilingAndReachability();
        TallViewportContainsRaisedPlatformLaunch();
        DirectWindowTransfersKeepSourceUntilTakeoff();
        HighWindowsUseRealRiseAndAvailableHeadroom();
        OneWayLandingDoesNotDependOnFixedClockPhase();
        DisablingCursorPlayLeavesOrdinaryWindowPlanningAvailable();
        CursorSitApproachesWithMotorAndWaitsForBraking();
        LedgeGripUsesJointAndReleasesMomentum();
        MovingOrClosingGripNeverTeleports();
        HangingFixedStepsAndWorldCleanup();
        AuthoredRunRampsReachTheirSpeedWithoutASecondEase();
        WallClimbAndSkid();
        WorldLifetime();
        std::cout << "Box2D physics: " << checks << " checks passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
