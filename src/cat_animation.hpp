#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include "cat_deformation.hpp"
#include "cat_expression.hpp"
#include "cat_gait.hpp"
#include "cat_walk_dynamics.hpp"
#include "cat_sprite_sequence.hpp"
#include "cat_authored_locomotion.hpp"

namespace cat::animation {

enum class Motion { Idle, Walk, Sleep, Eat, Groom, Stretch, Watch, Stalk, Pounce, Zoomies, TailChase, Punch, Jump, Treat, EarFlick, TailRaise, BellyRoll, Loaf, Knead, Peek, Scratch, Sneeze, ToyPaw, BoxSit, Sniff, LookDown, Balance, Startled, Yawn, SleepTalk, Hang, Pride };
constexpr std::uint64_t kFirstPunchMs = 300;
constexpr std::uint64_t kSecondPunchMs = 600;
constexpr std::uint64_t kPunchDurationMs = 2900;
constexpr std::uint64_t kJumpDurationMs = 1450;
constexpr std::uint64_t kTreatDurationMs = 4200;
constexpr std::uint64_t kEarFlickDurationMs = 2200;
constexpr std::uint64_t kTailRaiseDurationMs = 3400;
constexpr std::uint64_t kPeekDurationMs = 3600;
constexpr std::uint64_t kBellyRollDurationMs = 4300;
constexpr double kPi = 3.14159265358979323846;
constexpr int kSpriteCount = 29;
// Original complete drawings have about 12–15 logical pixels of forepaw
// excursion at the 110 px reference size. Keep the established 14 px cycle.
// Four poses cannot describe a complete fast gallop; cap their presentation
// cadence instead of buzzing through them at 47 fps when moving 165 px/s.
constexpr double kOriginalWalkStride = 14.0;
constexpr double kOriginalWalkMaxFramesPerSecond = 12.0;

// Offsets are logical pixels; the native renderer applies monitor DPI and real-age growth.
struct Pose {
    bool fishMeal=false; // Transient accepted feeding presentation; never serialized.
    cat::sprite_sequence::Clip spriteClip=cat::sprite_sequence::Clip::None;
    unsigned spriteFrame=0;
    bool spriteTimed=false;
    std::uint64_t spriteElapsedMs=0;
    cat::sprite_sequence::Options spriteOptions{};
    int frame = 4;
    double x = 0, lift = 0, scaleX = 1, scaleY = 1, angle = 0;
    bool flip = false;
    bool centerPivot = false;
    int blendFrame = -1;
    double blend = 0;
    cat::deform::Joints joints;
    bool groundContact = true;
    bool bellyRig = false;
    cat::expression::Face face;
    bool walkingRig=false;
    bool originalWalk=false;
    bool jointWalk=false;
    cat::gait::Feet jointFeet{};
    double jointPhase=0;
    cat::walk_dynamics::Frame jointDynamics{};
    double walkPhase=0,walkStride=24,walkDuty=.68,walkLift=3.2;
    cat::gait::Feet walkFeet{};
    bool walkFeetValid=false;
};

// Authored artwork is an optional presentation layer. Canonical frame IDs,
// physical root height and saved state remain independent of sheet layouts.
inline void AttachSprite(Pose& pose,cat::sprite_sequence::Clip clip,std::uint64_t elapsed,
                         const cat::sprite_sequence::Options& options={}) {
    pose.spriteClip=clip;pose.spriteTimed=true;
    pose.spriteElapsedMs=elapsed;pose.spriteOptions=options;
    pose.spriteFrame=cat::sprite_sequence::Sample(clip,elapsed,options).frame;
}

inline void UseAuthoredSprite(Pose& pose) {
    // The authored frame already contains local acting. Keep the physical
    // root and registered anchor; do not add legacy punch shifts or warps.
    pose.x=pose.lift=pose.angle=0;pose.scaleX=pose.scaleY=1;
    pose.centerPivot=pose.bellyRig=false;pose.blendFrame=-1;pose.blend=0;
    pose.joints={};pose.face={};
}

inline double SmoothStep(double fraction) {
    const double t = std::clamp(fraction, 0.0, 1.0);
    return t * t * (3 - 2 * t);
}

// Hosts use this while Box2D still reports support, including a window jump
// whose target is being prepared after the preview takeoff deadline. The
// action clock alone must never select flight art before actual takeoff.
inline Pose JumpPreparationPose(std::uint64_t elapsed,int direction,bool pounce=false) {
    Pose p;
    p.flip=direction<0;
    p.joints.crouch=.65*SmoothStep(static_cast<double>(elapsed)/(pounce?240.0:220.0));
    cat::sprite_sequence::Options sprite;
    sprite.jump=cat::sprite_sequence::JumpPhase::Crouch;
    sprite.grounded=true;
    AttachSprite(p,cat::sprite_sequence::Clip::Jump,elapsed,sprite);
    return p;
}

// Use one registered airborne drawing throughout flight. The old16/17 blend
// morphed independently drawn faces and produced two eyes at the apex. Box2D
// supplies height; vertical velocity only controls a small tilt and local paws.
inline Pose AirbornePose(double velocityY,int direction) {
    Pose p;
    const double speed=std::isfinite(velocityY) ? std::clamp(velocityY,-10000.0,10000.0) : 0.0;
    cat::sprite_sequence::Options sprite;
    sprite.jump=cat::sprite_sequence::JumpPhase::Airborne;sprite.grounded=false;sprite.velocityY=speed;
    AttachSprite(p,cat::sprite_sequence::Clip::Jump,0,sprite);
    p.frame=16;
    p.flip=direction<0;
    p.groundContact=false;
    p.angle=(p.flip ? -1.0 : 1.0)*std::clamp(speed/120.0,-2.0,2.0);
    const double tuck=1-SmoothStep(std::abs(speed)/140.0);
    p.joints.frontLeft=tuck;
    p.joints.frontRight=.85*tuck;
    return p;
}

// Absorb contact in the torso while the head translates rigidly and the paws
// stay planted. This is only a stopped landing; a moving landing keeps its
// distance-driven gait until Box2D has actually removed horizontal velocity.
inline Pose LandingPose(std::uint64_t elapsedSinceContact,double strength,int direction) {
    Pose p;
    p.flip=direction<0;
    if(elapsedSinceContact<cat::sprite_sequence::kLandingMs) {
        cat::sprite_sequence::Options sprite;
        sprite.jump=cat::sprite_sequence::JumpPhase::Landing;sprite.contactElapsedMs=elapsedSinceContact;
        AttachSprite(p,cat::sprite_sequence::Clip::Jump,0,sprite);
    }
    const double impact=std::isfinite(strength) ? std::clamp(strength,0.0,1.0) : 0.0;
    if(elapsedSinceContact<280)
        p.joints.crouch=.7*impact*std::sin(elapsedSinceContact/280.0*kPi);
    return p;
}

inline int PunchHits(std::uint64_t elapsed) {
    return elapsed >= kSecondPunchMs ? 2 : elapsed >= kFirstPunchMs ? 1 : 0;
}
inline bool PunchContact(std::uint64_t elapsed) {
    return (elapsed >= kFirstPunchMs && elapsed < 460) ||
           (elapsed >= kSecondPunchMs && elapsed < 790);
}

inline bool Travels(Motion motion) {
    return motion == Motion::Walk || motion == Motion::Stalk || motion == Motion::Pounce || motion == Motion::Zoomies;
}
inline bool Walking(Motion motion) {
    return motion==Motion::Walk || motion==Motion::Stalk || motion==Motion::Zoomies;
}
inline double StrideLength(Motion motion) {
    // Nominal steady gait; the live controller blends from observed speed.
    return motion==Motion::Zoomies ? 52.0 : motion==Motion::Stalk ? 20.0 : 24.0;
}

inline double TravelFactor(Motion motion, std::uint64_t elapsed) {
    if (motion == Motion::Pounce) return elapsed >= 240 && elapsed < 840 ? 1.0 : 0.0;
    return Travels(motion) ? 1.0 : 0.0;
}

// Persistent gait phase follows ground actually covered, so weight/age changes,
// braking and a blocked boundary cannot make the paws keep stepping in place.
// Advance from rendered, ground-relative displacement divided by DPI * growth.
// Reset only for drag/viewport relocation. Hidden/menu pauses preserve the
// rendered fraction and phase, so resuming cannot add a phantom footstep.
class Locomotion {
public:
    Locomotion() { jointCycle_.Prime(); }
    void Reset() {
        cycle_.Reset();
        jointCycle_.Configure(cat::walk_dynamics::ParametersFor(cat::walk_dynamics::Mode::Walk));
        jointCycle_.Reset();jointCycle_.Prime();originalWalkPhase_=0;
        authoredCycle_.Reset();
        jointMode_=cat::walk_dynamics::Mode::Walk;
    }

