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
    for (const auto motion : {Motion::Treat,Motion::EarFlick,Motion::TailRaise}) {
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
    // Every locomotion preview also shows one complete source image. Runtime
    // selects these same drawings from actual distance, not this preview clock.
    for(const auto& loop:{std::pair{Motion::Walk,150ULL},std::pair{Motion::Stalk,240ULL},std::pair{Motion::Zoomies,84ULL}})
    for(std::uint64_t elapsed=0;elapsed<=6000;++elapsed) {
        const auto pose=Evaluate(loop.first,elapsed,elapsed,1);
        CHECK(pose.frame==static_cast<int>((elapsed/loop.second)%4));
        CHECK(pose.blendFrame==-1 && pose.blend==0);
        CHECK(cat::deform::Zero(pose.joints));
    }
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
    CHECK(std::abs(frequent.OriginalWalkPhase() - sparse.OriginalWalkPhase()) < 1e-12);
    CHECK(std::abs(frequent.Speed() - sparse.Speed()) < 1e-12);

    // A new action's elapsed clock must not reset a partially completed stride.
    const auto before = Evaluate(Motion::Walk, 9000, 700, 1, frequent);
    const auto restarted = Evaluate(Motion::Walk, 0, 700, 1, frequent);
    CHECK(WeightDistance(before, restarted) == 0);
    CHECK(before.originalWalk && restarted.originalWalk && !before.walkingRig && !restarted.walkingRig);
    CHECK(before.lift == restarted.lift && before.angle == restarted.angle);
    const auto heldPhase = frequent.Phase();
    const auto heldOriginalPhase = frequent.OriginalWalkPhase();
    CHECK(frequent.Advance(Motion::Idle, 0, .1));
    CHECK(frequent.Phase() == heldPhase);
    CHECK(frequent.OriginalWalkPhase() == heldOriginalPhase);
    CHECK(frequent.Advance(Motion::Walk, StrideLength(Motion::Walk)*.1, .1));
    CHECK(std::abs(frequent.Phase() - std::fmod(heldPhase + .1, 1.0)) < 1e-12);
    CHECK(std::abs(frequent.OriginalWalkPhase() - std::fmod(heldOriginalPhase + 2.4/14, 1.0)) < 1e-12);

    // Actual distance, including slower feeding/weight states, sets cadence.
    Locomotion slow, fast, leftward;
    CHECK(slow.Advance(Motion::Walk, 1.4, .1));
    CHECK(fast.Advance(Motion::Walk, 2.8, .1));
    CHECK(leftward.Advance(Motion::Walk, -1.4, .1));
    CHECK(std::abs(fast.Phase() - 2 * slow.Phase()) < 1e-12);
    CHECK(leftward.Phase() == slow.Phase());
    CHECK(std::abs(slow.OriginalWalkPhase()-.1)<1e-12);
    CHECK(std::abs(fast.OriginalWalkPhase()-.2)<1e-12);
    CHECK(leftward.OriginalWalkPhase()==slow.OriginalWalkPhase());
    const auto right = Evaluate(Motion::Walk, 0, 700, 1, slow);
    const auto left = Evaluate(Motion::Walk, 0, 700, -1, leftward);
    CHECK(WeightDistance(right, left) == 0 && right.flip != left.flip);
    CHECK(right.originalWalk && left.originalWalk && !right.walkingRig && !left.walkingRig);
    CHECK(std::abs(right.angle + left.angle) < 1e-12);
}

void BlockedOrStoppedGaitsDoNotKeepStepping() {
    using namespace cat::animation;
    Locomotion gait;
    CHECK(gait.Advance(Motion::Walk, 2, .05));
    const auto phase = gait.Phase();
    const auto originalPhase = gait.OriginalWalkPhase();
    for (int i = 0; i < 100; ++i) CHECK(gait.Advance(Motion::Walk, 0, .02));
    CHECK(gait.Phase() == phase);
    CHECK(gait.OriginalWalkPhase() == originalPhase);
    CHECK(gait.Speed() < 1e-8);
    const auto stopped = Evaluate(Motion::Walk, 3000, 0, 1, gait);
    CHECK(stopped.lift == 0);
    CHECK(std::abs(stopped.angle) < 1e-12 && std::abs(stopped.scaleY - 1) < 1e-12);
    const auto late = Evaluate(Motion::Walk, 100000, 0, 1, gait);
    CHECK(WeightDistance(stopped, late) == 0);
    CHECK(stopped.originalWalk && late.originalWalk && !stopped.walkingRig && !late.walkingRig);

    // A reversal changes facing, never rewinds the gait or adds a phantom step.
    CHECK(gait.Advance(Motion::Walk, -StrideLength(Motion::Walk)*.1, .1));
    CHECK(std::abs(gait.Phase() - std::fmod(phase + .1, 1.0)) < 1e-12);
    CHECK(std::abs(gait.OriginalWalkPhase() - std::fmod(originalPhase + 2.4/14, 1.0)) < 1e-12);
    gait.Reset();
    CHECK(gait.Phase() == 0 && gait.OriginalWalkPhase() == 0 && gait.Speed() == 0);
}

