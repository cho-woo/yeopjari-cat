#include "../src/cat_animation.hpp"

#include <array>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {
using cat::animation::Motion;
using cat::animation::Pose;
using cat::animation::Evaluate;
int checks = 0;

#define CHECK(condition) do { ++checks; if (!(condition)) throw std::runtime_error( \
    std::string(__FILE__) + ":" + std::to_string(__LINE__) + " failed: " #condition); } while (false)

constexpr std::array<Motion, 32> motions = {
    Motion::Idle, Motion::Walk, Motion::Sleep, Motion::Eat, Motion::Groom,
    Motion::Stretch, Motion::Watch, Motion::Stalk, Motion::Pounce,
    Motion::Zoomies, Motion::TailChase, Motion::Punch, Motion::Jump, Motion::Treat,
    Motion::EarFlick, Motion::TailRaise, Motion::BellyRoll, Motion::Loaf,
    Motion::Knead, Motion::Peek, Motion::Scratch, Motion::Sneeze, Motion::ToyPaw, Motion::BoxSit, Motion::Sniff, Motion::LookDown, Motion::Balance, Motion::Startled,
    Motion::Yawn, Motion::SleepTalk, Motion::Hang, Motion::Pride
};
static_assert(motions.size() == static_cast<std::size_t>(Motion::Pride) + 1,
              "Every supported motion needs animation contract coverage");

std::array<double, 7> JointValues(const cat::deform::Joints& joints) {
    return {joints.leftEar, joints.rightEar, joints.headNod, joints.frontLeft,
            joints.frontRight, joints.tailRaise, joints.breath};
}

void FiniteAndContained() {
    // A conservative adult sprite envelope. This checks animation transforms,
    // not the alpha bounds of the generated artwork or actual Windows drawing.
    constexpr double halfSpriteWidth = 75, spriteHeight = 110;
    constexpr double viewWidth = 260, viewHeight = 210, footBaseline = viewHeight - 1;
    std::vector<std::uint64_t> samples;
    for (std::uint64_t t = 0; t <= 6000; t += 23) samples.push_back(t);
    samples.insert(samples.end(), {199,200,219,220,239,240,299,300,459,460,599,600,639,640,789,790,
        839,840,1059,1060,1319,1320,1449,1450,2900,4199,4200,86400000,3653ULL*86400000,
        std::numeric_limits<std::uint64_t>::max()});
    bool valid = true, contained = true;
    for (const auto motion : motions) for (const auto elapsed : samples) for (const int direction : {-1,1}) {
        const auto p = Evaluate(motion, elapsed, elapsed, direction);
        valid = valid && p.frame >= 0 && p.frame < cat::animation::kSpriteCount
            && std::isfinite(p.x) && std::isfinite(p.lift)
            && std::isfinite(p.scaleX) && std::isfinite(p.scaleY)
            && std::isfinite(p.angle) && p.scaleX >= .9 && p.scaleX <= 1.1
            && p.scaleY >= .75 && p.scaleY <= 1.05 && std::abs(p.angle) <= 6
            && p.blendFrame >= -1 && p.blendFrame < cat::animation::kSpriteCount
            && std::isfinite(p.blend) && p.blend >= 0 && p.blend <= 1
            && (p.blendFrame >= 0 || p.blend == 0) && cat::deform::Finite(p.joints);
        // Peek and the box intentionally hide part of the body below the edge.
        // Every other action leaves root height to the physical controller.
        if (motion == Motion::Peek) valid = valid && p.lift >= -38 && p.lift <= 0;
        else if (motion == Motion::BoxSit) valid = valid && p.lift >= -8 && p.lift <= 0;
        else valid = valid && p.lift == 0;
        for (double joint : JointValues(p.joints)) valid = valid && joint >= -1 && joint <= 1;
        valid = valid && p.joints.leftEar >= 0 && p.joints.rightEar >= 0
            && p.joints.frontLeft >= 0 && p.joints.frontRight >= 0 && p.joints.tailRaise >= 0;
        const double radians = p.angle * cat::animation::kPi / 180.0;
        // Mirror the native renderer's toe clearance, which keeps a rotated
        // sprite above the work-area edge rather than below the taskbar.
        const double footLift = std::abs(std::sin(radians)) * halfSpriteWidth * p.scaleX;
        for (double x : {-halfSpriteWidth, halfSpriteWidth}) for (double y : {-spriteHeight, 0.0}) {
            const double sx = x * p.scaleX, sy = y * p.scaleY;
            const double px = viewWidth / 2 + p.x + sx*std::cos(radians) - sy*std::sin(radians);
            const double py = footBaseline - p.lift - footLift + sx*std::sin(radians) + sy*std::cos(radians);
            const double hiddenDepth = motion == Motion::Peek ? 38 : motion == Motion::BoxSit ? 8 : 0;
            contained = contained && px >= 0 && px <= viewWidth && py >= 0 && py <= viewHeight + hiddenDepth;
        }
    }
    CHECK(valid);
    CHECK(contained);
}

