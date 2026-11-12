#include "../src/cat_environment.hpp"
#include "../src/cat_motion_gate.hpp"
#include "../src/cat_physics.hpp"
#include "../src/cat_cursor_attack.hpp"
#include "../src/cat_cursor_reach_hop.hpp"
#include "../src/cat_cursor_animation.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
int checks = 0;
#define CHECK(c) do { ++checks; if (!(c)) throw std::runtime_error(std::to_string(__LINE__) + ": " #c); } while (false)
using Action = cat::environment::CursorAction;
using Motion = cat::animation::Motion;

bool Reach(Action action) {
    return action == Action::Paw || action == Action::Tiptoe ||
        action == Action::BothPaws || action == Action::Pounce;
}
Motion MotionFor(Action action) {
    if (action == Action::Hunt || action == Action::Sneak) return Motion::Stalk;
    if (action == Action::Pounce) return Motion::Pounce;
    if (action == Action::Paw || action == Action::Tiptoe || action == Action::BothPaws) return Motion::ToyPaw;
    return Motion::Watch;
}
struct Change { Action action; std::uint64_t at; };

// Uses the production session and Box2D launch helper. The host supplies only
// desktop samples, a deterministic clock and the same stop/ownership ordering
// as the native host; repeated play must not rely on cursor motion.
struct Host {
    cat::physics::Controller physics;
    cat::environment::CursorSession session;
    cat::environment::CursorInput input;
    Motion motion = Motion::Idle, ownedMotion = Motion::Watch;
    bool waiting = false, busy = false, launchAttempted = false;
    std::uint64_t now = 1000, previous = 1000, started = 1000, decision = 9000, serial = 0;
    std::uint64_t lastLanding = 0;
    double cursorX = 419, cursorWorldHeight = 4.4;
    unsigned launches = 0, contacts = 0;
    bool continuousTakeoff = true;
    cat::cursor::PawContact pawContacts;
    std::vector<Change> changes;

    Host() {
        physics.Reset(400, 96, 960); physics.SetCeiling(800);
        for (int frame = 0; frame < 12; ++frame) physics.Update(1./60);
        input.repeatNearby = true; input.canApproach = false;
        input.enabled = input.available = true;
        input.minX = 96; input.maxX = 960; input.sizeScale = 1;
        input.pawStandOff = 24; input.interest = .5; input.roll = .5;
    }
    void SetMotion(Motion next) {
        motion = next; started = now; decision = now+8000; ++serial;
        launchAttempted = false; pawContacts.Reset();
        waiting = cat::motion::NeedsStop(next, physics.CurrentPose().grounded,
            physics.CurrentPose().velocityX);
    }
    void Frame(std::uint64_t time, double seconds) {
        now = time;
        const auto before = physics.CurrentPose();
        input.nowMs = now; input.grounded = before.grounded;
        input.canContinue = !busy && (before.grounded || session.InFlight());
        input.canStart = input.canContinue && !waiting && (motion == Motion::Idle || motion == Motion::Watch);
        input.catX = before.x; input.supportHeight = before.lift;
        input.cursorX = cursorX; input.cursorHeight = cursorWorldHeight-before.lift;
        const auto output = session.Step(input, now-previous, {motion == ownedMotion, waiting, started, serial});
        if (output.changed) {
            changes.push_back({output.action, now}); ownedMotion = MotionFor(output.action);
            SetMotion(ownedMotion); session.AdoptMotion(started, waiting, serial);
        }
        cat::motion::AdvanceStopGate(waiting, before.grounded, before.velocityX,
            now, started, decision);
        physics.SetTargetVelocity(waiting ? 0 : session.Velocity()*input.movementScale);
        if (!waiting && !launchAttempted && session.Action() == Action::Pounce &&
                now-started >= input.pouncePreparationMs) {
            launchAttempted = true;
            const auto takeoff = physics.CurrentPose();
            const bool launched = cat::cursor::LaunchReachHop(physics, session.TargetX(), session.TargetHeight(),
                input.minX, input.maxX, input.sizeScale, 1, input.canApproach);
            session.ConfirmLaunch(launched);
            if (launched) {
                ++launches;
                const auto after = physics.CurrentPose();
                continuousTakeoff = continuousTakeoff && takeoff.x == after.x && takeoff.lift == after.lift;
            }
        }
        physics.Update(seconds);
        const auto physical = physics.CurrentPose();
        if (physical.landed) lastLanding = now;
        const bool allowed = session.Active() && session.ContactAllowed() && session.OwnsSerial(serial) &&
            input.enabled && input.available && !busy && !waiting;
        if (allowed && session.Action() == Action::Paw && physical.grounded && std::abs(physical.velocityX) <= 2) {
            const auto pose = cat::cursor::AttackPose(cat::cursor::AttackKind::Gentle, now-started,
                output.direction, (cursorX-physical.x)/input.sizeScale,
                (cursorWorldHeight-physical.lift)/input.sizeScale);
            const auto paw = cat::cursor::PawPosition(pose);
            const auto index = static_cast<unsigned>(cat::cursor::ContactIndex(cat::cursor::AttackKind::Gentle,now-started));
            const double pawX = physical.x+paw.x*input.sizeScale;
            const double pawHeight = paw.y*input.sizeScale;
            const bool contact = pawContacts.Observe(index, true, pawX, pawHeight,
                cursorX, cursorWorldHeight-physical.lift, 7*input.sizeScale);
            if (contact) {
                ++contacts;
                CHECK(!pawContacts.Observe(index, true, pawX, pawHeight,
                    cursorX, cursorWorldHeight-physical.lift, 7*input.sizeScale));
            }
        }
        CHECK(std::isfinite(physical.x) && std::isfinite(physical.lift));
        previous = now;
    }
    void Run(double seconds, int fps = 60) {
        const auto origin = now;
        const auto frames = static_cast<int>(std::round(seconds*fps));
        for (int frame = 1; frame <= frames; ++frame)
            Frame(origin+static_cast<std::uint64_t>(std::round(frame*1000./fps)), 1./fps);
    }
    bool Until(Action wanted, double seconds = 4, int fps = 60) {
        const auto origin = now;
        for (int frame = 1; frame <= static_cast<int>(seconds*fps); ++frame) {
            Frame(origin+static_cast<std::uint64_t>(std::round(frame*1000./fps)), 1./fps);
            if (session.Action() == wanted) return true;
        }
        return false;
    }
    void Pause(std::uint64_t elapsed) {
        now += elapsed; previous = now; started += elapsed; decision += elapsed;
        session.Pause(elapsed, {motion == ownedMotion, waiting, started, serial});
        physics.Update(0, true);
    }
};