    // Returns false for a discontinuous or invalid sample. The allowance grows
    // with dt so a normal run during a delayed 120 ms tick is not a teleport.
    bool Advance(Motion motion, double signedDistanceBodyPx, double deltaSeconds, bool grounded=true) {
        if (!std::isfinite(signedDistanceBodyPx) || !std::isfinite(deltaSeconds) ||
                deltaSeconds < 0 || deltaSeconds > .25 ||
                std::abs(signedDistanceBodyPx) > std::max(12.0, 500.0 * deltaSeconds)) {
            Reset();
            return false;
        }
        if (deltaSeconds == 0) {
            if (signedDistanceBodyPx == 0) return true;
            Reset();
            return false;
        }
        cycle_.Advance(std::abs(signedDistanceBodyPx),deltaSeconds,Walking(motion),motion==Motion::Stalk);
        if(Walking(motion)) {
            // Keep the running gait while the physical body is still braking.
            // Requested action changes must not stand the torso upright over
            // a paw that is still supporting a fast running stride.
            jointMode_=cat::walk_dynamics::SelectMode(jointMode_,Speed());
            const auto mode=jointMode_;
            std::array<double,4> bias{};
            for(unsigned leg=0;leg<4;++leg)bias[leg]=cat::walk_dynamics::PawBias(mode,leg);
            jointCycle_.Configure(motion==Motion::Stalk && mode==cat::walk_dynamics::Mode::Walk?cat::walk_dynamics::ParametersForStalk():
                cat::walk_dynamics::ParametersFor(mode),bias);
        }
        jointCycle_.Advance(std::abs(signedDistanceBodyPx),deltaSeconds,
            Walking(motion),false);
        if (Walking(motion)) {
            const double distancePhase=std::abs(signedDistanceBodyPx)/kOriginalWalkStride;
            const double cadenceLimit=deltaSeconds*kOriginalWalkMaxFramesPerSecond/4;
            originalWalkPhase_=std::fmod(originalWalkPhase_+std::min(distancePhase,cadenceLimit),1.0);
            if(originalWalkPhase_>1-1e-12)originalWalkPhase_=0;
        }
        if(!authoredCycle_.Advance(signedDistanceBodyPx,deltaSeconds,Walking(motion),grounded)) {
            Reset();
            return false;
        }
        return true;
    }