void PunchVisualAndEventsAgree() {
    using namespace cat::animation;
    CHECK(PunchHits(0) == 0);
    CHECK(PunchHits(kFirstPunchMs - 1) == 0);
    CHECK(PunchHits(kFirstPunchMs) == 1);
    CHECK(PunchHits(kSecondPunchMs - 1) == 1);
    CHECK(PunchHits(kSecondPunchMs) == 2);
    CHECK(PunchHits(kPunchDurationMs) == 2);
    CHECK(PunchHits(std::numeric_limits<std::uint64_t>::max()) == 2);

    // An event must appear on a visible paw-contact pose in either direction.
    for (const int direction : {-1,1}) for (const auto event : {kFirstPunchMs,kSecondPunchMs}) {
        CHECK(Evaluate(Motion::Punch,event,event,direction).frame == 13);
        CHECK(Evaluate(Motion::Punch,event-1,event-1,direction).frame != 13);
    }
    CHECK(Evaluate(Motion::Punch,kPunchDurationMs-1,kPunchDurationMs-1,1).frame == 14);

    // A 25 fps timer can skip the exact event time, but detects each hit once.
    for (std::uint64_t phase : {0,7,19,39}) {
        int previous = 0, events = 0;
        for (std::uint64_t t = phase; t < kPunchDurationMs; t += 40) {
            const int reached = PunchHits(t);
            CHECK(reached >= previous);
            if (reached > previous) {
                previous = reached;
                CHECK(PunchContact(t));
                if (PunchContact(t)) ++events;
                CHECK(Evaluate(Motion::Punch,t,t,1).frame == 13);
            }
        }
        CHECK(events == 2);
    }
}

void DelayedPunchTicksNeverReplay() {
    using namespace cat::animation;
    // Model the event consumer contract while sampling at nonuniform times, as
    // happens while a native context menu or a scheduling stall delays ticks.
    const auto simulate = [](const std::vector<std::uint64_t>& ticks) {
        int consumed = 0, visibleEffects = 0;
        for (const auto t : ticks) {
            const auto due = PunchHits(t);
            if (due > consumed) {
                consumed = due;
                if (PunchContact(t)) {
                    ++visibleEffects;
                    CHECK(Evaluate(Motion::Punch,t,t,1).frame == 13);
                }
            }
            CHECK(!PunchContact(t) || Evaluate(Motion::Punch,t,t,1).frame == 13);
        }
        CHECK(consumed == 2);
        return visibleEffects;
    };
    CHECK(simulate({200,1100,1140,1600,2500,2900}) == 0);
    CHECK(simulate({200,500,620,1100,1600,2900}) == 1);
    CHECK(simulate({200,320,1100,1140,2500,2900}) == 1);
    CHECK(simulate({200,650,900,1100,2900}) == 1);
    CHECK(!PunchContact(460));
    CHECK(!PunchContact(790));
    CHECK(!PunchContact(1100));
    CHECK(!PunchContact(std::numeric_limits<std::uint64_t>::max()));
}