void StationaryHeightSelectsReachAtEveryScaleAndRefreshRate() {
    const struct { double height; Action action; } cases[] = {
        {40, Action::Paw}, {55, Action::Paw}, {56, Action::Tiptoe},
        {115, Action::Tiptoe}, {116, Action::BothPaws}, {155, Action::BothPaws},
        {156, Action::Pounce}, {205, Action::Pounce}
    };
    for (int fps : {30, 60, 120, 144}) for (double scale : {.55, 1., 1.2}) for (const auto& test : cases) {
        Host host; host.input.sizeScale = scale;
        host.cursorX = 400+38.15*scale; host.cursorWorldHeight = test.height*scale;
        CHECK(host.Until(test.action, 2, fps));
        CHECK(host.changes.size() >= 2 && host.changes.front().action == Action::Watch);
        const auto watched = host.changes[1].at-host.changes.front().at;
        CHECK(watched >= 350 && watched <= 600+static_cast<unsigned>(std::ceil(1000./fps)));
        CHECK(host.changes[1].action == test.action);
        CHECK(std::abs(host.physics.CurrentPose().x-400) < .01);
    }
}

void StationaryLowPointerRepeatsWithQuietRestsAndRealContactLatches() {
    for (int fps : {30, 60, 120, 144}) {
        Host host; host.cursorWorldHeight = 40;
        host.Run(40, fps);
        unsigned reaches = 0, rests = 0, paws = 0;
        bool varied = false;
        for (std::size_t index = 0; index < host.changes.size(); ++index) {
            const auto& change = host.changes[index];
            reaches += Reach(change.action); paws += change.action == Action::Paw;
            varied = varied || change.action == Action::Tiptoe || change.action == Action::BothPaws;
            if (change.action != Action::Rest) continue;
            ++rests;
            CHECK(index && Reach(host.changes[index-1].action));
            if (index+1 < host.changes.size()) {
                const auto rest = host.changes[index+1].at-change.at;
                CHECK(rest >= 5000 && rest <= 9000+static_cast<unsigned>(std::ceil(1000./fps)));
            }
        }
        CHECK(reaches >= 4 && rests >= 3 && paws >= 2 && varied);
        Host touching; touching.Run(28, fps);
        CHECK(touching.contacts >= 4);
        CHECK(host.launches == 0);
        CHECK(std::abs(host.physics.CurrentPose().x-400) < .01);
        CHECK(host.session.Active());
    }
}

