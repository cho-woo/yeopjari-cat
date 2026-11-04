#include "../src/cat_motion_gate.hpp"
#include "../src/cat_physics.hpp"
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {
int checks = 0;
#define CHECK(condition) do { ++checks; if (!(condition)) throw std::runtime_error( \
    std::string(__FILE__) + ":" + std::to_string(__LINE__) + " failed: " #condition); } while (false)
using cat::animation::Motion;

void StopBeforeActionWithActualMotor() {
    for (double fps : {30.0, 60.0, 120.0})
        for (double speed : {-200.0, -24.0, 24.0, 200.0})
            for (Motion action : {Motion::Sleep, Motion::Loaf, Motion::BellyRoll,
                    Motion::Knead, Motion::Punch, Motion::Jump, Motion::Pounce}) {
                cat::physics::Controller physics;
                physics.Reset(800, 96, 1824);
                physics.SetTargetVelocity(speed);
                const double dt = 1.0 / fps;
                cat::animation::Locomotion gait;
                for (int i = 0; i < static_cast<int>(fps); ++i) {
                    const double oldX = physics.CurrentPose().x;
                    physics.Update(dt);
                    gait.Advance(Motion::Walk, physics.CurrentPose().x - oldX, dt);
                }
                bool waiting = cat::motion::NeedsStop(action, physics.CurrentPose().grounded,
                    physics.CurrentPose().velocityX);
                CHECK(waiting);
                std::uint64_t started = 1000, decision = started + 4300;
                bool began = false;
                for (int i = 1; i <= static_cast<int>(fps * 2); ++i) {
                    const auto ticks = 1000 + static_cast<std::uint64_t>(std::llround(i * 1000.0 / fps));
                    const bool released = cat::motion::AdvanceStopGate(waiting,
                        physics.CurrentPose().grounded, physics.CurrentPose().velocityX,
                        ticks, started, decision);
                    CHECK(started == ticks); // No late contact/takeoff after braking.
                    CHECK(decision - started == 4300); // Full action duration remains.
                    if (released) {
                        CHECK(!waiting);
                        CHECK(std::abs(physics.CurrentPose().velocityX) <= 2);
                        CHECK(physics.CurrentPose().grounded);
                        began = true;
                        break;
                    }
                    CHECK(waiting);
                    CHECK(cat::animation::PunchHits(ticks - started) == 0);
                    CHECK(ticks - started < 220);
                    physics.SetTargetVelocity(0);
                    const double oldX = physics.CurrentPose().x;
                    physics.Update(dt);
                    CHECK(gait.Advance(Motion::Walk, physics.CurrentPose().x - oldX, dt));
                }
                CHECK(began);
                const auto start = started, deadline = decision;
                CHECK(!cat::motion::AdvanceStopGate(waiting, true, 0, started + 400, started, decision));
                CHECK(started == start && decision == deadline);
            }
}

void InterruptionsAndPauseKeepPhase() {
    for (Motion travel : {Motion::Walk, Motion::Stalk, Motion::Zoomies})
        CHECK(!cat::motion::NeedsStop(travel, true, 120));
    CHECK(!cat::motion::NeedsStop(Motion::Jump, false, 120));
    CHECK(!cat::motion::NeedsStop(Motion::Jump, true, 120, true));
    CHECK(!cat::motion::NeedsStop(Motion::Sleep, true, 1.9));
    CHECK(cat::motion::NeedsStop(Motion::Pounce, true, 120));
    cat::physics::Controller physics;
    physics.Reset(500, 96, 1824);
    physics.SetTargetVelocity(60);
    cat::animation::Locomotion gait;
    for (int i = 0; i < 17; ++i) {
        const double oldX = physics.CurrentPose().x;
        physics.Update(1.0 / 60);
        gait.Advance(Motion::Walk, physics.CurrentPose().x - oldX, 1.0 / 60);
    }
    const double phase = gait.Phase(), x = physics.CurrentPose().x;
    CHECK(phase != 0);
    for (int i = 0; i < 100; ++i) {
        physics.Update(.1, true);
        CHECK(gait.Phase() == phase);
        CHECK(physics.CurrentPose().x == x);
    }
    physics.Update(1.0 / 60);
    CHECK(gait.Advance(Motion::Walk, physics.CurrentPose().x - x, 1.0 / 60));
    CHECK(gait.Phase() != phase);
    bool waiting = true;
    std::uint64_t start = 1000, decision = 5000;
    CHECK(!cat::motion::AdvanceStopGate(waiting, true, 30, 1200, start, decision));
    // The pause path moves both clocks together, then the next active frame
    // still waits for planted feet without consuming the action's duration.
    start += 10000; decision += 10000;
    CHECK(!cat::motion::AdvanceStopGate(waiting, true, 20, 11216, start, decision));
    CHECK(start == 11216 && decision == 15216);
    CHECK(cat::motion::AdvanceStopGate(waiting, true, 1.5, 11232, start, decision));
    CHECK(start == 11232 && decision == 15232);
}

void ReachRealLedge(cat::physics::Controller& physics, double dt) {
    physics.Reset(800,96,1824);
    physics.SetPlatform(250,1450,180);
    CHECK(physics.CanLaunchTo(800,180));
    CHECK(physics.LaunchTo(800,180));
    bool landed=false;
    for (int i=0;i<static_cast<int>(4/dt);++i) {
        physics.Update(dt);
        if (physics.OnPlatform()) { landed=true;break; }
    }
    CHECK(landed && physics.CurrentPose().grounded);
    CHECK(std::abs(physics.CurrentPose().lift-180)<2);
}

void RestingActionsBrakeOnActualElevatedContact() {
    for(double fps : {30.,60.,120.}) for(double speed : {-70.,70.})
    for(Motion action : {Motion::BellyRoll,Motion::Loaf,Motion::Knead}) {
        const double dt=1/fps;
        cat::physics::Controller physics;
        ReachRealLedge(physics,dt);
        physics.SetTargetVelocity(speed);
        for(int i=0;i<static_cast<int>(fps*.5);++i) physics.Update(dt);
        CHECK(physics.OnPlatform());
        bool waiting=cat::motion::NeedsStop(action,physics.CurrentPose().grounded,physics.CurrentPose().velocityX);
        CHECK(waiting);
        std::uint64_t start=1000,decision=5300;
        bool began=false;
        for(int i=1;i<=static_cast<int>(fps*2);++i) {
            const auto ticks=1000+static_cast<std::uint64_t>(std::llround(i*1000/fps));
            const auto before=physics.CurrentPose();
            CHECK(before.onPlatform && before.grounded);
            CHECK(std::abs(before.lift-180)<2);
            const bool released=cat::motion::AdvanceStopGate(waiting,before.grounded,before.velocityX,ticks,start,decision);
            CHECK(decision-start==4300);
            if(released) {
                began=true;
                CHECK(start==ticks && std::abs(before.velocityX)<=cat::motion::kStoppedSpeed);
                // Reclining/crouching may change local anatomy, never subtract
                // the platform height or apply an extra animation height arc.
                const auto pose=cat::animation::Evaluate(action,0,ticks,1);
                CHECK(pose.lift==0 && pose.groundContact);
                break;
            }
            physics.SetTargetVelocity(0);
            physics.Update(dt);
        }
        CHECK(began);
        const auto supported=physics.CurrentPose();
        for(int i=0;i<8;++i) physics.Update(.1,true);
        CHECK(physics.CurrentPose().x==supported.x && physics.CurrentPose().lift==supported.lift);
        CHECK(physics.CurrentPose().onPlatform && physics.CurrentPose().grounded);
    }
}

void LostLedgeCannotFreezeGravityBehindTheStopGate() {
    for(double fps : {30.,60.,120.}) {
        const double dt=1/fps;
        cat::physics::Controller physics;
        ReachRealLedge(physics,dt);
        physics.SetTargetVelocity(70);
        for(int i=0;i<static_cast<int>(fps*.5);++i) physics.Update(dt);
        bool waiting=cat::motion::NeedsStop(Motion::BellyRoll,physics.CurrentPose().grounded,physics.CurrentPose().velocityX);
        CHECK(waiting);
        physics.RemovePlatform();
        const auto removed=physics.CurrentPose();
        CHECK(!removed.grounded && !removed.onPlatform);
        CHECK(removed.lift>178); // Removing the support must not teleport down.
        std::uint64_t start=1000,decision=5300;
        bool fell=false,began=false;
        for(int i=1;i<=static_cast<int>(fps*3);++i) {
            const auto ticks=1000+static_cast<std::uint64_t>(std::llround(i*1000/fps));
            const auto before=physics.CurrentPose();
            fell=fell || before.lift<150;
            const bool released=cat::motion::AdvanceStopGate(waiting,before.grounded,before.velocityX,ticks,start,decision);
            CHECK(decision-start==4300);
            if(released) {
                CHECK(before.grounded && !before.onPlatform && before.lift<2);
                CHECK(start==ticks);
                began=true;
                break;
            }
            CHECK(waiting);
            physics.SetTargetVelocity(0);
            physics.Update(dt); // Gate only delays the action; gravity still runs.
        }
        CHECK(fell && began);
    }
}

void CarriedLedgeUsesFeetRelativeToTheSupport() {
    for (double fps : {30., 60., 120.}) {
        const double dt = 1 / fps;
        cat::physics::Controller physics;
        ReachRealLedge(physics, dt);
        physics.SetTargetVelocity(0);
        double shift = 0;
        for (int i = 0; i < static_cast<int>(fps * 2); ++i) {
            shift += 18 * dt;
            physics.SetPlatform(250 + shift, 1450 + shift, 180);
            physics.Update(dt);
        }
        const auto carried = physics.CurrentPose();
        CHECK(carried.grounded && carried.onPlatform);
        CHECK(carried.velocityX > 15);
        const double relativeVelocity = carried.velocityX - carried.supportVelocityX;
        CHECK(std::abs(relativeVelocity) <= cat::motion::kStoppedSpeed);
        CHECK(!cat::motion::NeedsStop(Motion::Loaf, carried.grounded, relativeVelocity));
        // Using world speed here would delay a resting action indefinitely
        // even though the paws have settled on a steadily moving window.
        CHECK(cat::motion::NeedsStop(Motion::Loaf, carried.grounded, carried.velocityX));
        bool waiting = true;
        std::uint64_t start = 1000, decision = 5300;
        CHECK(cat::motion::AdvanceStopGate(waiting, carried.grounded, relativeVelocity,
            1100, start, decision));
        CHECK(!waiting && start == 1100 && decision == 5400);

        double relativeTravel = 0, worldTravel = 0;
        cat::animation::Locomotion gait;
        for (int i = 0; i < static_cast<int>(fps * 2); ++i) {
            const auto before = physics.CurrentPose();
            shift += 18 * dt;
            physics.SetPlatform(250 + shift, 1450 + shift, 180);
            physics.Update(dt);
            const auto after = physics.CurrentPose();
            CHECK(after.grounded && after.onPlatform);
            const double walked = after.x - before.x - after.supportTravelX;
            relativeTravel += std::abs(walked);
            worldTravel += after.x - before.x;
            gait.Advance(Motion::Walk, walked, dt);
        }
        CHECK(worldTravel > 35 && worldTravel < 37);
        CHECK(relativeTravel < .1);
        // The support moves 36 pixels, but no complete walking stride should
        // be synthesized from externally carried distance.
        CHECK(gait.Phase() < .05);
    }
}

void EdgeBrakeOnlyStopsOutwardTravel() {
    CHECK(cat::motion::BrakeBeforeEdge(330, -60, -60, 320, 1380));
    CHECK(cat::motion::BrakeBeforeEdge(1370, 60, 60, 320, 1380));
    CHECK(cat::motion::BrakeBeforeEdge(320, 0, -60, 320, 1380));
    CHECK(cat::motion::BrakeBeforeEdge(1380, 0, 60, 320, 1380));
    CHECK(!cat::motion::BrakeBeforeEdge(800, 320, 320, 320, 1380));
    CHECK(!cat::motion::BrakeBeforeEdge(800, -320, -320, 320, 1380));
    CHECK(!cat::motion::BrakeBeforeEdge(330, -60, 60, 320, 1380));
    CHECK(!cat::motion::BrakeBeforeEdge(1370, 60, -60, 320, 1380));
    CHECK(!cat::motion::BrakeBeforeEdge(330, 60, -60, 320, 1380));
    CHECK(!cat::motion::BrakeBeforeEdge(1370, -60, 60, 320, 1380));
    CHECK(!cat::motion::BrakeBeforeEdge(320, -320, 0, 320, 1380));
    CHECK(!cat::motion::BrakeBeforeEdge(1380, 320, 0, 320, 1380));
}

void RealMotorStopsBeforeFiniteLedgeEdges() {
    constexpr double low = 320, high = 1380;
    // ReachRealLedge creates an actual platform at 250..1450. The logical
    // walking edges are 70px inside it; only the desktop has physical walls.
    for (double fps : {30., 60., 120.})
    for (double speed : {60., 200., 320.})
    for (int direction : {-1, 1}) {
        const double dt = 1 / fps;
        cat::physics::Controller physics;
        ReachRealLedge(physics, dt);
        bool braking = false, stopped = false;
        double brakeX = 0, brakeSpeed = 0;
        for (int i = 0; i < static_cast<int>(fps * 16); ++i) {
            const auto before = physics.CurrentPose();
            const double relativeVelocity = before.velocityX - before.supportVelocityX;
            if (!braking && cat::motion::BrakeBeforeEdge(before.x, relativeVelocity,
                    direction * speed, low, high)) {
                braking = true;
                brakeX = before.x;
                brakeSpeed = relativeVelocity;
                CHECK(direction * brakeSpeed > speed * .9);
                CHECK(direction < 0 ? brakeX > low : brakeX < high);
            }
            physics.SetTargetVelocity(braking ? 0 : direction * speed);
            physics.Update(dt);
            const auto after = physics.CurrentPose();
            CHECK(after.onPlatform && after.grounded);
            CHECK(after.x >= low && after.x <= high);
            CHECK(std::abs(after.lift - 180) < 2);
            if (braking && std::abs(after.velocityX) <= cat::motion::kStoppedSpeed) {
                CHECK(direction * (after.x - brakeX) > .5);
                CHECK(std::abs(after.velocityX) < std::abs(brakeSpeed));
                stopped = true;
                break;
            }
        }
        CHECK(braking && stopped);
        for (int i = 0; i < static_cast<int>(fps); ++i) physics.Update(dt);
        const auto resting = physics.CurrentPose();
        CHECK(resting.onPlatform && resting.grounded);
        CHECK(resting.x >= low && resting.x <= high);
        CHECK(std::abs(resting.velocityX) < .01);
        // Disabling the helper lets the same unchanged physical world walk
        // across the safe edge and off the finite platform. There is no hidden
        // wall, coordinate snap or Reset used to manufacture the stopped pose.
        physics.SetTargetVelocity(direction * speed);
        bool leftPlatform = false;
        for (int i = 0; i < static_cast<int>(fps * 4); ++i) {
            physics.Update(dt);
            if (!physics.CurrentPose().onPlatform) { leftPlatform = true; break; }
        }
        CHECK(leftPlatform);
    }
}


}

int main() {
    try {
        StopBeforeActionWithActualMotor();
        InterruptionsAndPauseKeepPhase();
        RestingActionsBrakeOnActualElevatedContact();
        LostLedgeCannotFreezeGravityBehindTheStopGate();
        CarriedLedgeUsesFeetRelativeToTheSupport();
        EdgeBrakeOnlyStopsOutwardTravel();
        RealMotorStopsBeforeFiniteLedgeEdges();
        std::cout << "Motion gate physics: " << checks << " checks passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