void PounceTravelAndLanding() {
    using namespace cat::animation;
    CHECK(!Travels(Motion::Groom));
    CHECK(!Travels(Motion::Punch));
    CHECK(!Travels(Motion::TailChase));
    CHECK(Travels(Motion::Pounce));
    int movingMilliseconds = 0, travelIntervals = 0;
    bool wasMoving = false;
    for (std::uint64_t t = 0; t < 2400; ++t) {
        const double factor = TravelFactor(Motion::Pounce,t);
        const auto p = Evaluate(Motion::Pounce,t,0,1);
        CHECK(factor == 0 || factor == 1);
        if (factor > 0 && !wasMoving) ++travelIntervals;
        wasMoving = factor > 0;
        movingMilliseconds += static_cast<int>(factor);
        // Box2D supplies the only root-height arc, including delayed landings.
        CHECK(p.lift == 0);
        CHECK(p.frame == (t < 240 ? 4 : t < 840 ? 16 : 4));
        CHECK(p.scaleX==1 && p.scaleY==1);
        if (t >= 840) CHECK(factor == 0);
    }
    CHECK(travelIntervals == 1);
    CHECK(movingMilliseconds == 600);
    CHECK(TravelFactor(Motion::Pounce,239) == 0);
    CHECK(TravelFactor(Motion::Pounce,240) == 1);
    CHECK(TravelFactor(Motion::Pounce,839) == 1);
    CHECK(TravelFactor(Motion::Pounce,840) == 0);
    CHECK(TravelFactor(Motion::Pounce,std::numeric_limits<std::uint64_t>::max()) == 0);
}

void StationaryJumpArtDoesNotAddHeight() {
    using namespace cat::animation;
    CHECK(kJumpDurationMs == 1450);
    CHECK(!Travels(Motion::Jump));
    bool stationary = true, expectedFrames = true, physicalHeightOnly = true, settled = true;
    int airborneIntervals = 0;
    bool wasAirborne = false;
    for (std::uint64_t t = 0; t <= kJumpDurationMs + 500; ++t) {
        const auto p = Evaluate(Motion::Jump,t,t,1);
        stationary = stationary && p.x == 0 && TravelFactor(Motion::Jump,t) == 0;
        expectedFrames = expectedFrames && (p.frame == 4 || p.frame == 16);
        physicalHeightOnly = physicalHeightOnly && p.lift == 0;
        const bool airborne = p.frame == 16;
        if (airborne && !wasAirborne) ++airborneIntervals;
        wasAirborne = airborne;
        if (t >= kJumpDurationMs) settled = settled && p.frame == 4 && p.lift == 0
            && std::abs(p.scaleX-1) < 1e-12 && std::abs(p.scaleY-1) < 1e-12;
    }
    CHECK(stationary);
    CHECK(expectedFrames);
    CHECK(physicalHeightOnly);
    CHECK(settled);
    CHECK(airborneIntervals == 1);
    CHECK(Evaluate(Motion::Jump,219,219,1).frame == 4);
    CHECK(Evaluate(Motion::Jump,220,220,1).frame == 16);
    CHECK(Evaluate(Motion::Jump,639,639,1).frame == 16);
    CHECK(Evaluate(Motion::Jump,640,640,1).frame == 16);
    CHECK(Evaluate(Motion::Jump,1059,1059,1).frame == 16);
    CHECK(Evaluate(Motion::Jump,1060,1060,1).frame == 4);
    CHECK(Evaluate(Motion::Jump,1190,1190,1).joints.crouch > .65);
    const auto late = Evaluate(Motion::Jump,std::numeric_limits<std::uint64_t>::max(),0,1);
    CHECK(late.frame == 4 && late.lift == 0);
    CHECK(TravelFactor(Motion::Jump,std::numeric_limits<std::uint64_t>::max()) == 0);
}

void TreatLickingRemainsSeparateAndGrounded() {
    using namespace cat::animation;
    CHECK(kTreatDurationMs == 4200);
    CHECK(!Travels(Motion::Treat));
    bool stationary = true, distinct = true, subtle = true;
    double minimumTongue=1,maximumTongue=0;
    for (std::uint64_t t = 0; t < kTreatDurationMs; t += 20) {
        const auto treat = Evaluate(Motion::Treat,t,t,1);
        const auto food = Evaluate(Motion::Eat,t,t,1);
        stationary = stationary && treat.x == 0 && TravelFactor(Motion::Treat,t) == 0;
        distinct = distinct && treat.frame == 18 && food.frame == 7 && treat.blendFrame == -1;
        subtle = subtle && treat.lift >= 0 && treat.lift < 1
            && treat.scaleY > .98 && treat.scaleY < 1.02;
        minimumTongue=std::min(minimumTongue,treat.joints.frontLeft);
        maximumTongue=std::max(maximumTongue,treat.joints.frontLeft);
    }
    CHECK(stationary);
    CHECK(distinct);
    CHECK(subtle);
    CHECK(minimumTongue<.01 && maximumTongue>.99);
    CHECK(Evaluate(Motion::Treat,199,199,1).frame == 18);
    CHECK(Evaluate(Motion::Treat,200,200,1).frame == 18);
    CHECK(Evaluate(Motion::Treat,400,400,1).frame == 18);
    CHECK(TravelFactor(Motion::Treat,std::numeric_limits<std::uint64_t>::max()) == 0);
}