void OriginalWalkingKeepsArchivedFramesAndCadence() {
    using namespace cat::animation;
    CHECK(kOriginalWalkStride==14);
    // Actual v1.0 source shows one complete drawing at a time. Preserve that
    // anatomy through the former v1.9 fade intervals while keeping the live
    // 14px distance cycle and independence from the elapsed action clock.
    for(int frame=0;frame<4;++frame)for(double fraction:{.0,.2,.6,.7,.8,.9,.999}) {
        Locomotion gait;
        const double phase=(frame+fraction)/4;
        double remaining=phase*14;
        while(remaining>1e-12) {
            const double step=std::min(remaining,2.2);
            CHECK(gait.Advance(Motion::Walk,step,step/22));remaining-=step;
        }
        for(int facing:{-1,1}) {
            const auto pose=Evaluate(Motion::Walk,900000,700,facing,gait);
            CHECK(pose.originalWalk && !pose.walkingRig && !pose.jointWalk);
            CHECK(pose.frame==frame && pose.flip==(facing<0));
            CHECK(pose.x==0 && pose.lift==0 && pose.angle==0 && pose.scaleX==1 && pose.scaleY==1);
            CHECK(pose.groundContact && !pose.centerPivot && cat::deform::Zero(pose.joints));
            const auto weights=FrameWeights(pose);
            CHECK(pose.blendFrame==-1 && pose.blend==0);
            CHECK(weights[frame]==1);
            for(int other=0;other<kSpriteCount;++other)if(other!=frame)CHECK(weights[other]==0);
        }
    }
    // Every walking action shares the phase; pauses and blocked travel do not
    // consume a step, and a different gait name does not reset the cycle.
    Locomotion gait;
    for(int i=0;i<7;++i)CHECK(gait.Advance(Motion::Walk,2,.1));
    CHECK(std::min(gait.OriginalWalkPhase(),1-gait.OriginalWalkPhase())<1e-12);
    const auto held=gait.OriginalWalkPhase();
    CHECK(gait.Advance(Motion::Zoomies,4,.1));
    CHECK(gait.Advance(Motion::Idle,0,.1));
    CHECK(gait.Advance(Motion::Walk,0,0));
    CHECK(std::abs(gait.OriginalWalkPhase()-std::fmod(held+4.0/kOriginalWalkStride,1.0))<1e-12);
}

void DistanceDrivenFramesStaySingleAndSequential() {
    using namespace cat::animation;
    CHECK(StrideLength(Motion::Walk)==24 && StrideLength(Motion::Stalk)==20 && StrideLength(Motion::Zoomies)==52);
    for(const auto motion:{Motion::Walk,Motion::Stalk,Motion::Zoomies}) {
        const double speed=motion==Motion::Zoomies?165:motion==Motion::Stalk?12.5:22;
        Locomotion gait;auto previous=Evaluate(motion,0,0,1,gait);int changes=0;
        for(int sample=0;sample<1200;++sample) {
            CHECK(gait.Advance(motion,speed/600,1./600));
            const auto pose=Evaluate(motion,sample*1000,700,1,gait);
            CHECK(pose.originalWalk && !pose.walkingRig && !pose.jointWalk);
            CHECK(pose.frame==previous.frame || pose.frame==(previous.frame+1)%4);
            changes+=pose.frame!=previous.frame;
            CHECK(pose.walkPhase==gait.Phase()); // Newer animals retain their existing fallback metadata.
            CHECK(pose.walkFeetValid && cat::gait::SameFeet(pose.walkFeet,gait.Feet(motion)));
            CHECK(pose.groundContact && pose.x==0 && pose.lift==0 && pose.angle==0);
            CHECK(pose.scaleX==1 && pose.scaleY==1 && cat::deform::Zero(pose.joints));
            const auto weights=FrameWeights(pose);
            for(int frame=0;frame<kSpriteCount;++frame)CHECK(weights[frame]==(frame==pose.frame?1:0));
            CHECK(pose.blendFrame==-1 && pose.blend==0);
            previous=pose;
        }
        CHECK(changes>0 && changes<=static_cast<int>(2*kOriginalWalkMaxFramesPerSecond));
    }
}

