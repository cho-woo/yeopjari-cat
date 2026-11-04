#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace cat::animation {

enum class Motion { Idle, Walk, Sleep, Eat, Groom, Stretch, Watch, Stalk, Pounce, Zoomies, TailChase, Punch, Jump, Treat, EarFlick, TailRaise, BellyRoll, Loaf, Knead, Peek, Scratch, Sneeze, ToyPaw, BoxSit };
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

// Offsets are logical pixels; the native renderer applies monitor DPI and real-age growth.
struct Pose {
    int frame = 4;
    double x = 0, lift = 0, scaleX = 1, scaleY = 1, angle = 0;
    bool flip = false;
    bool centerPivot = false;
    int blendFrame = -1;
    double blend = 0;
};

inline double SmoothStep(double fraction) {
    const double t = std::clamp(fraction, 0.0, 1.0);
    return t * t * (3 - 2 * t);
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
    p.lift = (std::sin(ticks / 700.0) + 1) * .38;
    p.flip = direction < 0;
    const auto crossfade = [&](int first, int second, double fraction) {
        p.frame = first;
        p.blendFrame = second;
        p.blend = SmoothStep(fraction);
    };
    switch (motion) {
    case Motion::Idle:
        p.frame = ticks % 6100 < 170 ? 5 : 4;
        break;
    case Motion::Walk:
        p.frame = static_cast<int>(elapsed / 145 % 4);
        crossfade(p.frame, (p.frame + 1) % 4, static_cast<double>(elapsed % 145) / 145);
        p.lift += std::abs(std::sin(t / 145.0 * kPi)) * .8;
        break;
    case Motion::Sleep:
        p.frame = 6;
        p.scaleY = 1 + .008 * std::sin(t / 1500.0);
        break;
    case Motion::Eat:
        p.frame = 7;
        p.scaleY = 1 + .012 * std::sin(t / 110.0);
        break;
    case Motion::Treat:
        p.frame = 18 + static_cast<int>(elapsed / 200 % 2);
        if (elapsed % 200 >= 150)
            crossfade(p.frame, p.frame == 18 ? 19 : 18, static_cast<double>(elapsed % 200 - 150) / 50);
        p.lift = .28 + .28 * std::sin(t / 200.0 * kPi);
        p.scaleY = 1 + .009 * std::sin(t / 200.0 * kPi);
        break;
    case Motion::Groom:
        p.frame = elapsed / 440 % 3 < 2 ? 9 : 10;
        p.angle = direction * std::sin(t / 220.0) * 1.7;
        break;
    case Motion::Stretch:
        p.frame = 8;
        p.scaleX = 1 + .035 * std::sin(std::min(t / 1900.0, 1.0) * kPi);
        p.scaleY = 1 - .025 * std::sin(std::min(t / 1900.0, 1.0) * kPi);
        break;
    case Motion::Watch:
        p.frame = elapsed / 1700 % 3 == 1 ? 15 : 14;
        p.angle = direction * std::sin(t / 650.0) * 5;
        break;
    case Motion::Stalk:
        p.frame = 11;
        p.x = std::sin(t / 95.0) * 1.5;
        p.angle = std::sin(t / 95.0) * .8;
        break;
    case Motion::Pounce:
        if (elapsed < 240) {
            p.frame = 11;
            p.scaleY = .96;
        } else if (elapsed < 840) {
            const double jump = (t - 240) / 600;
            p.frame = 1;
            p.lift = std::sin(jump * kPi) * 30;
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
            p.lift = 55 * std::sin(flight * kPi);
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
        p.lift += std::abs(std::sin(t / 80.0)) * 4;
        p.angle = std::sin(t / 130.0) * 3;
        p.scaleX = 1.025;
        break;
    case Motion::TailChase:
        p.frame = 15;
        p.flip = (elapsed / 370 % 2 == 0) != (direction < 0);
        p.x = std::sin(t / 240.0) * 12;
        p.angle = std::sin(t / 150.0) * 8;
        p.scaleX = .86 + .14 * std::abs(std::cos(t / 240.0));
        p.lift += std::abs(std::sin(t / 180.0)) * 4;
        break;
    case Motion::Punch:
        if (elapsed < kFirstPunchMs) {
            p.frame = 12;
            p.scaleY = 1 - .035 * std::sin(t / kFirstPunchMs * kPi);
        } else if (PunchContact(elapsed)) {
            p.frame = 13;
            const double strike = elapsed < 460 ? t - kFirstPunchMs : t - kSecondPunchMs;
            p.x = direction * (2 + 3 * std::max(0.0, 1 - strike / 150.0));
            p.lift += 2.0;
        } else if (elapsed < kSecondPunchMs) {
            p.frame = 12;
        } else {
            p.frame = 14;
            const double recover = std::max(0.0, 1 - (t - 790) / 800.0);
            p.angle = -direction * std::sin((t - 790) / 120.0) * 6 * recover;
        }
        break;
    case Motion::EarFlick:
        // One ear, a quiet pause, then the other. Holds keep the movement from
        // looking like a constant twitch, while transitions remain continuous.
        if (elapsed < 160) p.frame = 4;
        else if (elapsed < 310) crossfade(4, 20, (t - 160) / 150);
        else if (elapsed < 420) p.frame = 20;
        else if (elapsed < 620) crossfade(20, 4, (t - 420) / 200);
        else if (elapsed < 960) p.frame = 4;
        else if (elapsed < 1110) crossfade(4, 21, (t - 960) / 150);
        else if (elapsed < 1230) p.frame = 21;
        else if (elapsed < 1450) crossfade(21, 4, (t - 1230) / 220);
        else p.frame = 4;
        p.angle = direction * .45 * std::sin(t / 300) *
            std::sin(std::min(t / kEarFlickDurationMs, 1.0) * kPi);
        break;
    case Motion::TailRaise:
        // Raise through the half-height pose, hold the tail upright, lower it.
        if (elapsed < 120) p.frame = 4;
        else if (elapsed < 560) crossfade(4, 22, (t - 120) / 440);
        else if (elapsed < 900) crossfade(22, 23, (t - 560) / 340);
        else if (elapsed < 2100) p.frame = 23;
        else if (elapsed < 2460) crossfade(23, 22, (t - 2100) / 360);
        else if (elapsed < 3000) crossfade(22, 4, (t - 2460) / 540);
        else p.frame = 4;
        p.angle = direction * .5 * std::sin(t / 600) *
            std::sin(std::min(t / kTailRaiseDurationMs, 1.0) * kPi);
        break;
    case Motion::Loaf:
        // Compact loaf: tuck the feet under the body and breathe slowly.
        p.frame = 14;
        p.scaleX = 1.06;
        p.scaleY = .78 + .008 * std::sin(t / 950.0);
        p.lift = 0; // The compressed loaf still rests on its paws, above the edge.
        break;
    case Motion::Knead:
        // Alternating paws using the clean grooming poses, with a gentle shoulder bob.
        p.frame = elapsed / 260 % 2 ? 9 : 10;
        p.scaleY = .97 + .025 * std::abs(std::sin(t / 260.0 * kPi));
        p.x = direction * std::sin(t / 260.0 * kPi) * 1.4;
        p.lift += std::abs(std::sin(t / 260.0 * kPi)) * .8;
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
        p.frame = elapsed / 150 % 2 ? 10 : 14;
        p.angle = direction * std::sin(t / 75.0) * 4.0;
        p.x = direction * std::sin(t / 95.0) * 2.2;
        p.scaleY = .98 + .018 * std::abs(std::sin(t / 150.0 * kPi));
        break;
    case Motion::Sneeze: {
        p.frame = 14;
        const double phase = std::clamp(t / 1700.0, 0.0, 1.0);
        const double snap = std::exp(-std::pow((phase - .42) / .12, 2.0));
        p.angle = -direction * 13.0 * snap;
        p.scaleX = 1.0 + .05 * snap;
        p.scaleY = 1.0 - .11 * snap;
        p.lift += 2.5 * snap;
        break;
    }
    case Motion::ToyPaw:
        p.frame = elapsed / 330 % 2 ? 12 : 13;
        p.x = direction * (2.0 + 3.0 * std::max(0.0, std::sin(t / 330.0 * kPi)));
        p.lift += std::max(0.0, std::sin(t / 330.0 * kPi)) * 1.8;
        break;
    case Motion::BoxSit:
        p.frame = 14;
        p.scaleX = .92;
        p.scaleY = .88 + .008 * std::sin(t / 900.0);
        p.lift = -8.0;
        break;
    case Motion::BellyRoll: {
        // A lazy roll onto the back and back again. Reuse the curled/sleeping
        // silhouette, but rotate around its center so the whole body stays above
        // the platform edge instead of pivoting around the paws.
        p.frame = 6;
        p.centerPivot = true;
        const double phase = std::clamp(t / kBellyRollDurationMs, 0.0, 1.0);
        const double roll = phase < .5 ? SmoothStep(phase * 2.0) : SmoothStep((1.0 - phase) * 2.0);
        p.angle = direction * 168.0 * roll;
        p.scaleX = 1.0 + .055 * std::sin(phase * kPi);
        p.scaleY = 0.94 + .06 * std::cos(phase * 2.0 * kPi);
        p.lift = 2.5 * std::sin(phase * kPi);
        // Enter/leave the curled pose rather than popping between sitting and
        // sleeping silhouettes at a motion boundary.
        if (elapsed < 240) crossfade(14, 6, t / 240.0);
        else if (elapsed >= kBellyRollDurationMs) p.frame = 14;
        else if (elapsed > kBellyRollDurationMs - 240)
            crossfade(6, 14, (t - (kBellyRollDurationMs - 240)) / 240.0);
        break;
    }
    }
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
    p.lift = (std::sin(ticks / 700.0) + 1) * .38;
    p.x = 0;
    p.scaleX = p.scaleY = 1;
    p.angle = 0;
    p.blendFrame = -1;
    p.blend = 0;
    if (motion == Motion::Stalk) {
        p.frame = 11;
        p.x = swing * .6 * activity;
        p.angle = swing * .5 * activity;
        p.lift += .25 * bob * activity;
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
        p.lift += 3.2 * bob * activity;
        p.scaleX += .025 * activity;
        p.scaleY -= .018 * bob * activity;
        p.angle = direction * 2.4 * swing * activity;
    } else {
        p.lift += .8 * bob * activity;
        p.scaleY -= .009 * bob * activity;
        p.angle = direction * .6 * swing * activity;
    }
    return p;
}

} // namespace cat::animation