std::array<double,cat::animation::kSpriteCount> FrameWeights(const Pose& pose) {
    std::array<double,cat::animation::kSpriteCount> weights{};
    weights[pose.frame] += pose.blendFrame < 0 ? 1 : 1-pose.blend;
    if (pose.blendFrame >= 0) weights[pose.blendFrame] += pose.blend;
    return weights;
}

double WeightDistance(const Pose& first, const Pose& second) {
    const auto a = FrameWeights(first), b = FrameWeights(second);
    double difference = 0;
    for (std::size_t i = 0; i < a.size(); ++i) difference += std::abs(a[i]-b[i]);
    return difference;
}

void BlendedLoopsAndGestureContinuity() {
    // Frame numbers may wrap, but the represented image weights must not jump.
    using namespace cat::animation;
    for (const auto motion : {Motion::Walk,Motion::Zoomies,Motion::Treat,Motion::EarFlick,Motion::TailRaise}) {
        bool continuous = true, normalized = true;
        for (std::uint64_t t = 1; t <= 6000; ++t) {
            const auto previous = Evaluate(motion,t-1,t-1,1), current = Evaluate(motion,t,t,1);
            continuous = continuous && WeightDistance(previous,current) < .07;
            const auto weights = FrameWeights(current);
            double total = 0;
            for (double weight : weights) { total += weight; normalized = normalized && weight >= 0; }
            normalized = normalized && std::abs(total-1) < 1e-12;
        }
        CHECK(continuous);
        CHECK(normalized);
    }
    // Both walking cycles wrap smoothly from the last sprite into the first.
    for (const auto& loop : {std::pair{Motion::Walk,145ULL},std::pair{Motion::Zoomies,55ULL}}) {
        for (std::uint64_t boundary = loop.second; boundary <= loop.second*12; boundary += loop.second) {
            CHECK(WeightDistance(Evaluate(loop.first,boundary-1,boundary-1,1),
                                 Evaluate(loop.first,boundary,boundary,1)) < .005);
        }
    }
    CHECK(Evaluate(Motion::Walk,72,72,1).blend > .45);
    CHECK(Evaluate(Motion::Walk,72,72,1).blend < .55);
    CHECK(Evaluate(Motion::Treat,149,149,1).blend == 0);
    CHECK(Evaluate(Motion::Treat,175,175,1).blend == 0);
    CHECK(WeightDistance(Evaluate(Motion::Treat,199,199,1),Evaluate(Motion::Treat,200,200,1)) < .005);
}

void EarAndTailRemainStationaryWithHolds() {
    using namespace cat::animation;
    CHECK(kEarFlickDurationMs == 2200);
    CHECK(kTailRaiseDurationMs == 3400);
    CHECK(!Travels(Motion::EarFlick));
    CHECK(!Travels(Motion::TailRaise));
    for (std::uint64_t t = 0; t < 4000; t += 10) {
        const auto ears = Evaluate(Motion::EarFlick,t,t,1), tail = Evaluate(Motion::TailRaise,t,t,1);
        for (const auto& pose : {ears, tail}) {
            // Alternate whole-cat images caused a 12% body-width morph even
            // with unit scale. Keeping the exact base image is the contract.
            CHECK(pose.frame == 4 && pose.blendFrame == -1 && pose.blend == 0);
            CHECK(pose.x == 0 && pose.lift == 0 && pose.angle == 0 && !pose.centerPivot);
            CHECK(pose.scaleX == 1 && pose.scaleY == 1);
            CHECK(pose.joints.headNod == 0 && pose.joints.breath == 0);
            CHECK(pose.joints.frontLeft == 0 && pose.joints.frontRight == 0);
        }
        CHECK(ears.joints.tailRaise == 0);
        CHECK(tail.joints.leftEar == 0 && tail.joints.rightEar == 0);
        CHECK(ears.joints.leftEar == 0 || ears.joints.rightEar == 0);
        CHECK(TravelFactor(Motion::EarFlick,t) == 0 && TravelFactor(Motion::TailRaise,t) == 0);
    }
    CHECK(Evaluate(Motion::EarFlick,360,360,1).joints.leftEar == 1);
    CHECK(Evaluate(Motion::EarFlick,360,360,1).joints.rightEar == 0);
    CHECK(cat::deform::Zero(Evaluate(Motion::EarFlick,800,800,1).joints));
    CHECK(Evaluate(Motion::EarFlick,1170,1170,1).joints.rightEar == 1);
    CHECK(Evaluate(Motion::EarFlick,1170,1170,1).joints.leftEar == 0);
    CHECK(Evaluate(Motion::TailRaise,1200,1200,1).joints.tailRaise == 1);
    CHECK(Evaluate(Motion::TailRaise,2000,2000,1).joints.tailRaise == 1);
    for (const auto& end : {std::pair{Motion::EarFlick,kEarFlickDurationMs},std::pair{Motion::TailRaise,kTailRaiseDurationMs}}) {
        for (const auto time : {end.second, std::numeric_limits<std::uint64_t>::max()}) {
            const auto late = Evaluate(end.first,time,time,1);
            CHECK(late.frame == 4 && late.blendFrame == -1);
            CHECK(cat::deform::Zero(late.joints));
        }
    }
}