void AllWalkingActionsUseIntactSourceDrawings() {
    using namespace cat::animation;
    Locomotion gait;
    for(int sample=0;sample<240;++sample) {
        const auto motion=sample<80?Motion::Walk:sample<160?Motion::Stalk:Motion::Zoomies;
        const double distance=sample<180?22./60:0;
        CHECK(gait.Advance(motion,distance,1./60));
        const auto pose=Evaluate(motion,sample*16,sample*16,1,gait);
        CHECK(pose.originalWalk && !pose.jointWalk && !pose.walkingRig);
        CHECK(pose.frame>=0 && pose.frame<4);
        CHECK(pose.blendFrame==-1 && pose.blend==0);
        CHECK(cat::walk_dynamics::Neutral(pose.jointDynamics));
    }
    for(Motion motion:{Motion::Idle,Motion::Sleep,Motion::Jump})
        CHECK(!Evaluate(motion,0,0,1,gait).originalWalk);
    gait.Reset();CHECK(gait.OriginalWalkPhase()==0);
}

void WalkingActionChangesKeepTheSameDrawing() {
    using namespace cat::animation;
    Locomotion gait;
    for(int sample=0;sample<90;++sample)CHECK(gait.Advance(Motion::Zoomies,165./60,1./60));
    const auto running=Evaluate(Motion::Zoomies,0,0,1,gait);
    // Changing the requested gait keeps the exact complete drawing. There is
    // no cropped running torso or independent standing-up transform to snap.
    for(const auto motion:{Motion::Walk,Motion::Stalk,Motion::Zoomies}) {
        const auto pose=Evaluate(motion,900000,0,1,gait);
        CHECK(pose.frame==running.frame && pose.originalWalk && !pose.jointWalk && !pose.walkingRig);
        CHECK(pose.x==0 && pose.lift==0 && pose.angle==0 && pose.scaleX==1 && pose.scaleY==1);
        CHECK(cat::deform::Zero(pose.joints));
    }
    const double before=gait.OriginalWalkPhase();
    CHECK(gait.Advance(Motion::Stalk,165./60,1./60));
    const double delta=gait.OriginalWalkPhase()>=before?gait.OriginalWalkPhase()-before:gait.OriginalWalkPhase()+1-before;
    CHECK(delta>0 && delta<=kOriginalWalkMaxFramesPerSecond/240+1e-12);
    gait.Reset();CHECK(gait.OriginalWalkPhase()==0);
}

void IntactWalkingCadenceIsBoundedAndShared() {
    using namespace cat::animation;
    CHECK(kOriginalWalkMaxFramesPerSecond==12);
    for(int fps:{30,60,120})for(double speed:{18.,29.,38.,45.,80.,140.,230.})for(int direction:{-1,1}) {
        std::array<double,3> finalPhases{};unsigned gaitIndex=0;
        for(const auto motion:{Motion::Walk,Motion::Stalk,Motion::Zoomies}) {
            Locomotion gait;int previous=0,changes=0;double phases=0;
            for(int sample=0;sample<fps*2;++sample) {
                const double before=gait.OriginalWalkPhase();
                CHECK(gait.Advance(motion,direction*speed/fps,1.0/fps));
                const double phase=gait.OriginalWalkPhase();
                const double delta=phase>=before?phase-before:phase+1-before;
                CHECK(delta>=0 && delta<=kOriginalWalkMaxFramesPerSecond/(4*fps)+1e-12);
                phases+=delta;
                const auto pose=Evaluate(motion,900000,sample,direction,gait);
                CHECK(pose.originalWalk && !pose.jointWalk && !pose.walkingRig && pose.frame>=0 && pose.frame<4);
                CHECK(pose.flip==(direction<0) && pose.blendFrame==-1 && pose.blend==0);
                changes+=pose.frame!=previous;previous=pose.frame;
            }
            CHECK(changes<=2*kOriginalWalkMaxFramesPerSecond);
            if(speed<=29)CHECK(std::abs(phases-speed*2/kOriginalWalkStride)<1e-9);
            else if(speed>=45)CHECK(std::abs(phases*4-2*kOriginalWalkMaxFramesPerSecond)<1e-9);
            finalPhases[gaitIndex++]=gait.OriginalWalkPhase();
            const auto held=Evaluate(motion,0,0,direction,gait);const double heldPhase=gait.OriginalWalkPhase();
            for(int sample=0;sample<fps;++sample)CHECK(gait.Advance(motion,0,1.0/fps));
            const auto stopped=Evaluate(motion,100000,100000,direction,gait);
            CHECK(gait.OriginalWalkPhase()==heldPhase && held.frame==stopped.frame);
            CHECK(stopped.x==0 && stopped.lift==0 && stopped.angle==0 && cat::deform::Zero(stopped.joints));
        }
        CHECK(std::abs(finalPhases[0]-finalPhases[1])<1e-12 && std::abs(finalPhases[1]-finalPhases[2])<1e-12);
    }
}

