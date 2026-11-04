#include "../src/cat_environment.hpp"
#include "../src/cat_motion_gate.hpp"
#include "../src/cat_physics.hpp"
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
int checks = 0;
#define CHECK(c) do { ++checks; if (!(c)) throw std::runtime_error(std::to_string(__LINE__) + ": " #c); } while (false)
using Motion = cat::animation::Motion;
using Action = cat::environment::CursorAction;

Motion MotionFor(Action action) {
    switch (action) {
    case Action::Hunt: case Action::ApproachSit: return Motion::Stalk;
    case Action::Sniff: return Motion::Sniff;
    case Action::Paw: return Motion::ToyPaw;
    case Action::Sit: return Motion::Loaf;
    case Action::Abandon: return Motion::EarFlick;
    default: return Motion::Watch;
    }
}

// Uses the production CursorSession, real controller and production stop gate
// in the same order as Tick. Only OS cursor sampling and the wall clock are
// supplied here, so an action-clock/ownership regression is observable.
struct Host {
    cat::physics::Controller physics;
    cat::environment::CursorSession session;
    Motion motion = Motion::Idle, ownedMotion = Motion::Watch, walking = Motion::Walk;
    bool waiting = false, enabled = true, available = true, busy = false;
    std::uint64_t now = 1000, started = 1000, decision = 4000, previous = 1000;
    double cursorX = 560, cursorHeight = 0, interest = .01, roll = .999;
    std::vector<Action> changes;

    Host() { physics.Reset(400, 96, 960); }
    void SetMotion(Motion next, std::uint64_t duration) {
        if (cat::animation::Travels(motion)) walking = motion;
        motion = next; started = now; decision = now + duration;
        waiting = cat::motion::NeedsStop(next, physics.CurrentPose().grounded, physics.CurrentPose().velocityX);
    }
    void Frame(std::uint64_t time, double seconds) {
        now = time;
        cat::environment::CursorInput input;
        input.nowMs = now; input.enabled = enabled; input.available = available;
        input.canContinue = !busy && physics.CurrentPose().grounded;
        input.canStart = input.canContinue && !waiting && (motion == Motion::Idle || motion == Motion::Watch ||
            motion == Motion::Walk || motion == Motion::Stalk || motion == Motion::Loaf);
        input.cursorX = cursorX; input.cursorHeight = cursorHeight; input.catX = physics.CurrentPose().x;
        input.minX = 96; input.maxX = 960; input.interest = interest; input.roll = roll;
        const auto output = session.Step(input, now - previous, {motion == ownedMotion, waiting, started});
        if (output.changed) {
            changes.push_back(output.action); ownedMotion = MotionFor(output.action);
            SetMotion(ownedMotion, output.action == Action::None ? 1800 : 8000);
            session.AdoptMotion(started, waiting);
        }
        cat::motion::AdvanceStopGate(waiting, physics.CurrentPose().grounded,
            physics.CurrentPose().velocityX, now, started, decision);
        const double speed = session.Active() ? session.Velocity() : 0;
        physics.SetTargetVelocity(waiting ? 0 : speed);
        physics.Update(seconds);
        const Motion displayed = waiting ? walking : motion;
        if (displayed == Motion::Sniff || displayed == Motion::Loaf || displayed == Motion::ToyPaw)
            CHECK(std::abs(physics.CurrentPose().velocityX) <= 2.01);
        CHECK(physics.CurrentPose().grounded);
        previous = now;
    }
    void Pause(std::uint64_t milliseconds) {
        now += milliseconds; started += milliseconds; decision += milliseconds;
        session.Pause(milliseconds, {motion == ownedMotion, waiting, started});
        physics.Update(0, true); previous = now;
    }
};

void WalkingPetAcknowledgesLowCursorAndCompletesContact() {
    for (int fps : {30, 60, 120, 144}) {
        Host host;
        host.physics.SetTargetVelocity(60);
        for (int i = 0; i < 60; ++i) host.physics.Update(1./60);
        host.SetMotion(Motion::Walk, 8000);
        bool braked = false, sniffed = false, pawed = false;
        for (int frame = 1; frame < fps*10; ++frame) {
            host.Frame(1000 + static_cast<std::uint64_t>(std::round(frame*1000./fps)), 1./fps);
            if (frame == 1) CHECK(host.session.Action() == Action::Watch && host.waiting);
            braked = braked || host.waiting;
            sniffed = sniffed || host.session.Action() == Action::Sniff;
            pawed = pawed || host.session.Action() == Action::Paw;
        }
        CHECK(braked && sniffed && pawed);
        const std::vector<Action> expected{Action::Watch, Action::Hunt, Action::Sniff, Action::Paw, Action::None};
        CHECK(host.changes == expected);
        CHECK(std::abs(host.physics.CurrentPose().x - host.cursorX) < 48);
    }
}

void OffCancelsDuringBrakingAndOnRespondsWithoutOldCooldown() {
    Host host;
    host.physics.SetTargetVelocity(60);
    for (int i = 0; i < 60; ++i) host.physics.Update(1./60);
    host.SetMotion(Motion::Walk, 8000);
    host.Frame(1016, 1./60);
    CHECK(host.waiting && host.session.Active());
    const double speed = host.physics.CurrentPose().velocityX;
    host.enabled = false; host.Frame(1032, 1./60);
    CHECK(!host.session.Active());
    CHECK(host.physics.CurrentPose().velocityX < speed);
    for (int frame = 3; frame < 90; ++frame) host.Frame(1000 + frame*16, 1./60);
    host.enabled = true; host.Frame(2440, 1./60);
    CHECK(host.session.Action() == Action::Watch);
}

void InterruptingActionWinsAndPauseDoesNotSkipToPaw() {
    Host host;
    host.cursorX = 430; host.cursorHeight = 60;
    host.Frame(1016, 1./60);
    for (int i = 2; i < 30; ++i) host.Frame(1000+i*16, 1./60);
    CHECK(host.session.Action() == Action::Watch);
    const auto physical = host.physics.CurrentPose();
    host.Pause(5000);
    host.Frame(host.now+16, 1./60);
    CHECK(host.session.Action() == Action::Watch);
    CHECK(std::abs(host.physics.CurrentPose().x - physical.x) < .01);
    host.now += 1; host.SetMotion(Motion::Eat, 4200);
    host.Frame(host.now+16, 1./60);
    CHECK(!host.session.Active());
    CHECK(host.motion == Motion::Eat);
}

void RealMotorReachesRareCursorSeatThenStops() {
    for (int fps : {30, 60, 120, 144}) {
        Host host;
        host.cursorX = 430; host.cursorHeight = 30; host.interest = 1; host.roll = 0;
        bool sat = false;
        for (int frame = 1; frame < fps*9; ++frame) {
            host.Frame(1000 + static_cast<std::uint64_t>(std::round(frame*1000./fps)), 1./fps);
            if (host.session.Action() == Action::Sit && !host.waiting) {
                sat = true;
                CHECK(std::abs(host.physics.CurrentPose().x-host.cursorX) < 4);
            }
        }
        CHECK(sat && !host.session.Active());
    }
}
}

int main() {
    try {
        WalkingPetAcknowledgesLowCursorAndCompletesContact(); OffCancelsDuringBrakingAndOnRespondsWithoutOldCooldown();
        InterruptingActionWinsAndPauseDoesNotSkipToPaw(); RealMotorReachesRareCursorSeatThenStops();
        std::cout << "Cursor host and Box2D: " << checks << " checks passed\n";
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