void GroundedActionsKeepBodyScaleAndSupportedLandings() {
    using namespace cat::animation;
    for(int m=0;m<=static_cast<int>(Motion::Pride);++m)for(unsigned t=0;t<=8000;t+=13) {
        const auto p=Evaluate(static_cast<Motion>(m),t,t,-1);
        CHECK(p.scaleX==1 && p.scaleY==1 && p.lift==0);
        CHECK(p.flip); // Action timing never flips facing in place.
    }
    for(double strength:{0.0,.35,1.0})for(unsigned t=0;t<=400;++t) {
        const auto p=LandingPose(t,strength,-1);
        CHECK(p.frame==4 && p.blendFrame==-1 && p.flip && p.groundContact);
        CHECK(p.x==0 && p.lift==0 && p.angle==0 && p.scaleX==1 && p.scaleY==1);
        const cat::deform::Point a{.40,.47},b{.70,.45};
        const auto qa=cat::deform::SittingPoint(a,p.joints),qb=cat::deform::SittingPoint(b,p.joints);
        CHECK(std::abs(std::hypot(qa.x-qb.x,qa.y-qb.y)-std::hypot(a.x-b.x,a.y-b.y))<1e-12);
        CHECK(cat::deform::SittingPoint({.5,1},p.joints).y==1);
        CHECK(std::abs(p.joints.crouch-LandingPose(t+1,strength,-1).joints.crouch)<.008);
        if(t>=280)CHECK(cat::deform::Zero(p.joints));
    }
    CHECK(cat::deform::Zero(LandingPose(140,std::numeric_limits<double>::quiet_NaN(),1).joints));
    for(unsigned t=0;t<3000;t+=7) {
        const auto grooming=Evaluate(Motion::Groom,t,t,1),scratch=Evaluate(Motion::Scratch,t,t,1);
        CHECK(grooming.frame==9 && grooming.blendFrame==-1);
        CHECK(scratch.frame==9 && scratch.blendFrame==-1);
    }
    CHECK(Evaluate(Motion::Groom,440,440,1).joints.frontRight==1);
    CHECK(Evaluate(Motion::Scratch,180,180,1).joints.frontRight==1);
}