void AuthoredLocomotionUsesPhysicalDistanceAtEveryFrameRate() {
    using namespace cat::animation;
    using Mode=cat::authored_locomotion::Mode;
    // Registered reference-size distances, rather than an elapsed animation
    // clock or the old four-drawing presentation cap, drive the new cycles.
    constexpr double walkStride=72,runStride=150;
    for(const double speed:{12.,22.,42.,80.,165.})for(const int direction:{-1,1}) {
        double previousResult=-1;
        for(const int fps:{30,60,120,240}) {
            Locomotion gait;
            CHECK(gait.ConfigureAuthoredStrides(walkStride,runStride));
            const bool running=speed>=cat::authored_locomotion::Cycle::RunEnterSpeed;
            const double stride=running?runStride:walkStride;
            double accumulated=0;
            for(int sample=0;sample<fps*2;++sample) {
                const double before=gait.AuthoredPhase();
                // The requested action deliberately remains Walk at high
                // speed: the Box2D travel chooses the actual run artwork.
                CHECK(gait.Advance(Motion::Walk,direction*speed/fps,1.0/fps,true));
                const double after=gait.AuthoredPhase();
                const double delta=after>=before?after-before:after+1-before;
                CHECK(std::abs(delta-speed/(stride*fps))<1e-12);
                accumulated+=delta;
                CHECK(after>=0 && after<1);
                CHECK(gait.AuthoredMode()==(running?Mode::Run:Mode::Walk));
            }
            CHECK(std::abs(accumulated-speed*2/stride)<1e-11);
            if(previousResult>=0)CHECK(std::abs(gait.AuthoredPhase()-previousResult)<1e-11);
            previousResult=gait.AuthoredPhase();
        }
    }
}

void AuthoredLocomotionFreezesItsDrawingAndModeWithoutRelativeTravel() {
    using namespace cat::animation;
    using Mode=cat::authored_locomotion::Mode;
    Locomotion gait;
    CHECK(gait.ConfigureAuthoredStrides(72,150));
    CHECK(gait.Advance(Motion::Walk,10,.1,true));
    CHECK(gait.AuthoredMode()==Mode::Run);
    const double held=gait.AuthoredPhase();
    // A support can move far across the desktop while its relative travel is
    // zero. Time passing, pauses and a blocked motor hold BOTH phase and mode.
    for(int sample=0;sample<600;++sample) {
        CHECK(gait.Advance(sample%2?Motion::Walk:Motion::Zoomies,0,.01,true));
        CHECK(gait.AuthoredPhase()==held && gait.AuthoredMode()==Mode::Run);
    }
    CHECK(gait.Advance(Motion::Walk,0,0,true));
    CHECK(gait.AuthoredPhase()==held && gait.AuthoredMode()==Mode::Run);
    // A Walk request can survive a window being closed underneath the cat.
    // Physical contact, not the action label, prevents stepping in free fall.
    for(int sample=0;sample<30;++sample) {
        CHECK(gait.Advance(Motion::Walk,2,.02,false));
        CHECK(gait.AuthoredPhase()==held && gait.AuthoredMode()==Mode::Run);
    }
    for(const auto motion:{Motion::Jump,Motion::Pounce,Motion::Idle,Motion::Eat}) {
        CHECK(gait.Advance(motion,1,.02,true));
        CHECK(gait.AuthoredPhase()==held && gait.AuthoredMode()==Mode::Run);
    }
    CHECK(gait.Advance(Motion::Stalk,-.44,.02,true));
    CHECK(gait.AuthoredMode()==Mode::Walk);
    CHECK(std::abs(gait.AuthoredPhase()-(held+.44/72))<1e-12);
}