    double Phase() const { return cycle_.Phase(); }
    double OriginalWalkPhase() const { return originalWalkPhase_; }
    // Register once when art/body width changes. Reconfiguration preserves the
    // step fraction; changing an action name never restarts an authored cycle.
    bool ConfigureAuthoredStrides(double walkStride,double runStride,unsigned standingFrame=0) {
        standingFrame_=standingFrame;
        return authoredCycle_.Configure(walkStride,runStride);
    }
    bool AuthoredConfigured() const { return authoredCycle_.Configured(); }
    double AuthoredPhase() const { return authoredCycle_.Phase(); }
    cat::authored_locomotion::Mode AuthoredMode() const { return authoredCycle_.CurrentMode(); }
    double AuthoredStride() const { return authoredCycle_.Stride(); }
    unsigned AuthoredStandingFrame() const {return standingFrame_;}
    bool AuthoredStanding() const {return authoredCycle_.Standing();}
    bool RecentAuthoredWalk() const {return authoredCycle_.RecentWalk();}
    double Speed() const { return cycle_.Speed(); } // Observed body pixels per second.
    cat::gait::Parameters Settings(Motion motion) const { return cycle_.Settings(motion==Motion::Stalk); }
    cat::gait::Feet Feet(Motion motion) const { return cycle_.Targets(motion==Motion::Stalk); }
    cat::gait::Feet JointFeet() const { return jointCycle_.Targets(); }
    double JointPhase() const { return jointCycle_.Phase(); }
    cat::walk_dynamics::Mode JointMode() const { return jointMode_; }
    cat::gait::Feet GallopFeet() const { return jointCycle_.Targets(); }
    double GallopPhase() const { return jointCycle_.Phase(); }

private:
    cat::gait::Cycle cycle_;
    cat::gait::Cycle jointCycle_{cat::walk_dynamics::ParametersFor(cat::walk_dynamics::Mode::Walk),false,0};
    double originalWalkPhase_=0;
    unsigned standingFrame_=0;
    cat::authored_locomotion::Cycle authoredCycle_;
    cat::walk_dynamics::Mode jointMode_=cat::walk_dynamics::Mode::Walk;
};

inline cat::sprite_sequence::Clip AuthoredLocomotionClip(const Locomotion& locomotion) {
    return locomotion.AuthoredMode()==cat::authored_locomotion::Mode::Run?
        cat::sprite_sequence::Clip::Run:cat::sprite_sequence::Clip::Walk;
}

// Call after actual support, landing, cursor contact and toy overrides. The
// host proves that the selected character owns the requested registered clip;
// absent art keeps its complete original drawings and fallback behavior.
inline bool ApplyAuthoredLocomotion(Pose& pose,Motion displayed,const Locomotion& locomotion,
                                   bool resourceAvailable) {
    // Finish braking on the same side-view animal instead of immediately
    // replacing it with a front-facing seated cat. No image blending/warping.
    const bool neutral=displayed==Motion::Idle || displayed==Motion::Watch;
    if(resourceAvailable && locomotion.AuthoredConfigured() && pose.groundContact &&
            ((locomotion.AuthoredStanding() && Walking(displayed)) ||
                (neutral && !pose.originalWalk && locomotion.RecentAuthoredWalk() && pose.spriteClip==cat::sprite_sequence::Clip::None))) {
        if(locomotion.AuthoredStanding()) {
            pose.spriteClip=cat::sprite_sequence::Clip::Walk;pose.spriteTimed=false;pose.spriteFrame=locomotion.AuthoredStandingFrame();
        } else {
            // During the first 100ms below walking speed retain the last side
            // pose. Falling back to the front neutral here caused a one-frame
            // sit before Standing() became ready, even without a new action.
            cat::sprite_sequence::Options options;options.cyclePhase=locomotion.AuthoredPhase();
            AttachSprite(pose,AuthoredLocomotionClip(locomotion),0,options);
        }
        UseAuthoredSprite(pose);pose.originalWalk=pose.jointWalk=pose.walkingRig=false;
        return true;
    }
    if(!resourceAvailable || !locomotion.AuthoredConfigured() || !Walking(displayed) ||
            !pose.groundContact || !pose.originalWalk ||
            pose.spriteClip!=cat::sprite_sequence::Clip::None)return false;
    cat::sprite_sequence::Options options;
    options.cyclePhase=locomotion.AuthoredPhase();
    // The registered program maps this fraction to its actual source playlist
    // (currently eight Walk and eight Run drawings). Elapsed time never steps
    // a blocked cat, and extra frames are never invented by interpolation.
    AttachSprite(pose,AuthoredLocomotionClip(locomotion),0,options);
    UseAuthoredSprite(pose);
    pose.originalWalk=pose.jointWalk=pose.walkingRig=false;
    return true;
}

inline Pose Evaluate(Motion motion, std::uint64_t elapsed, std::uint64_t ticks, int direction) {
    Pose p;
    const double t = static_cast<double>(elapsed);
    // The physics controller owns root height. Breathing moves the chest,
    // never the feet or the whole picture above the supporting surface.
    p.joints.breath = .35 * std::sin(ticks / 1200.0);
    p.flip = direction < 0;
    const auto crossfade = [&](int first, int second, double fraction) {
        p.frame = first;
        p.blendFrame = second;
        p.blend = SmoothStep(fraction);
    };
    switch (motion) {
    case Motion::Idle:
        p.frame = 4;
        p.face.blink = SmoothStep((170.0 - std::abs(static_cast<double>(ticks % 6100) - 170.0)) / 100.0);
        break;
    case Motion::Walk:
        // The original v1.0 preview clock. Independently painted eyes/paws
        // must never be mixed into a double image between these drawings.
        p.frame = static_cast<int>(elapsed / 150 % 4);
        p.joints = {};
        break;
    case Motion::Sleep:
        p.frame = 6;
        p.joints.breath = .45 * std::sin(t / 1500.0);
        break;
    case Motion::Eat:
        p.frame = 7;
        p.joints = {};
        p.joints.headNod = .42 * std::sin(t / 260.0);
        break;
    case Motion::Treat:
        p.frame = 18;
        p.joints = {};
        p.joints.headNod = .25 * std::sin(t / 280.0);
        p.joints.frontLeft = .5-.5*std::cos(t / 230.0 * kPi);
        break;
    case Motion::Groom:
        p.frame = 9;
        p.joints = {};
        p.joints.headNod = .32 * std::sin(t / 440.0 * kPi);
        p.joints.frontRight = .5-.5*std::cos(t / 440.0 * kPi);
        break;
    case Motion::Stretch:
        p.frame = 8;
        p.joints = {};
        p.joints.breath = std::sin(std::min(t / 1900.0, 1.0) * kPi);
        break;
    case Motion::Watch:
        p.frame = 4;
        p.joints.headNod = .35 * std::sin(t / 950.0);
        break;
    case Motion::Stalk:
        // Sneaking is locomotion. A static crouch (and, for added species,
        // its sitting-art fallback) must never translate along the floor.
        p.frame = static_cast<int>(elapsed / 240 % 4);
        p.joints={};
        break;
    case Motion::Pounce:
        if (elapsed < 240) {
            p.frame = 4;
            p.joints.crouch = .65 * SmoothStep(t / 240.0);
        } else if (elapsed < 840) {
            p = AirbornePose(((t-240)/600-.5)*400,direction);
        } else {
            p = LandingPose(elapsed-840,1,direction);
        }
        break;
    case Motion::Jump:
        // Preview timing only; runtime uses actual flight/contact from Box2D.
        p.lift = 0;
        if (elapsed < 220) {
            p.frame = 4;
            p.joints.crouch = .65 * SmoothStep(t / 220.0);
        } else if (elapsed < 1060) {
            p = AirbornePose(((t-220)/840-.5)*400,direction);
        } else {
            p = LandingPose(elapsed-1060,1,direction);
        }
        break;
    case Motion::Zoomies:
        p.frame = static_cast<int>(elapsed / 84 % 4);
        p.joints={};
        p.angle = 0;
        break;
    case Motion::TailChase:
        p.frame = 15;
        p.joints = {};
        p.joints.tailRaise = .5-.5*std::cos(t / 520.0 * kPi);
        p.joints.headNod = .4*std::sin(t / 520.0 * kPi);
        break;
    case Motion::Punch:
        if (elapsed < kFirstPunchMs) {
            p.frame = 12;
        } else if (PunchContact(elapsed)) {
            p.frame = 13;
            const double strike = elapsed < 460 ? t - kFirstPunchMs : t - kSecondPunchMs;
            p.x = direction * (2 + 3 * std::max(0.0, 1 - strike / 150.0));
        } else if (elapsed < kSecondPunchMs) {
            p.frame = 12;
        } else {
            p.frame = 14;
            const double recover = std::max(0.0, 1 - (t - 790) / 800.0);
            p.joints.headNod = -.3 * std::sin((t - 790) / 120.0) * recover;
        }
        break;
    case Motion::EarFlick:
        // The same face, chest and paws are retained throughout. Only ear
        // vertices bend around their roots; alternate whole-cat art is gone.
        p.frame = 4; p.joints = {};
        p.joints.leftEar = SmoothStep((t - 160) / 150) * (1 - SmoothStep((t - 420) / 200));
        p.joints.rightEar = SmoothStep((t - 960) / 150) * (1 - SmoothStep((t - 1230) / 220));
        break;
    case Motion::TailRaise:
        p.frame = 4; p.joints = {};
        p.joints.tailRaise = SmoothStep((t - 120) / 780) * (1 - SmoothStep((t - 2100) / 900));
        break;
    case Motion::Loaf:
        // Compact loaf: tuck the feet under the body and breathe slowly.
        p.frame = 4;
        p.joints.crouch = SmoothStep(t / 350.0);
        p.joints.frontLeft = p.joints.frontRight = .7;
        p.lift = 0; // The compressed loaf still rests on its paws, above the edge.
        break;
    case Motion::Knead:
        // Alternating paws using the clean grooming poses, with a gentle shoulder bob.
        p.frame = 4;
        p.joints.frontLeft = std::max(0.0, std::sin(t / 420.0 * kPi));
        p.joints.frontRight = std::max(0.0, -std::sin(t / 420.0 * kPi));
        p.joints.headNod = .12 * std::sin(t / 840.0 * kPi);
        break;
    case Motion::Peek: {
        // Peek from a supported crouch. Toy/window foreground supplies cover;
        // the renderer must not put the cat through its supporting floor.
        p.frame = 4;
        const double phase = std::clamp(t / kPeekDurationMs, 0.0, 1.0);
        const double dip = phase < .3 ? SmoothStep(phase / .3) : phase > .72 ? SmoothStep((1.0 - phase) / .28) : 1.0;
        p.joints.crouch = dip;
        p.joints.headNod = .55 * std::sin(t / 620.0) * dip;
        break;
    }
    case Motion::Scratch:
        p.frame = 9;
        p.joints = {};
        p.joints.headNod = .15 * std::sin(t / 180.0 * kPi);
        p.joints.frontRight = .5-.5*std::cos(t / 180.0 * kPi);
        break;
    case Motion::Sneeze: {
        p.frame = 4;
        const double phase = std::clamp(t / 1700.0, 0.0, 1.0);
        const double snap = std::exp(-std::pow((phase - .42) / .12, 2.0));
        p.joints.headNod = -.95 * snap;
        p.joints.leftEar = p.joints.rightEar = .45 * snap;
        p.joints.breath = -.6 * snap;
        p.face.blink = snap; p.face.mouth = .5 * snap;
        break;
    }
    case Motion::ToyPaw:
        p.frame = elapsed < 330 ? 12 : elapsed < 660 ? 13 : 4;
        p.x = elapsed >= 330 && elapsed < 660 ? direction * 3.0 * std::sin((t-330) / 330.0 * kPi) : 0;
        break;
    case Motion::BoxSit:
        p.frame = 4;
        p.joints.crouch = .65 * SmoothStep(t / 350.0);
        break;
    case Motion::Yawn:
        p.face.mouth = std::pow(std::sin(std::clamp(t / 2400.0, 0.0, 1.0) * kPi), 2);
        p.face.blink = SmoothStep(p.face.mouth/.35);
        p.joints.headNod = -.3 * p.face.mouth;
        break;
    case Motion::SleepTalk:
        p.face.blink = 1;
        p.face.mouth = .3 * std::max(0.0, std::sin(t / 130.0));
        p.joints.crouch = 1;
        break;
    case Motion::Hang:
        p.frame = 26;
        p.bellyRig = true; // Reuse the single canonical head with hanging body/paws.
        p.groundContact = false;
        p.joints.headNod = .45;
        p.joints.frontLeft = .5 * (.5 + .5 * std::sin(t / 340.0));
        p.joints.frontRight = .5 * (.5 + .5 * std::cos(t / 340.0));
        p.face.wide = .4; p.face.brow = .3;
        break;
    case Motion::Pride:
        p.joints.headNod = -.4 * SmoothStep(t / 450.0);
        p.joints.tailRaise = .8;
        p.face.blink = .35; p.face.smile = .7;
        break;
    case Motion::Sniff:
        p.frame = 4;
        p.joints.headNod = .20 * std::sin(t / 140.0);
        p.joints.breath = .25 * std::sin(t / 110.0);
        break;
    case Motion::LookDown:
        p.frame = 4;
        p.joints.headNod = .75 * SmoothStep(t / 480.0);
        break;
    case Motion::Balance:
        p.frame = 4;
        p.joints.headNod = .4 * std::sin(t / 170.0) * (1 - SmoothStep(t / 1300.0));
        p.joints.frontLeft = .6 * std::max(0.0, std::sin(t / 170.0));
        p.joints.frontRight = .6 * std::max(0.0, -std::sin(t / 170.0));
        break;
    case Motion::Startled: {
        p.frame = 4;
        const double recovery = 1 - SmoothStep(t / 1200.0);
        p.joints.leftEar = p.joints.rightEar = .65 * recovery;
        p.joints.headNod = -.5 * recovery;
        p.face.wide = recovery; p.face.brow = -.75 * recovery; p.face.mouth = .35 * recovery;
        break;
    }
    case Motion::BellyRoll: {
        // Keep each reclining head attached to its painted neck and grounded
        // skull. Replacing it with the upright sitting head twists the neck.
        p.joints = {};
        p.bellyRig = false;
        // Register each pose change in a brief smooth movement. A half-second
        // hold at partial opacity exposed two separate sets of paws/tails.
        // Head and torso turn together; no independent upright head overlay.
        const auto turn=[&](int first,int second,double local,double duration){
            crossfade(first,second,(local-(duration-180)/2)/180);
        };
        if (elapsed < 520) turn(4, 24, t, 520);
        else if (elapsed < 1120) turn(24, 25, t - 520, 600);
        else if (elapsed < 3140) p.frame = 25;
        else if (elapsed < 3740) turn(25, 24, t - 3140, 600);
        else if (elapsed < kBellyRollDurationMs) turn(24, 4, t - 3740, 560);
        else p.frame = 4;
        const double settle = SmoothStep((t - 800) / 500) * (1 - SmoothStep((t - 3000) / 600));
        p.joints.frontLeft = settle * (.5 + .5 * std::sin(t / 360.0));
        p.joints.frontRight = settle * (.5 + .5 * std::cos(t / 430.0));
        p.joints.breath = settle * .7 * std::sin(t / 650.0);
        p.joints.headNod = 0; // The resting skull stays on the floor; only paws/belly move.
        break;
    }
    }
    using Clip=cat::sprite_sequence::Clip;
    switch(motion) {
    case Motion::Groom:AttachSprite(p,Clip::Groom,elapsed);break;
    case Motion::Sleep:AttachSprite(p,Clip::Sleep,elapsed);break;
    case Motion::Eat:AttachSprite(p,Clip::Eat,elapsed);break;
    case Motion::Treat:AttachSprite(p,Clip::Treat,elapsed);break;
    case Motion::Punch:AttachSprite(p,Clip::Punch,elapsed);break;
    case Motion::Jump:AttachSprite(p,Clip::Jump,elapsed);break;
    default:break;
    }
    return p;
}

// Sleep variants are chosen once by the controller, never by rendering time.
// Every variant retains a physical ground baseline and one registered face.
inline Pose EvaluateSleep(unsigned variant, std::uint64_t elapsed, std::uint64_t ticks, int direction, bool perched = false) {
    Pose p=Evaluate(Motion::Sleep,elapsed,ticks,direction);
    if(variant%6==1) { p.frame=24; p.bellyRig=false; p.face.blink=1; }
    else if(variant%6==2) {p.frame=4;p.joints.crouch=1;p.face.blink=1;}
    else if(variant%6==3) {p.frame=25;p.bellyRig=false;p.face.blink=1;}
    else if(variant%6==4) p.frame=27;
    else if(variant%6==5) p.frame=28;
    if(variant%6!=0 || perched)p.spriteClip=cat::sprite_sequence::Clip::None;
    if(perched){p.frame=4;p.bellyRig=false;p.joints.crouch=.35;p.joints.headNod=.45+.25*std::sin(elapsed/2300.0);p.face.blink=.9+.1*std::sin(elapsed/3100.0);}
    return p;
}

// Compatible opt-in path for a physics-backed caller. The original evaluator
// remains available for previews and its time-based jump lift can be replaced
// by the caller's physical height without changing any sprite identities.
inline Pose Evaluate(Motion motion, std::uint64_t elapsed, std::uint64_t ticks,
                     int direction, const Locomotion& locomotion) {
    Pose p = Evaluate(motion, elapsed, ticks, direction);
    if (!Walking(motion)) return p;

    const double phase = locomotion.Phase();
    p.lift = 0;
    p.x = 0;
    p.scaleX = p.scaleY = 1;
    p.angle = 0;
    p.blendFrame = -1;
    p.blend = 0;
    const auto settings=locomotion.Settings(motion);
    p.joints={};p.face={};p.walkPhase=phase;p.walkStride=settings.stride;
    p.walkDuty=settings.duty;p.walkLift=settings.lift;
    p.walkFeet=locomotion.Feet(motion);p.walkFeetValid=true;
    // Keep each painted animal intact: no extracted limb strips, body plate,
    // joint warp or face interpolation. Walk/stalk/run share this same phase,
    // so an action-name change cannot reset a step. The renderer retains the
    // inverse-warp fallback only for newer animals without these four poses.
    p.frame=static_cast<int>(locomotion.OriginalWalkPhase()*4);
    p.originalWalk=true;p.walkingRig=false;p.jointWalk=false;
    return p;
}

} // namespace cat::animation