void BellyUsesSupportedAnatomyInsteadOfScreenRotation() {
    using namespace cat::animation;
    CHECK(kSpriteCount >= 26);
    int sideSamples = 0, backHoldMilliseconds = 0;
    auto previous = Evaluate(Motion::BellyRoll,0,0,1);
    CHECK(previous.frame == 4 && previous.blend == 0 && cat::deform::Zero(previous.joints));
    for (std::uint64_t t = 0; t <= kBellyRollDurationMs + 500; ++t) {
        const auto pose = Evaluate(Motion::BellyRoll,t,t,1);
        CHECK(pose.frame == 4 || pose.frame == 24 || pose.frame == 25);
        CHECK(pose.blendFrame == -1 || pose.blendFrame == 4 || pose.blendFrame == 24 || pose.blendFrame == 25);
        // A supported side/back silhouette provides the anatomy. Rotation or
        // rescaling of a sleeping body is not an acceptable substitute.
        CHECK(pose.angle == 0 && pose.x == 0 && pose.lift == 0 && !pose.centerPivot);
        CHECK(pose.scaleX == 1 && pose.scaleY == 1);
        const auto weights = FrameWeights(pose);
        sideSamples += weights[24] > .9;
        backHoldMilliseconds += weights[25] == 1;
        // L1 counts both fading layers. The registered 180ms turn changes
        // each by at most .00834/ms; reject a cut while allowing that motion.
        CHECK(WeightDistance(previous,pose) < .018);
        if (t >= kBellyRollDurationMs) {
            CHECK(pose.frame == 4 && pose.blendFrame == -1);
            CHECK(cat::deform::Zero(pose.joints));
        }
        previous = pose;
    }
    CHECK(sideSamples > 100);
    CHECK(backHoldMilliseconds >= 1500);
    CHECK(Evaluate(Motion::BellyRoll,2200,2200,1).frame == 25);
    const auto late = Evaluate(Motion::BellyRoll,std::numeric_limits<std::uint64_t>::max(),0,1);
    CHECK(late.frame == 4 && late.blendFrame == -1 && cat::deform::Zero(late.joints));
}

void LocalJointControlsStayContinuousAndMirroredOnce() {
    using namespace cat::animation;
    for (const auto motion : motions) {
        auto previous = Evaluate(motion,0,0,1);
        for (std::uint64_t t = 1; t <= 8000; ++t) {
            const auto pose = Evaluate(motion,t,t,1), mirrored = Evaluate(motion,t,t,-1);
            const auto before = JointValues(previous.joints), after = JointValues(pose.joints);
            CHECK(after == JointValues(mirrored.joints));
            for (std::size_t joint = 0; joint < after.size(); ++joint) {
                CHECK(std::isfinite(after[joint]) && std::abs(after[joint]) <= 1);
                // Controls belong to the current anatomical rig. Switching
                // from planted crouch to real airborne paws changes rigs;
                // comparing those unrelated coordinates is not continuity.
                if(pose.frame==previous.frame)
                    CHECK(std::abs(after[joint] - before[joint]) < .02);
            }
            previous = pose;
        }
    }
    // Kneading alternates attached paws; it must not flicker between different
    // grooming heads. At least one forepaw remains at rest at every sample.
    bool leftMoved = false, rightMoved = false;
    for (std::uint64_t t = 0; t < 1800; t += 10) {
        const auto pose = Evaluate(Motion::Knead,t,t,1);
        CHECK(pose.frame == 4 && pose.blendFrame == -1);
        CHECK(pose.joints.frontLeft == 0 || pose.joints.frontRight == 0);
        leftMoved = leftMoved || pose.joints.frontLeft > .9;
        rightMoved = rightMoved || pose.joints.frontRight > .9;
    }
    CHECK(leftMoved && rightMoved);
}

void FacingAndMirroring() {
    bool sameIdentity = true, oppositeFacing = true, directionalImpulses = true;
    for (const auto motion : motions) for (std::uint64_t t = 0; t < 4000; t += 37) {
        const auto right = Evaluate(motion,t,t,1), left = Evaluate(motion,t,t,-1);
        sameIdentity = sameIdentity && right.frame == left.frame && right.scaleX == left.scaleX
            && right.scaleY == left.scaleY && right.lift == left.lift
            && right.blendFrame == left.blendFrame && right.blend == left.blend;
        oppositeFacing = oppositeFacing && right.flip != left.flip;
        if (motion == Motion::Punch || motion == Motion::Pounce || motion == Motion::Jump
                || motion == Motion::EarFlick || motion == Motion::TailRaise) {
            directionalImpulses = directionalImpulses && std::abs(right.x+left.x) < 1e-12
                && std::abs(right.angle+left.angle) < 1e-12;
        }
    }
    CHECK(sameIdentity);
    CHECK(oppositeFacing);
    CHECK(directionalImpulses);
}