void AuthoredGaitChangesPreservePhaseAndUseSpeedHysteresis() {
    using namespace cat::animation;
    using Mode=cat::authored_locomotion::Mode;
    Locomotion gait;
    CHECK(gait.ConfigureAuthoredStrides(72,150));
    Mode expected=Mode::Walk;
    double phase=0;
    const std::array<double,13> speeds{{22,59.99,60,59.99,48,45.01,45,45.01,59.99,60,80,44.99,22}};
    const std::array<Motion,3> requests{{Motion::Stalk,Motion::Walk,Motion::Zoomies}};
    for(unsigned sample=0;sample<speeds.size();++sample) {
        const double speed=speeds[sample];
        if(expected==Mode::Walk && speed>=60)expected=Mode::Run;
        else if(expected==Mode::Run && speed<=45)expected=Mode::Walk;
        const double stride=expected==Mode::Run?150:72;
        phase=std::fmod(phase+speed*.02/stride,1.0);
        CHECK(gait.Advance(requests[sample%requests.size()],speed*.02,.02,true));
        CHECK(gait.AuthoredMode()==expected);
        CHECK(std::abs(gait.AuthoredPhase()-phase)<1e-12);
        // Rendering the same step with a freshly restarted action clock is
        // independent of requested motion and facing. Hosts attach it later.
        for(const auto motion:requests) {
            const auto pose=Evaluate(motion,0,0,-1,gait);
            CHECK(pose.flip && pose.originalWalk && pose.spriteClip==cat::sprite_sequence::Clip::None);
            CHECK(std::abs(gait.AuthoredPhase()-phase)<1e-12);
        }
    }
    const double held=gait.AuthoredPhase();const auto mode=gait.AuthoredMode();
    // Nutrition width/growth registration must not reset a half-finished step.
    CHECK(gait.ConfigureAuthoredStrides(72*1.15,150*1.15));
    CHECK(gait.AuthoredPhase()==held && gait.AuthoredMode()==mode);
    for(double invalid:{0.,-1.,std::numeric_limits<double>::infinity(),std::numeric_limits<double>::quiet_NaN()}) {
        CHECK(!gait.ConfigureAuthoredStrides(invalid,150));
        CHECK(!gait.ConfigureAuthoredStrides(72,invalid));
        CHECK(gait.AuthoredPhase()==held && gait.AuthoredMode()==mode);
    }
    gait.Reset();
    CHECK(gait.AuthoredConfigured() && gait.AuthoredPhase()==0 && gait.AuthoredMode()==Mode::Walk);
    CHECK(gait.Advance(Motion::Walk,7.2*1.15,.2,true));
    CHECK(std::abs(gait.AuthoredPhase()-.1)<1e-12);
}

void NativeVideoCadenceDoesNotSelectTheOldRunGait() {
    using namespace cat::animation;
    using Mode=cat::authored_locomotion::Mode;
    for(double modifiers:{.7,1.,1.25,1./.7})for(int fps:{30,60,120}) {
        Locomotion gait;
        const double nativeWalk=81.6*modifiers;
        CHECK(gait.ConfigureAuthoredStrides(68,72,0,nativeWalk));
        for(int tick=0;tick<fps;++tick) {
            CHECK(gait.Advance(Motion::Walk,nativeWalk/fps,1./fps,true));
            CHECK(gait.AuthoredMode()==Mode::Walk);
        }
        CHECK(std::abs(gait.AuthoredPhase()-std::fmod(nativeWalk/68,1.))<1e-11);
        CHECK(gait.Advance(Motion::Zoomies,135*modifiers/fps,1./fps,true));
        CHECK(gait.AuthoredMode()==Mode::Run);
        CHECK(gait.Advance(Motion::Walk,nativeWalk/fps,1./fps,true));
        CHECK(gait.AuthoredMode()==Mode::Walk);
        const auto phase=gait.AuthoredPhase();
        for(double invalid:{-1.,std::numeric_limits<double>::infinity(),std::numeric_limits<double>::quiet_NaN()}) {
            CHECK(!gait.ConfigureAuthoredStrides(68,72,0,invalid));
            CHECK(gait.AuthoredPhase()==phase);
        }
        CHECK(gait.SetAuthoredPhase(.375)&&gait.AuthoredPhase()==.375);
        CHECK(!gait.SetAuthoredPhase(-.01)&&gait.AuthoredPhase()==.375);
        CHECK(!gait.SetAuthoredPhase(std::numeric_limits<double>::quiet_NaN())&&gait.AuthoredPhase()==.375);
        CHECK(gait.SetAuthoredPhase(1)&&gait.AuthoredPhase()==0);
    }
    cat::authored_locomotion::Cycle cycle;
    CHECK(cycle.Configure(68,72,81.6));
    CHECK(std::abs(cycle.RunEnterThreshold()-110.16)<1e-10);
    CHECK(std::abs(cycle.RunExitThreshold()-93.84)<1e-10);
    CHECK(cycle.Configure(68,72));
    CHECK(cycle.RunEnterThreshold()==60 && cycle.RunExitThreshold()==45);
}

void ActualTakeoffKeepsPreparationsGroundedPastPreviewDeadline() {
    using namespace cat::animation;
    using Clip=cat::sprite_sequence::Clip;
    for(const int facing:{-1,1})for(const bool pounce:{false,true})
        for(const std::uint64_t elapsed:{0,219,220,239,240,700,6000,86400000}) {
            const auto pose=JumpPreparationPose(elapsed,facing,pounce);
            CHECK(pose.groundContact && pose.spriteOptions.grounded && pose.spriteClip==Clip::Jump);
            CHECK(pose.frame==4 && pose.flip==(facing<0) && pose.lift==0 && pose.x==0);
            CHECK(pose.spriteOptions.jump==cat::sprite_sequence::JumpPhase::Crouch);
            CHECK(pose.joints.crouch>=0 && pose.joints.crouch<=.65);
            CHECK(pose.spriteFrame<4);
        }
}

