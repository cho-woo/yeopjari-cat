#include "../src/cat_environment.hpp"
#include "../src/cat_motion_gate.hpp"
#include "../src/cat_physics.hpp"
#include "../src/cat_cursor_attack.hpp"
#include "../src/cat_cursor_animation.hpp"
#include "../src/care_sprites.generated.hpp"
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
    case Action::Hunt: case Action::Sneak: case Action::ApproachSit: return Motion::Stalk;
    case Action::Chase: return Motion::Zoomies;
    case Action::Pounce: return Motion::Pounce;
    case Action::Sniff: return Motion::Sniff;
    case Action::Paw: case Action::Punch: return Motion::ToyPaw;
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
    std::uint64_t serial = 0;
    double cursorX = 560, cursorHeight = 0, interest = .01, roll = .999;
    double growth = 1, jump = 1, movementScale = 1, minX = 96, maxX = 960;
    bool launchAttempted = false;
    int launches = 0, contacts = 0;
    cat::cursor::PawContact pawContacts;
    std::vector<Action> changes;
    const cat::sprite_assets::Descriptor* authoredPunch = nullptr;
    std::uint64_t takeoffMs = 240;

    Host() { physics.Reset(400, 96, 960); physics.SetCeiling(800); }
    void UseAuthoredCream() {
        using Clip=cat::sprite_sequence::Clip;
        authoredPunch=cat::sprite_assets::Find(Clip::Punch,0,cat::sprite_assets::kCareSprites);
        const auto* jump=cat::sprite_assets::Find(Clip::Jump,0,cat::sprite_assets::kCareSprites);
        CHECK(authoredPunch && authoredPunch->program.stepCount && jump && jump->program.stepCount);
        takeoffMs=cat::sprite_sequence::Duration(jump->program,jump->program.intro);
    }
    void SetMotion(Motion next, std::uint64_t duration) {
        if (cat::animation::Travels(motion)) walking = motion;
        motion = next; started = now; decision = now + duration; ++serial;
        launchAttempted = false; pawContacts.Reset();
        waiting = cat::motion::NeedsStop(next, physics.CurrentPose().grounded, physics.CurrentPose().velocityX);
    }
    void Frame(std::uint64_t time, double seconds) {
        now = time;
        cat::environment::CursorInput input;
        input.nowMs = now; input.enabled = enabled; input.available = available;
        input.grounded = physics.CurrentPose().grounded; input.sizeScale = growth;
        input.movementScale = movementScale;
        input.pouncePreparationMs=takeoffMs;
        if(authoredPunch) {
            input.pawDurationMs=input.punchDurationMs=cat::sprite_sequence::ProgramDuration(authoredPunch->program);
            input.pawStandOff=38.15;
        }
        input.canContinue = !busy && (input.grounded || session.InFlight());
        input.canStart = input.canContinue && !waiting && (motion == Motion::Idle || motion == Motion::Watch ||
            motion == Motion::Walk || motion == Motion::Stalk || motion == Motion::Loaf);
        input.cursorX = cursorX; input.cursorHeight = cursorHeight-physics.CurrentPose().lift;
        input.supportHeight = physics.CurrentPose().lift; input.catX = physics.CurrentPose().x;
        input.minX = minX; input.maxX = maxX; input.interest = interest; input.roll = roll;
        const auto output = session.Step(input, now - previous, {motion == ownedMotion, waiting, started, serial});
        if (output.changed) {
            changes.push_back(output.action); ownedMotion = MotionFor(output.action);
            SetMotion(ownedMotion, output.action == Action::None ? 1800 : 8000);
            session.AdoptMotion(started, waiting, serial);
        }
        cat::motion::AdvanceStopGate(waiting, physics.CurrentPose().grounded,
            physics.CurrentPose().velocityX, now, started, decision);
        const double speed = session.Active() ? session.Velocity()*movementScale : 0;
        physics.SetTargetVelocity(waiting ? 0 : speed);
        if (!waiting && !launchAttempted && session.Action()==Action::Pounce && now-started>=takeoffMs) {
            launchAttempted = true;
            const bool launched = cat::cursor::LaunchPounce(physics,session.TargetX(),minX,maxX,growth,jump);
            session.ConfirmLaunch(launched);
            if (launched) ++launches;
        }
        physics.Update(seconds);
        const auto physical = physics.CurrentPose();
        const bool allowed = session.Active() && session.ContactAllowed() && session.OwnsSerial(serial) &&
            enabled && available && !busy && !waiting;
        if (cat::cursor::LandingContact(allowed && session.InFlight(),physical,cursorX,cursorHeight-physical.lift,growth))
            ++contacts;
        if (allowed && physical.grounded && std::abs(physical.velocityX)<=2 &&
                (session.Action()==Action::Paw || session.Action()==Action::Punch)) {
            const auto kind = session.Action()==Action::Punch ? cat::cursor::AttackKind::Strong : cat::cursor::AttackKind::Gentle;
            const auto pose = cat::cursor::AttackPose(kind,now-started,output.direction,
                (cursorX-physical.x)/growth,(cursorHeight-physical.lift)/growth);
            const auto paw = authoredPunch?cat::deform::Point{output.direction*38.15,38.89}:cat::cursor::PawPosition(pose);
            cat::sprite_sequence::Options options;
            if(authoredPunch)options.sheetFrames=authoredPunch->frameCount;
            const auto contact=authoredPunch?cat::sprite_sequence::ProgramContactIndex(
                cat::sprite_sequence::Clip::Punch,authoredPunch->program,now-started,options):
                static_cast<unsigned>(cat::cursor::ContactIndex(kind,now-started));
            if (pawContacts.Observe(contact,true,
                    physical.x+paw.x*growth,paw.y*growth,cursorX,cursorHeight-physical.lift,7*growth)) ++contacts;
        }
        const Motion displayed = cat::motion::DisplayedMotion(motion,walking,physical.grounded,waiting,physical.velocityX);
        if (displayed == Motion::Sniff || displayed == Motion::Loaf || displayed == Motion::ToyPaw)
            CHECK(std::abs(physics.CurrentPose().velocityX) <= 2.01);
        CHECK(std::isfinite(physical.x) && std::isfinite(physical.lift));
        previous = now;
    }
    void Pause(std::uint64_t milliseconds) {
        now += milliseconds; started += milliseconds; decision += milliseconds;
        session.Pause(milliseconds, {motion == ownedMotion, waiting, started, serial});
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

void MovingTeaseBuildsFromTapToRealFrozenTargetPounce() {
    for (int fps : {30,60,120,144}) {
        Host host;
        host.interest=1; host.cursorHeight=6;
        bool tapped=false,punched=false,chased=false,airborne=false;
        std::uint64_t firstTap=0;
        double chaseTarget=0, frozen=0, peak=0;
        const auto initial=host.physics.CurrentPose();
        for (int frame=1;frame<fps*9;++frame) {
            const double t=double(frame)/fps;
            if (!chased) host.cursorX=initial.x+28+15*std::sin(t*2*3.141592653589793*1.2);
            else host.cursorX=chaseTarget;
            if (host.session.InFlight()) host.cursorX=frozen;
            host.Frame(1000+static_cast<std::uint64_t>(std::round(t*1000)),1./fps);
            const auto action=host.session.Action();
            if(action==Action::Paw){tapped=true;if(!firstTap)firstTap=host.now;}
            punched=punched||action==Action::Punch;
            if(action==Action::Chase && !chased){chased=true;chaseTarget=host.physics.CurrentPose().x+105;}
            if(host.session.InFlight()) {airborne=true;frozen=host.session.TargetX();}
            peak=std::max(peak,host.physics.CurrentPose().lift);
            if (host.launches && host.physics.CurrentPose().grounded && !host.session.Active()) break;
        }
        CHECK(tapped && firstTap<1500);
        CHECK(punched && chased && airborne);
        CHECK(host.launches==1);
        CHECK(peak>25 && peak<32);
        CHECK(host.physics.CurrentPose().grounded);
        CHECK(std::abs(host.physics.CurrentPose().x-frozen)<20);
        CHECK(host.contacts>=1);
    }
}

void PounceOffDepartureAndInterruptionPreserveExistingImpulse() {
    for (int cancel=0;cancel<4;++cancel) {
        Host host;
        host.interest=1; host.cursorHeight=6;
        bool cancelled=false;
        int contactsBefore=0;
        double xBefore=0, vxBefore=0;
        for (int frame=1;frame<900;++frame) {
            const double t=frame/120.;
            if(!cancelled)host.cursorX=428+15*std::sin(t*2*3.141592653589793*1.2);
            if(host.session.InFlight() && !cancelled) {
                cancelled=true;contactsBefore=host.contacts;
                xBefore=host.physics.CurrentPose().x;vxBefore=host.physics.CurrentPose().velocityX;
                if(cancel==0)host.enabled=false;
                if(cancel==1)host.available=false; // Another monitor or unavailable cursor.
                if(cancel==2)host.cursorX+=500;
                if(cancel==3){host.now+=1;host.SetMotion(Motion::Eat,2400);host.busy=true;}
            }
            host.Frame(1000+static_cast<std::uint64_t>(std::round(t*1000)),1./120);
            if(cancelled && !host.physics.CurrentPose().grounded) {
                CHECK(std::abs(host.physics.CurrentPose().velocityX-vxBefore)<.001);
                CHECK(host.contacts==contactsBefore);
            }
            if(cancelled && host.physics.CurrentPose().grounded)break;
        }
        CHECK(cancelled && host.launches==1);
        CHECK(host.physics.CurrentPose().grounded);
        CHECK(host.contacts==contactsBefore);
        CHECK(std::abs(host.physics.CurrentPose().x-xBefore)<180);
        if(cancel==3)CHECK(host.motion==Motion::Eat);
    }
}

void ContactRequiresActualPawAndOwnedUnpausedAction() {
    for (double height:{6.,70.}) {
        Host host;
        host.cursorX=419;host.cursorHeight=height;
        for(int frame=1;frame<300;++frame)host.Frame(1000+frame*16, .016);
        CHECK(host.contacts==(height==6 ? 2:0));
        CHECK(host.launches==0);
    }
    Host host;
    host.cursorX=430;host.cursorHeight=6;
    host.Frame(1016,1./60);
    CHECK(host.session.Action()==Action::Watch);
    host.now=1020;host.SetMotion(Motion::Watch,4000); // Same name, different ownership generation.
    host.Pause(5000);
    host.Frame(host.now+16,1./60);
    CHECK(!host.session.Active());
    CHECK(host.motion==Motion::Watch && host.contacts==0);

    Host approached;
    approached.cursorX=560;approached.cursorHeight=6;approached.interest=1;
    for(int frame=1;frame<500;++frame)approached.Frame(1000+frame*16,.016);
    CHECK(approached.contacts==2); // Physical approach reaches the real paw, not an arbitrary sniff radius.

    Host smallAndSlow;
    smallAndSlow.growth=.6;smallAndSlow.movementScale=.55;
    smallAndSlow.cursorX=600;smallAndSlow.cursorHeight=3.6;smallAndSlow.interest=1;
    for(int frame=1;frame<900;++frame)smallAndSlow.Frame(1000+frame*16,.016);
    CHECK(smallAndSlow.contacts==2);
    CHECK(!smallAndSlow.session.Active());
}

void RareSlowStalkActuallyReachesAndTapsAtEveryFrameRate() {
    for(int fps:{30,60,120,144})for(int side:{-1,1})for(double movement:{1.,.55}) {
        Host host;host.interest=1;host.roll=0;host.movementScale=movement;
        host.growth=movement==1 ? 1 : .6;
        host.cursorX=400+side*200;host.cursorHeight=6*host.growth;
        bool sneaked=false,sniffed=false,pawed=false;
        std::uint64_t firstSneak=0,firstPaw=0;
        for(int frame=1;frame<fps*40;++frame) {
            host.Frame(1000+static_cast<std::uint64_t>(std::round(frame*1000./fps)),1./fps);
            const auto action=host.session.Action();
            if(action==Action::Sneak) {
                sneaked=true;if(!firstSneak)firstSneak=host.now;
                CHECK(host.motion==Motion::Stalk);
                CHECK(std::abs(host.session.Velocity())<=14);
                CHECK(host.session.Velocity()*side>=0);
            }
            sniffed=sniffed||action==Action::Sniff;
            if(action==Action::Paw){pawed=true;if(!firstPaw)firstPaw=host.now;}
            CHECK(action!=Action::Sit&&action!=Action::ApproachSit);
            if(pawed&&!host.session.Active())break;
        }
        CHECK(sneaked&&sniffed&&pawed&&!host.session.Active());
        CHECK(firstPaw-firstSneak>11000);
        CHECK(host.contacts==2);
        CHECK(host.launches==0);
    }
}

void PounceRespectsLedgeRoofBodySizeAndNutrition() {
    cat::physics::Controller body;
    body.Reset(400,96,960);body.SetCeiling(800);
    CHECK(body.LaunchToPlatform(250,750,400,400));
    for(int i=0;i<180;++i)body.Update(1./60);
    CHECK(body.OnPlatform());
    const auto before=body.CurrentPose();
    CHECK(!cat::cursor::LaunchPounce(body,745,320,680,1,1));
    CHECK(!cat::cursor::LaunchPounce(body,650,320,680,.6,.78));
    body.SetCeiling(410);
    CHECK(!cat::cursor::LaunchPounce(body,450,320,680,1,1));
    CHECK(body.OnPlatform() && body.CurrentPose().x==before.x && body.CurrentPose().lift==before.lift);
    body.SetCeiling(800);
    CHECK(cat::cursor::LaunchPounce(body,450,320,680,.6,.78));
    double peak=0;
    for(int i=0;i<120;++i){body.Update(1./60);peak=std::max(peak,body.CurrentPose().lift);}
    CHECK(peak>411 && peak<415);
    CHECK(body.OnPlatform() && std::abs(body.CurrentPose().lift-400)<.6);
}
void AuthoredContactOrdinalsAboveTwoRemainOncePerAction() {
    cat::cursor::PawContact contacts;
    CHECK(!contacts.Observe(0,true,0,0,0,0,7));
    CHECK(!contacts.Observe(65,true,0,0,0,0,7));
    for(unsigned index=1;index<=cat::sprite_sequence::kMaxProgramCues;++index) {
        CHECK(!contacts.Observe(index,false,0,0,0,0,7));
        CHECK(!contacts.Observe(index,true,0,0,100,100,7));
        CHECK(contacts.Observe(index,true,0,0,0,0,7));
        CHECK(!contacts.Observe(index,true,0,0,0,0,7));
    }
    contacts.Reset();CHECK(contacts.Observe(64,true,0,0,0,0,7));
    CHECK(!contacts.Observe(64,true,0,0,0,0,7));
    CHECK(contacts.Observe(1,true,0,0,0,0,7)); // High bit cannot alias legacy first contact.
}

std::uint64_t FirstAuthoredContact(const cat::sprite_assets::Descriptor& asset) {
    for(unsigned i=0;i<asset.program.cueCount;++i) {
        const auto& cue=asset.program.cues[i];
        if(!(cue.events&cat::sprite_sequence::Contact))continue;
        std::uint64_t time=cue.offsetMs;
        for(unsigned step=0;step<cue.step;++step)time+=asset.program.steps[step].durationMs;
        return time;
    }
    throw std::runtime_error("The registered punch must contain an authored contact cue");
}

void AuthoredPawUsesItsPaintedEndpointFullClockAndPause() {
    for(const int fps:{30,60,120,144})for(const double growth:{.6,1.})
        for(const double height:{6.,38.89,70.}) {
            Host host;host.UseAuthoredCream();host.growth=growth;
            host.cursorX=400+38.15*growth;host.cursorHeight=height*growth;
            const auto cue=FirstAuthoredContact(*host.authoredPunch);
            const auto duration=cat::sprite_sequence::ProgramDuration(host.authoredPunch->program);
            CHECK(cue>650 && duration>850);
            bool pawed=false,paused=false,visibleCue=false;
            std::uint64_t pauseOffset=0;
            for(int frame=1;frame<fps*9;++frame) {
                const auto before=host.contacts;
                host.Frame(1000+static_cast<std::uint64_t>(std::round(frame*1000./fps))+pauseOffset,1./fps);
                if(host.session.Action()==Action::Paw) {
                    pawed=true;
                    cat::sprite_sequence::Options options;options.sheetFrames=host.authoredPunch->frameCount;
                    visibleCue=visibleCue || cat::sprite_sequence::ProgramContactActive(cat::sprite_sequence::Clip::Punch,
                        host.authoredPunch->program,host.now-host.started,options);
                    if(!paused && host.now-host.started>=cue-100) {
                        CHECK(host.contacts==0);
                        const auto phase=host.now-host.started;
                        host.Pause(5000);pauseOffset+=5000;paused=true;
                        CHECK(host.now-host.started==phase);
                        CHECK(host.contacts==0 && host.session.Action()==Action::Paw);
                    }
                }
                if(host.contacts>before) {
                    CHECK(height==38.89 && host.session.Action()==Action::Paw);
                    CHECK(host.now-host.started>=cue && host.now-host.started<duration);
                    const auto count=host.contacts;
                    host.Frame(host.now,0); // A repeated contact callback is not a second strike.
                    CHECK(host.contacts==count);
                }
            }
            CHECK(pawed && paused && visibleCue);
            CHECK(host.contacts==(height==38.89?1:0));
            CHECK(host.launches==0 && !host.session.Active());
        }
}

void AuthoredPounceWaitsForEveryPreparationDrawing() {
    for(const int fps:{30,60,120,144}) {
        Host host;host.UseAuthoredCream();host.interest=1;host.cursorHeight=6;
        bool tapped=false,punched=false,chased=false,prepared=false,airborne=false;
        double chaseTarget=0,frozen=0;
        CHECK(host.takeoffMs>240);
        for(int frame=1;frame<fps*10;++frame) {
            const double t=static_cast<double>(frame)/fps;
            host.cursorX=chased?chaseTarget:428+15*std::sin(t*2*3.141592653589793*1.2);
            if(host.session.InFlight())host.cursorX=frozen;
            host.Frame(1000+static_cast<std::uint64_t>(std::round(t*1000)),1./fps);
            const auto action=host.session.Action();
            tapped=tapped||action==Action::Paw;punched=punched||action==Action::Punch;
            if(action==Action::Chase && !chased){chased=true;chaseTarget=host.physics.CurrentPose().x+105;}
            if(action==Action::Pounce && host.now-host.started<host.takeoffMs) {
                prepared=true;CHECK(host.launches==0 && host.physics.CurrentPose().grounded);
            }
            if(host.session.InFlight()) {
                airborne=true;frozen=host.session.TargetX();
                CHECK(host.launches==1 && host.now-host.started>=host.takeoffMs);
            }
            if(host.launches && host.physics.CurrentPose().grounded && !host.session.Active())break;
        }
        CHECK(tapped && punched && chased && prepared && airborne);
        CHECK(host.launches==1 && host.physics.CurrentPose().grounded && !host.session.Active());
        CHECK(host.contacts==1); // The floor pointer misses the high paw; only real pounce landing touches it.
        CHECK(std::abs(host.physics.CurrentPose().x-frozen)<20);
    }
}
}

int main() {
    try {
        WalkingPetAcknowledgesLowCursorAndCompletesContact(); OffCancelsDuringBrakingAndOnRespondsWithoutOldCooldown();
        InterruptingActionWinsAndPauseDoesNotSkipToPaw(); RealMotorReachesRareCursorSeatThenStops();
        MovingTeaseBuildsFromTapToRealFrozenTargetPounce();
        PounceOffDepartureAndInterruptionPreserveExistingImpulse();
        ContactRequiresActualPawAndOwnedUnpausedAction(); PounceRespectsLedgeRoofBodySizeAndNutrition();
        AuthoredContactOrdinalsAboveTwoRemainOncePerAction();
        RareSlowStalkActuallyReachesAndTapsAtEveryFrameRate();
        AuthoredPawUsesItsPaintedEndpointFullClockAndPause();AuthoredPounceWaitsForEveryPreparationDrawing();
        std::cout << "Cursor host and Box2D: " << checks << " checks passed\n";
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
