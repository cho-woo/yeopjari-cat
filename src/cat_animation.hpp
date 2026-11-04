#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include "cat_deformation.hpp"
#include "cat_expression.hpp"

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

// Offsets are logical pixels; the native renderer applies monitor DPI and real-age growth.
struct Pose {
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
};

inline double SmoothStep(double fraction) {
    const double t = std::clamp(fraction, 0.0, 1.0);
    return t * t * (3 - 2 * t);
}

// Use one registered airborne drawing throughout flight. The old16/17 blend
// morphed independently drawn faces and produced two eyes at the apex. Box2D
// supplies height; vertical velocity only controls a small tilt and local paws.
inline Pose AirbornePose(double velocityY,int direction) {
    Pose p;
    const double speed=std::isfinite(velocityY) ? std::clamp(velocityY,-10000.0,10000.0) : 0.0;
    p.frame=16;
    p.flip=direction<0;
    p.groundContact=false;
    p.angle=(p.flip ? -1.0 : 1.0)*std::clamp(speed/120.0,-2.0,2.0);
    const double tuck=1-SmoothStep(std::abs(speed)/140.0);
    p.joints.frontLeft=tuck;
    p.joints.frontRight=.85*tuck;
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

inline double TravelFactor(Motion motion, std::uint64_t elapsed) {
    if (motion == Motion::Pounce) return elapsed >= 240 && elapsed < 840 ? 1.0 : 0.0;
    return Travels(motion) ? 1.0 : 0.0;
}

// Persistent gait phase follows ground actually covered, so weight/age changes,
// braking and a blocked boundary cannot make the paws keep stepping in place.
// Advance once per simulation update, using post-collision displacement divided
// by DPI * growth scale. Rendering must not advance this state. Reset after a
// drag, work-area relocation or a hidden/menu pause; those are not footsteps.
class Locomotion {
public:
    void Reset() { phase_ = speed_ = 0; }

    // Returns false for a discontinuous or invalid sample. The allowance grows
    // with dt so a normal run during a delayed 120 ms tick is not a teleport.
    bool Advance(Motion motion, double signedDistanceBodyPx, double deltaSeconds) {
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
        const double distance = std::abs(signedDistanceBodyPx);
        const double observedSpeed = std::min(500.0, distance / deltaSeconds);
        const double response = -std::expm1(-deltaSeconds / .075);
        speed_ += (observedSpeed - speed_) * response;
        if (motion == Motion::Walk || motion == Motion::Zoomies || motion == Motion::Stalk) {
            const double stride = motion == Motion::Zoomies ? 38.0 : 14.0;
            phase_ = std::fmod(phase_ + distance / stride, 1.0);
        }
        return true;
    }

    double Phase() const { return phase_; } // One full four-frame stride, [0, 1).
    double Speed() const { return speed_; } // Observed body pixels per second.

private:
    double phase_ = 0, speed_ = 0;
};

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
        p.frame = static_cast<int>(elapsed / 145 % 4);
        crossfade(p.frame, (p.frame + 1) % 4, static_cast<double>(elapsed % 145) / 145);
        break;
    case Motion::Sleep:
        p.frame = 6;
        p.joints.breath = .45 * std::sin(t / 1500.0);
        break;
    case Motion::Eat:
        p.frame = 7;
        p.scaleY = 1 + .012 * std::sin(t / 110.0);
        break;
    case Motion::Treat:
        p.frame = 18 + static_cast<int>(elapsed / 200 % 2);
        if (elapsed % 200 >= 150)
            crossfade(p.frame, p.frame == 18 ? 19 : 18, static_cast<double>(elapsed % 200 - 150) / 50);
        p.lift = 0;
        p.scaleY = 1 + .009 * std::sin(t / 200.0 * kPi);
        break;
    case Motion::Groom:
        crossfade(9, 10, .5 - .5 * std::cos(t / 440.0 * kPi));
        p.joints.headNod = .2 * std::sin(t / 440.0 * kPi);
        break;
    case Motion::Stretch:
        p.frame = 8;
        p.scaleX = 1 + .015 * std::sin(std::min(t / 1900.0, 1.0) * kPi);
        p.scaleY = 1 - .008 * std::sin(std::min(t / 1900.0, 1.0) * kPi);
        break;
    case Motion::Watch:
        p.frame = 4;
        p.joints.headNod = .35 * std::sin(t / 950.0);
        break;
    case Motion::Stalk:
        p.frame = 11;
        p.x = 0;
        break;
    case Motion::Pounce:
        if (elapsed < 240) {
            p.frame = 11;
            p.scaleY = .96;
        } else if (elapsed < 840) {
            const double jump = (t - 240) / 600;
            p.frame = 1;
            p.lift = 0;
            p.angle = (jump - .5) * 12 * direction;
            p.scaleX = 1.04;
        } else {
            p.frame = 14;
            p.scaleY = 1 - .055 * std::max(0.0, 1 - (t - 840) / 200.0);
        }
        break;
    case Motion::Jump:
        // A single in-place hop: crouch, rise, tuck the paws, land, then settle.
        // The new airborne art stays distinct from the forward-pouncing frames.
        p.lift = 0;
        if (elapsed < 220) {
            const double crouch = std::sin(t / 220.0 * kPi);
            p.frame = 11;
            p.scaleX = 1 + .025 * crouch;
            p.scaleY = 1 - .1 * crouch;
        } else if (elapsed < 1060) {
            const double flight = (t - 220) / 840;
            p.frame = flight < .5 ? 16 : 17;
            p.lift = 0;
            p.scaleY = 1 + .025 * std::sin(flight * 2 * kPi);
            p.angle = direction * 2 * std::sin(flight * 2 * kPi);
        } else {
            const double landing = std::sin(std::min((t - 1060) / 260, 1.0) * kPi);
            p.frame = 14;
            p.scaleX = 1 + .03 * landing;
            p.scaleY = 1 - .09 * landing;
        }
        break;
    case Motion::Zoomies:
        p.frame = static_cast<int>(elapsed / 55 % 4);
        crossfade(p.frame, (p.frame + 1) % 4, static_cast<double>(elapsed % 55) / 55);
        p.angle = 0;
        p.scaleX = 1.025;
        break;
    case Motion::TailChase:
        p.frame = 15;
        p.flip = (elapsed / 370 % 2 == 0) != (direction < 0);
        p.x = 0;
        p.angle = 0;
        p.scaleX = .96 + .04 * std::abs(std::cos(t / 240.0));
        break;
    case Motion::Punch:
        if (elapsed < kFirstPunchMs) {
            p.frame = 12;
            p.scaleY = 1 - .035 * std::sin(t / kFirstPunchMs * kPi);
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
        // Sink below the platform edge so mostly the head peeks out, then rise again.
        p.frame = 14;
        const double phase = std::clamp(t / kPeekDurationMs, 0.0, 1.0);
        const double dip = phase < .3 ? SmoothStep(phase / .3) : phase > .72 ? SmoothStep((1.0 - phase) / .28) : 1.0;
        // Enter from the standing baseline and return there. Starting/ending
        // 54 pixels below it teleported the body when the motion changed.
        p.lift = -38.0 * dip;
        p.angle = direction * std::sin(t / 430.0) * 3.2 * dip;
        break;
    }
    case Motion::Scratch:
        p.frame = 9;
        p.joints.headNod = .25 * std::sin(t / 180.0 * kPi);
        p.joints.rightEar = .2 * (.5 + .5 * std::sin(t / 180.0 * kPi));
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
        p.lift = -8.0 * SmoothStep(t / 350.0);
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
        // Anatomical side/back poses keep the supported spine down and the
        // face upright. A screen-plane spin cannot represent a belly roll.
        p.joints = {};
        p.bellyRig = true;
        // Register each pose change in a brief smooth movement. A half-second
        // hold at partial opacity exposed two separate sets of paws/tails.
        // The sole head bone uses this exact weight and never detaches.
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
        p.joints.headNod = settle * .15 * std::sin(t / 800.0);
        break;
    }
    }
    return p;
}