void AuthoredLocomotionAttachesOnlyAvailableGroundedWalking() {
    using namespace cat::animation;
    using Clip=cat::sprite_sequence::Clip;
    for(const auto motion:{Motion::Walk,Motion::Stalk,Motion::Zoomies})
        for(const double speed:{12.,22.,42.,80.,165.})for(const int direction:{-1,1}) {
            Locomotion gait;
            CHECK(gait.ConfigureAuthoredStrides(72,150));
            for(int sample=0;sample<60;++sample)
                CHECK(gait.Advance(motion,direction*speed/60,1./60,true));
            const auto fallback=Evaluate(motion,9000,9000,direction,gait);
            auto unavailable=fallback;
            // Silver and other characters without this exact new asset keep
            // their old complete drawings, even when the gait chooses Run.
            CHECK(!ApplyAuthoredLocomotion(unavailable,motion,gait,false));
            CHECK(unavailable.frame==fallback.frame && unavailable.originalWalk);
            CHECK(unavailable.spriteClip==Clip::None && !unavailable.spriteTimed);
            CHECK(unavailable.flip==fallback.flip && WeightDistance(unavailable,fallback)==0);
            auto pose=fallback;
            CHECK(ApplyAuthoredLocomotion(pose,motion,gait,true));
            CHECK(pose.spriteClip==AuthoredLocomotionClip(gait));
            CHECK(pose.spriteClip==(speed>=60?Clip::Run:Clip::Walk));
            CHECK(pose.spriteTimed && pose.spriteElapsedMs==0);
            CHECK(pose.spriteOptions.cyclePhase==gait.AuthoredPhase());
            CHECK(pose.spriteFrame<8 && pose.spriteFrame==static_cast<unsigned>(gait.AuthoredPhase()*8));
            CHECK(!pose.originalWalk && !pose.jointWalk && !pose.walkingRig);
            CHECK(pose.flip==(direction<0) && pose.groundContact);
            CHECK(pose.lift==0 && pose.x==0 && pose.angle==0 && pose.scaleX==1 && pose.scaleY==1);
            CHECK(pose.blendFrame==-1 && pose.blend==0);
            for(const auto joint:JointValues(pose.joints))CHECK(joint==0);
        }
    Locomotion unconfigured;
    auto fallback=Evaluate(Motion::Walk,0,0,1,unconfigured);
    CHECK(!ApplyAuthoredLocomotion(fallback,Motion::Walk,unconfigured,true));
    Locomotion gait;CHECK(gait.ConfigureAuthoredStrides(72,150));
    CHECK(gait.Advance(Motion::Walk,2,.1,true));
    for(const auto motion:{Motion::Idle,Motion::Eat,Motion::Punch,Motion::ToyPaw,Motion::BoxSit}) {
        auto pose=Evaluate(motion,300,300,1,gait);
        const auto original=pose;
        CHECK(!ApplyAuthoredLocomotion(pose,Motion::Walk,gait,true));
        CHECK(pose.spriteClip==original.spriteClip && pose.frame==original.frame);
    }
    for(auto pose:{AirbornePose(80,1),LandingPose(70,1,1),JumpPreparationPose(900,1)}) {
        const auto clip=pose.spriteClip;const auto frame=pose.spriteFrame;
        CHECK(!ApplyAuthoredLocomotion(pose,Motion::Walk,gait,true));
        CHECK(pose.spriteClip==clip && pose.spriteFrame==frame);
    }
    auto box=Evaluate(Motion::Walk,0,0,1,gait);
    AttachSprite(box,Clip::Box,3000);
    CHECK(!ApplyAuthoredLocomotion(box,Motion::Walk,gait,true));
    CHECK(box.spriteClip==Clip::Box);
    auto stopped=Evaluate(Motion::Walk,0,0,1,gait);
    CHECK(!ApplyAuthoredLocomotion(stopped,Motion::Idle,gait,true));
}

void AuthoredWrapAndActionClockRestartsKeepOnePhysicalStep() {
    using namespace cat::animation;
    using Clip=cat::sprite_sequence::Clip;
    Locomotion gait;CHECK(gait.ConfigureAuthoredStrides(72,150));
    // Exactly one low-speed source cycle visits all eight supplied drawings.
    // Use many small physical samples so neither a reset nor a frame-rate cap
    // can be hidden by sampling just the cycle endpoint.
    std::array<bool,8> seen{};
    for(int step=0;step<=720;++step) {
        auto pose=Evaluate(step%2?Motion::Stalk:Motion::Walk,0,86400000,-1,gait);
        CHECK(ApplyAuthoredLocomotion(pose,Motion::Walk,gait,true));
        CHECK(pose.spriteClip==Clip::Walk && pose.spriteFrame<seen.size());
        seen[pose.spriteFrame]=true;
        if(step<720)CHECK(gait.Advance(Motion::Walk,-.1,.01,true));
    }
    for(bool visited:seen)CHECK(visited);
    CHECK(std::min(gait.AuthoredPhase(),1-gait.AuthoredPhase())<1e-10);
    auto before=Evaluate(Motion::Walk,9000,9000,1,gait);
    CHECK(ApplyAuthoredLocomotion(before,Motion::Walk,gait,true));
    const double phase=gait.AuthoredPhase();
    for(int pause=0;pause<100;++pause)CHECK(gait.Advance(Motion::Walk,0,.1,true));
    auto restarted=Evaluate(Motion::Zoomies,0,86400000,1,gait);
    CHECK(ApplyAuthoredLocomotion(restarted,Motion::Zoomies,gait,true));
    CHECK(gait.AuthoredPhase()==phase && restarted.spriteFrame==before.spriteFrame);
    CHECK(restarted.spriteClip==before.spriteClip && restarted.spriteOptions.cyclePhase==before.spriteOptions.cyclePhase);
}