void DistanceDrivenGaitSurvivesSpeedAndActionChanges() {
    using namespace cat::animation;
    Locomotion frequent, sparse;
    for (int i = 0; i < 120; ++i) CHECK(frequent.Advance(Motion::Walk, 22.0 / 60, 1.0 / 60));
    for (int i = 0; i < 60; ++i) CHECK(sparse.Advance(Motion::Walk, 22.0 / 30, 1.0 / 30));
    CHECK(std::abs(frequent.Phase() - sparse.Phase()) < 1e-12);
    CHECK(std::abs(frequent.Speed() - sparse.Speed()) < 1e-12);

    // A new action's elapsed clock must not reset a partially completed stride.
    const auto before = Evaluate(Motion::Walk, 9000, 700, 1, frequent);
    const auto restarted = Evaluate(Motion::Walk, 0, 700, 1, frequent);
    CHECK(WeightDistance(before, restarted) == 0);
    CHECK(before.walkingRig && restarted.walkingRig && before.walkPhase==restarted.walkPhase);
    CHECK(before.lift == restarted.lift && before.angle == restarted.angle);
    const auto heldPhase = frequent.Phase();
    CHECK(frequent.Advance(Motion::Idle, 0, .1));
    CHECK(frequent.Phase() == heldPhase);
    CHECK(frequent.Advance(Motion::Walk, StrideLength(Motion::Walk)*.1, .1));
    CHECK(std::abs(frequent.Phase() - std::fmod(heldPhase + .1, 1.0)) < 1e-12);

    // Actual distance, including slower feeding/weight states, sets cadence.
    Locomotion slow, fast, leftward;
    CHECK(slow.Advance(Motion::Walk, 1.4, .1));
    CHECK(fast.Advance(Motion::Walk, 2.8, .1));
    CHECK(leftward.Advance(Motion::Walk, -1.4, .1));
    CHECK(std::abs(fast.Phase() - 2 * slow.Phase()) < 1e-12);
    CHECK(leftward.Phase() == slow.Phase());
    const auto right = Evaluate(Motion::Walk, 0, 700, 1, slow);
    const auto left = Evaluate(Motion::Walk, 0, 700, -1, leftward);
    CHECK(WeightDistance(right, left) == 0 && right.flip != left.flip);
    CHECK(right.walkingRig && left.walkingRig && right.walkPhase==left.walkPhase);
    CHECK(std::abs(right.angle + left.angle) < 1e-12);
}

void BlockedOrStoppedGaitsDoNotKeepStepping() {
    using namespace cat::animation;
    Locomotion gait;
    CHECK(gait.Advance(Motion::Walk, 2, .05));
    const auto phase = gait.Phase();
    for (int i = 0; i < 100; ++i) CHECK(gait.Advance(Motion::Walk, 0, .02));
    CHECK(gait.Phase() == phase);
    CHECK(gait.Speed() < 1e-8);
    const auto stopped = Evaluate(Motion::Walk, 3000, 0, 1, gait);
    CHECK(stopped.lift == 0);
    CHECK(std::abs(stopped.angle) < 1e-12 && std::abs(stopped.scaleY - 1) < 1e-12);
    const auto late = Evaluate(Motion::Walk, 100000, 0, 1, gait);
    CHECK(WeightDistance(stopped, late) == 0);
    CHECK(stopped.walkingRig && late.walkingRig && stopped.walkPhase==late.walkPhase);

    // A reversal changes facing, never rewinds the gait or adds a phantom step.
    CHECK(gait.Advance(Motion::Walk, -StrideLength(Motion::Walk)*.1, .1));
    CHECK(std::abs(gait.Phase() - std::fmod(phase + .1, 1.0)) < 1e-12);
    gait.Reset();
    CHECK(gait.Phase() == 0 && gait.Speed() == 0);
}