// Sleep variants are chosen once by the controller, never by rendering time.
// Every variant retains a physical ground baseline and one registered face.
inline Pose EvaluateSleep(unsigned variant, std::uint64_t elapsed, std::uint64_t ticks, int direction, bool perched = false) {
    Pose p=Evaluate(Motion::Sleep,elapsed,ticks,direction);
    if(variant%6==1) { p.frame=24; p.bellyRig=true; p.face.blink=1; }
    else if(variant%6==2) {p.frame=4;p.joints.crouch=1;p.face.blink=1;}
    else if(variant%6==3) {p.frame=25;p.bellyRig=true;p.face.blink=1;}
    else if(variant%6==4) p.frame=27;
    else if(variant%6==5) p.frame=28;
    if(perched){p.frame=4;p.bellyRig=false;p.joints.crouch=.35;p.joints.headNod=.45+.25*std::sin(elapsed/2300.0);p.face.blink=.9+.1*std::sin(elapsed/3100.0);}
    return p;
}

// Compatible opt-in path for a physics-backed caller. The original evaluator
// remains available for previews and its time-based jump lift can be replaced
// by the caller's physical height without changing any sprite identities.
inline Pose Evaluate(Motion motion, std::uint64_t elapsed, std::uint64_t ticks,
                     int direction, const Locomotion& locomotion) {
    Pose p = Evaluate(motion, elapsed, ticks, direction);
    if (motion != Motion::Walk && motion != Motion::Zoomies && motion != Motion::Stalk) return p;

    const double phase = locomotion.Phase();
    const double referenceSpeed = motion == Motion::Zoomies ? 150.0 : motion == Motion::Stalk ? 12.0 : 22.0;
    const double activity = SmoothStep(locomotion.Speed() / referenceSpeed);
    const double swing = std::sin(phase * 2 * kPi);
    const double bob = swing * swing; // Two smooth weight transfers per stride.
    p.lift = 0;
    p.x = 0;
    p.scaleX = p.scaleY = 1;
    p.angle = 0;
    p.blendFrame = -1;
    p.blend = 0;
    if (motion == Motion::Stalk) {
        p.frame = 11;
        p.x = swing * .6 * activity;
        p.angle = swing * .5 * activity;
        return p;
    }

    const double framePhase = phase * 4;
    p.frame = static_cast<int>(framePhase);
    const double fraction = framePhase - p.frame;
    // Hold the clear paw pose for most of the step, then blend only adjacent
    // poses briefly. Keeping a double image for the whole stride adds blur.
    if (fraction > .6) {
        p.blendFrame = (p.frame + 1) % 4;
        p.blend = SmoothStep((fraction - .6) / .4);
    }
    if (motion == Motion::Zoomies) {
        p.scaleX += .025 * activity;
        p.scaleY -= .018 * bob * activity;
        p.angle = direction * 2.4 * swing * activity;
    } else {
        p.scaleY -= .009 * bob * activity;
        p.angle = direction * .6 * swing * activity;
    }
    return p;
}

} // namespace cat::animation