void AuthoredClipsFollowHostPhysicsAndPreserveFallbacks() {
    using namespace cat::animation;
    using Clip=cat::sprite_sequence::Clip;
    for(const auto& pair:{std::pair{Motion::Groom,Clip::Groom},std::pair{Motion::Eat,Clip::Eat},
            std::pair{Motion::Treat,Clip::Treat},std::pair{Motion::Punch,Clip::Punch}}) {
        const auto pose=Evaluate(pair.first,600,600,1);
        CHECK(pose.spriteClip==pair.second && !pose.fishMeal);
        CHECK(pose.spriteFrame<cat::sprite_sequence::GridFor(pair.second).count);
        CHECK(pose.lift==0);
        auto registered=pose;UseAuthoredSprite(registered);
        CHECK(registered.x==0 && registered.lift==0 && registered.angle==0);
        CHECK(registered.spriteClip==pose.spriteClip && registered.spriteFrame==pose.spriteFrame);
        CHECK(registered.frame==pose.frame);
    }
    CHECK(EvaluateSleep(0,3000,3000,1).spriteClip==Clip::Sleep);
    for(unsigned variant=1;variant<6;++variant)
        CHECK(EvaluateSleep(variant,3000,3000,1).spriteClip==Clip::None);
    CHECK(EvaluateSleep(0,3000,3000,1,true).spriteClip==Clip::None);
    for(double velocity:{-250.,-80.,0.,80.,250.}) {
        const auto pose=AirbornePose(velocity,1);
        CHECK(pose.spriteClip==Clip::Jump && pose.spriteFrame>=4 && pose.spriteFrame<=11);
        CHECK(!pose.groundContact && pose.lift==0 && pose.x==0);
    }
    CHECK(LandingPose(70,1,1).spriteClip==Clip::Jump);
    for(const auto elapsed:{280u,799u,800u,1070u,3000u,30000u}) {
        const auto landed=LandingPose(elapsed,1,1);
        CHECK(landed.spriteClip==Clip::Jump && landed.spriteTimed && landed.groundContact);
        CHECK(landed.spriteOptions.jump==cat::sprite_sequence::JumpPhase::Landing);
        CHECK(landed.spriteOptions.contactElapsedMs==elapsed);
        CHECK(landed.x==0 && landed.lift==0 && landed.scaleX==1 && landed.scaleY==1);
    }
    for(unsigned elapsed=0;elapsed<2500;elapsed+=5) {
        const auto paw=Evaluate(Motion::ToyPaw,elapsed,elapsed,-1);
        CHECK(paw.spriteClip==Clip::Punch && !paw.spriteTimed && paw.flip && paw.groundContact);
        CHECK(paw.spriteFrame==cat::sprite_sequence::SampleToyPaw(elapsed).frame);
        auto registered=paw;UseAuthoredSprite(registered);
        CHECK(registered.x==0 && registered.lift==0 && registered.blendFrame==-1);
    }
    CHECK(Evaluate(Motion::Walk,3000,3000,1).spriteClip==Clip::None);
}

void NewAnimalFallbackKeepsItsContactMetadata() {
    using namespace cat::animation;
    for(double fps:{30.,60.,120.})for(const auto& scenario:{
            std::pair{Motion::Walk,22.},std::pair{Motion::Stalk,12.5},
            std::pair{Motion::Zoomies,165.},std::pair{Motion::Zoomies,230.},
            std::pair{Motion::Walk,170.}}) {
        Locomotion gait;const auto [motion,speed]=scenario;
        for(int i=0;i<fps*3;++i)CHECK(gait.Advance(motion,speed/fps,1/fps));
        const auto pose=Evaluate(motion,0,0,1,gait);
        CHECK(pose.walkFeetValid);
        CHECK(pose.originalWalk && !pose.walkingRig && !pose.jointWalk && pose.frame>=0 && pose.frame<4);
        CHECK(std::abs(gait.Speed()-speed)<1e-8);
        if(speed>=165) {
            CHECK(std::abs(pose.walkStride-52)<1e-5);
            CHECK(speed/pose.walkStride<4.5); // Includes the fast newborn run.
        } else CHECK(std::abs(pose.walkStride-(motion==Motion::Stalk?20:24))<1e-8);
    }
}