void DistanceDrivenFrameTransitionsRemainContinuous() {
    using namespace cat::animation;
    CHECK(StrideLength(Motion::Walk)==24 && StrideLength(Motion::Stalk)==20 && StrideLength(Motion::Zoomies)==32);
    for (const auto motion : {Motion::Walk, Motion::Zoomies, Motion::Stalk}) {
        Locomotion gait;
        const double stride = StrideLength(motion);
        auto previous = Evaluate(motion, 0, 700, 1, gait);
        bool continuous = true, normalized = true;
        for (int i = 1; i <= 2800; ++i) {
            CHECK(gait.Advance(motion, stride / 1400, .002));
            const auto current = Evaluate(motion, 0, 700, 1, gait);
            CHECK(current.walkingRig && current.frame==1 && current.blendFrame==-1);
            CHECK(current.walkPhase==gait.Phase() && current.walkStride==stride);
            CHECK(current.groundContact && current.x==0 && current.lift==0);
            CHECK(current.scaleX==1 && current.scaleY==1 && current.angle==0);
            CHECK(cat::deform::Zero(current.joints));
            continuous = continuous && WeightDistance(previous, current) < .022
                && std::abs(previous.lift - current.lift) < .05
                && std::abs(previous.angle - current.angle) < .05;
            double total = 0;
            for (double weight : FrameWeights(current)) total += weight;
            normalized = normalized && std::abs(total - 1) < 1e-12;
            previous = current;
        }
        CHECK(continuous);
        CHECK(normalized);
    }
    // Every phase uses one registered walking drawing. A separate foot rig
    // carries continuous phase, so body/head pixels never crossfade identities.
    for (const double phase : {.05, .10, .30, .35, .55, .60, .80, .85}) {
        Locomotion gait;
        CHECK(gait.Advance(Motion::Walk, phase * StrideLength(Motion::Walk), .1));
        const auto pose=Evaluate(Motion::Walk, 0, 0, 1, gait);
        CHECK(pose.blendFrame == -1 && pose.walkingRig && pose.frame==1);
        CHECK(std::abs(pose.walkPhase-phase)<1e-12);
    }
}

void GaitDiscontinuityGuardsAndLegacyPoseCompatibility() {
    using namespace cat::animation;
    Locomotion gait;
    CHECK(gait.Advance(Motion::Zoomies, 30, .12)); // Real delayed running tick.
    const double heldPhase = gait.Phase();
    CHECK(gait.Advance(Motion::Zoomies, 0, 0));
    CHECK(gait.Phase() == heldPhase);
    for (const auto& invalid : {std::pair{1000.0, .02}, std::pair{1.0, .5},
            std::pair{1.0, 0.0}, std::pair{1.0, -.01},
            std::pair{std::numeric_limits<double>::infinity(), .02},
            std::pair{1.0, std::numeric_limits<double>::quiet_NaN()}}) {
        CHECK(gait.Advance(Motion::Walk, 1, .02));
        CHECK(!gait.Advance(Motion::Walk, invalid.first, invalid.second));
        CHECK(gait.Phase() == 0 && gait.Speed() == 0);
    }
    CHECK(gait.Advance(Motion::Walk, 2, .1));
    for (const auto motion : motions) {
        if (motion == Motion::Walk || motion == Motion::Zoomies || motion == Motion::Stalk) continue;
        for (std::uint64_t t : {0, 220, 300, 640, 1060, 1450, 3000}) {
            const auto legacy = Evaluate(motion, t, t, -1);
            const auto actual = Evaluate(motion, t, t, -1, gait);
            CHECK(legacy.frame == actual.frame && legacy.blendFrame == actual.blendFrame
                && legacy.blend == actual.blend && legacy.flip == actual.flip
                && legacy.x == actual.x && legacy.lift == actual.lift
                && legacy.scaleX == actual.scaleX && legacy.scaleY == actual.scaleY
                && legacy.angle == actual.angle && JointValues(legacy.joints) == JointValues(actual.joints));
            CHECK(!actual.walkingRig && !legacy.walkingRig);
        }
    }
}
} // namespace

int main() {
    try {
        FiniteAndContained();
        PunchVisualAndEventsAgree();
        DelayedPunchTicksNeverReplay();
        PounceTravelAndLanding();
        StationaryJumpArtDoesNotAddHeight();
        TreatLickingRemainsSeparateAndGrounded();
        BlendedLoopsAndGestureContinuity();
        EarAndTailRemainStationaryWithHolds();
        GroundedActionsKeepBodyScaleAndSupportedLandings();
        BellyUsesSupportedAnatomyInsteadOfScreenRotation();
        LocalJointControlsStayContinuousAndMirroredOnce();
        FacingAndMirroring();
        DistanceDrivenGaitSurvivesSpeedAndActionChanges();
        BlockedOrStoppedGaitsDoNotKeepStepping();
        DistanceDrivenFrameTransitionsRemainContinuous();
        GaitDiscontinuityGuardsAndLegacyPoseCompatibility();
        std::cout << "15 animation groups; " << checks << " checks passed\n";
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