void HighStationaryPointerHopsVerticallyAndWaitsForActualLandingRecovery() {
    for (int fps : {30, 60, 120, 144}) {
        Host host; host.cursorX = 430; host.cursorWorldHeight = 205;
        double peak = 0; unsigned landings = 0, previousLaunches = 0;
        std::uint64_t lastLanding = 0;
        for (int frame = 1; frame <= fps*22; ++frame) {
            host.Frame(1000+static_cast<std::uint64_t>(std::round(frame*1000./fps)), 1./fps);
            const auto physical = host.physics.CurrentPose();
            peak = std::max(peak, physical.lift);
            CHECK(std::abs(physical.x-400) < .02);
            if (physical.landed) { ++landings; lastLanding = host.now; }
            if (host.launches != previousLaunches) {
                if (previousLaunches) CHECK(lastLanding && host.now-lastLanding >= host.input.landingRecoveryMs+5000);
                previousLaunches = host.launches;
            }
            if (host.session.InFlight()) CHECK(std::abs(host.session.TargetX()-400) < .01);
        }
        CHECK(host.launches >= 3 && landings >= 2 && host.continuousTakeoff);
        CHECK(peak > 90 && peak < 245);
        CHECK(host.contacts == 0); // A high pointer is never credited for a floor landing.
    }
}

void WalkingPermissionUsesTheSameMotorWithoutChangingNearbyPlay() {
    for (int fps : {30, 60, 120, 144}) {
        Host walking; walking.input.canApproach = true; walking.cursorX = 525;
        bool approached = false, reached = false;
        for (int frame = 1; frame <= fps*8; ++frame) {
            walking.Frame(1000+static_cast<std::uint64_t>(std::round(frame*1000./fps)), 1./fps);
            approached = approached || walking.session.Action() == Action::Hunt;
            if (walking.session.Action() == Action::Paw && !walking.waiting) {
                reached = true;
                CHECK(std::abs(walking.physics.CurrentPose().velocityX) <= 2.01);
                break;
            }
        }
        CHECK(approached && reached);
        CHECK(walking.physics.CurrentPose().x > 440);
        Host planted; planted.cursorX = 525; planted.Run(8, fps);
        CHECK(planted.session.Active() && planted.launches == 0);
        CHECK(std::abs(planted.physics.CurrentPose().x-400) < .01);
        CHECK(std::none_of(planted.changes.begin(), planted.changes.end(),
            [](const Change& change) { return change.action == Action::Hunt; }));
    }
}

void DepartingPointerCancelsGroundPlayAndCannotRetargetAnExistingHop() {
    for (int reason = 0; reason < 5; ++reason) {
        Host host; host.cursorWorldHeight = 205;
        bool interrupted = false, landed = false;
        unsigned contacts = 0; double frozen = 0, velocity = 0;
        for (int frame = 1; frame <= 120*8; ++frame) {
            if (host.session.InFlight() && !interrupted) {
                interrupted = true; contacts = host.contacts;
                frozen = host.session.TargetX(); velocity = host.physics.CurrentPose().velocityX;
                if (reason == 0) host.input.enabled = false;
                if (reason == 1) host.input.available = false;
                if (reason == 2) host.cursorX += 500;
                if (reason == 3) host.cursorWorldHeight += 500;
                if (reason == 4) { host.SetMotion(Motion::Eat); host.busy = true; }
            }
            host.Frame(1000+static_cast<std::uint64_t>(std::round(frame*1000./120)), 1./120);
            if (!interrupted) continue;
            CHECK(!host.session.ContactAllowed());
            CHECK(host.contacts == contacts);
            if (!host.physics.CurrentPose().grounded) {
                CHECK(std::abs(host.physics.CurrentPose().velocityX-velocity) < .001);
                if (host.session.InFlight()) CHECK(host.session.TargetX() == frozen);
            } else { landed = true; break; }
        }
        CHECK(interrupted && landed && host.launches == 1);
        if (reason == 4) CHECK(host.motion == Motion::Eat && !host.session.Active());
    }
    Host host; CHECK(host.Until(Action::Paw));
    host.input.available = false; host.Run(.1);
    CHECK(!host.session.Active() && !host.session.ContactAllowed());
    const auto contacts = host.contacts;
    host.Run(2); CHECK(host.contacts == contacts);
}