void FallbackContactsStayPlantedAcrossGaitChanges() {
    using namespace cat::animation;
    std::array<Pose,3> endings{};int ending=0;
    for(double fps:{30.,60.,120.}) {
        Locomotion gait;double root=0;int plantedChecks=0,contacts=0;
        for(int frame=0;frame<static_cast<int>(fps*8);++frame) {
            const int segment=frame/static_cast<int>(fps*2);
            const Motion motion=segment==0?Motion::Walk:segment==1?Motion::Zoomies:
                segment==2?Motion::Stalk:Motion::Walk;
            const double speed=segment==0?22:segment==1?230:segment==2?12.5:170;
            const auto previous=Evaluate(motion,0,0,1,gait);
            const double beforeRoot=root;root+=speed/fps;
            CHECK(gait.Advance(motion,speed/fps,1/fps));
            const auto pose=Evaluate(motion,0,0,1,gait);
            for(unsigned leg=0;leg<pose.walkFeet.size();++leg) {
                const auto before=previous.walkFeet[leg],after=pose.walkFeet[leg];
                CHECK(std::isfinite(after.x)&&std::isfinite(after.y));
                CHECK(std::abs(after.x)<17 && after.y>=-4.801 && after.y<=0);
                if(before.stance&&after.stance&&before.contact==after.contact) {
                    ++plantedChecks;
                    CHECK(std::abs((root+after.x)-(beforeRoot+before.x))<1e-8);
                    CHECK(after.y==0);
                }
                if(after.contact!=before.contact)++contacts;
            }
            // Changing only the action label cannot move any of the feet.
            for(Motion other:{Motion::Walk,Motion::Stalk,Motion::Zoomies}) {
                const auto switched=Evaluate(other,0,0,1,gait);
                CHECK(cat::gait::SameFeet(pose.walkFeet,switched.walkFeet));
                CHECK(pose.walkStride==switched.walkStride && pose.walkDuty==switched.walkDuty);
            }
        }
        CHECK(plantedChecks>300 && contacts>30);
        endings[ending++]=Evaluate(Motion::Walk,0,0,1,gait);
    }
    for(unsigned i=1;i<endings.size();++i) {
        CHECK(std::abs(endings[i].walkPhase-endings[0].walkPhase)<1e-10);
        for(unsigned leg=0;leg<4;++leg) {
            CHECK(std::abs(endings[i].walkFeet[leg].x-endings[0].walkFeet[leg].x)<1e-8);
            CHECK(std::abs(endings[i].walkFeet[leg].y-endings[0].walkFeet[leg].y)<1e-8);
            CHECK(endings[i].walkFeet[leg].contact==endings[0].walkFeet[leg].contact);
        }
    }
}

void GaitDiscontinuityGuardsAndLegacyPoseCompatibility() {
    using namespace cat::animation;
    Locomotion gait;
    CHECK(gait.ConfigureAuthoredStrides(72,150));
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
        CHECK(gait.Phase() == 0 && gait.OriginalWalkPhase() == 0 && gait.Speed() == 0);
        CHECK(gait.AuthoredPhase()==0 && gait.AuthoredConfigured());
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
        OriginalWalkingKeepsArchivedFramesAndCadence();
        DistanceDrivenFramesStaySingleAndSequential();
        AllWalkingActionsUseIntactSourceDrawings();
        WalkingActionChangesKeepTheSameDrawing();
        IntactWalkingCadenceIsBoundedAndShared();
        AuthoredLocomotionUsesPhysicalDistanceAtEveryFrameRate();
        AuthoredLocomotionFreezesItsDrawingAndModeWithoutRelativeTravel();
        AuthoredGaitChangesPreservePhaseAndUseSpeedHysteresis();
        NativeVideoCadenceDoesNotSelectTheOldRunGait();
        ActualTakeoffKeepsPreparationsGroundedPastPreviewDeadline();
        AuthoredLocomotionAttachesOnlyAvailableGroundedWalking();
        AuthoredWrapAndActionClockRestartsKeepOnePhysicalStep();
        AuthoredClipsFollowHostPhysicsAndPreserveFallbacks();
        NewAnimalFallbackKeepsItsContactMetadata();
        FallbackContactsStayPlantedAcrossGaitChanges();
        GaitDiscontinuityGuardsAndLegacyPoseCompatibility();
        std::cout << "28 animation groups; " << checks << " checks passed\n";
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