void PauseAndNewActionKeepTheirOwnClocksAndContactOwnership() {
    Host host; CHECK(host.Until(Action::Paw));
    const auto elapsed = host.now-host.started;
    const auto physical = host.physics.CurrentPose();
    host.Pause(5000); host.Run(.016, 60);
    CHECK(host.session.Action() == Action::Paw);
    CHECK(host.now-host.started <= elapsed+20);
    CHECK(std::abs(host.physics.CurrentPose().x-physical.x) < .01);
    for (Motion replacement : {Motion::Eat, Motion::ToyPaw}) {
        Host interrupted; CHECK(interrupted.Until(Action::Paw));
        interrupted.SetMotion(replacement);
        const auto serial = interrupted.serial, start = interrupted.started;
        interrupted.Run(.1);
        CHECK(!interrupted.session.Active());
        CHECK(interrupted.motion == replacement && interrupted.serial == serial && interrupted.started == start);
        CHECK(!interrupted.session.ContactAllowed());
    }
}

void OutsideHeightOrBoundsNeverStartsAReach() {
    for (int scenario = 0; scenario < 4; ++scenario) {
        Host host;
        if (scenario == 0) host.cursorWorldHeight = 300;
        if (scenario == 1) host.cursorX = 1100;
        if (scenario == 2) host.input.enabled = false;
        if (scenario == 3) host.busy = true;
        host.Run(4);
        CHECK(host.launches == 0 && host.contacts == 0);
        CHECK(std::none_of(host.changes.begin(), host.changes.end(), [](const Change& c) { return Reach(c.action); }));
    }
}

void ActivePursuitKeepsTheWholeMonitorAndWaitsForPlantedTurns() {
    cat::environment::CursorReaction planner;
    cat::environment::CursorInput input;
    input.enabled=input.available=input.canStart=input.canContinue=input.pursuePointer=true;
    input.minX=60;input.maxX=1220;input.catX=200;input.cursorX=1100;input.cursorHeight=110;
    cat::environment::CursorOutput output;
    for(input.nowMs=1000;input.nowMs<2500;input.nowMs+=16)output=planner.Step(input);
    CHECK(output.action==Action::Chase&&output.velocity>100);
    CHECK(planner.Active()); // A900px gap no longer cancels explicit play.
    input.catX=900;input.cursorX=1005;input.catVelocityX=140;input.canAttack=false;
    for(unsigned frame=0;frame<100;++frame){input.nowMs+=16;output=planner.Step(input);}
    CHECK(output.action==Action::Chase); // Finish the source pivot before an attack.
    input.canAttack=true;input.nowMs+=16;output=planner.Step(input);
    CHECK(output.action==Action::Pounce);
    const auto target=planner.TargetX();
    planner.ConfirmLaunch(true);input.grounded=false;input.cursorX=100;input.nowMs+=16;
    output=planner.Step(input);
    CHECK(planner.InFlight()&&planner.TargetX()==target&&output.velocity==0);
    CHECK(!planner.ContactAllowed()); // No fictitious catch after a large escape.
    input.enabled=false;input.nowMs+=16;planner.Step(input);
    CHECK(planner.InFlight()); // An OFF toggle cannot undo a Box2D impulse.
    input.grounded=true;input.nowMs+=16;planner.Step(input);
    CHECK(!planner.Active());
}
} // namespace

int main() {
    try {
        StationaryHeightSelectsReachAtEveryScaleAndRefreshRate();
        StationaryLowPointerRepeatsWithQuietRestsAndRealContactLatches();
        HighStationaryPointerHopsVerticallyAndWaitsForActualLandingRecovery();
        WalkingPermissionUsesTheSameMotorWithoutChangingNearbyPlay();
        DepartingPointerCancelsGroundPlayAndCannotRetargetAnExistingHop();
        PauseAndNewActionKeepTheirOwnClocksAndContactOwnership();
        OutsideHeightOrBoundsNeverStartsAReach();
        ActivePursuitKeepsTheWholeMonitorAndWaitsForPlantedTurns();
        std::cout << "cat_cursor_nearby_test: " << checks << " checks passed\n";
        return 0;
    } catch (const std::exception& failure) {
        std::cerr << "cat_cursor_nearby_test: " << failure.what() << '\n';
        return 1;
    }
}
