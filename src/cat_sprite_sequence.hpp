#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>

namespace cat::sprite_sequence {

// The first eight ordinals are stable asset identifiers. None is not a sheet.
enum class Clip { Groom, Sleep, Box, Fish, Punch, Eat, Jump, Treat, None };
constexpr std::array<Clip,8> kClips{{Clip::Groom,Clip::Sleep,Clip::Box,Clip::Fish,
                                   Clip::Punch,Clip::Eat,Clip::Jump,Clip::Treat}};
struct Grid { unsigned columns=0,rows=0,count=0; };
constexpr Grid GridFor(Clip clip,unsigned sheetFrames=0) {
    Grid grid;
    switch(clip) {
    case Clip::Groom:grid={4,4,16};break;
    case Clip::Sleep:grid=sheetFrames==8?Grid{4,2,8}:Grid{4,3,12};break;
    case Clip::Box:grid={4,2,8};break;
    case Clip::Fish:grid={4,2,8};break;
    case Clip::Punch:grid={4,3,12};break;
    case Clip::Eat:grid={3,4,12};break;
    case Clip::Jump:grid={4,4,16};break;
    case Clip::Treat:grid={6,4,24};break;
    case Clip::None:break;
    }
    return sheetFrames==0 || sheetFrames==grid.count?grid:Grid{};
}
constexpr bool GridAllowed(Clip clip,unsigned columns,unsigned rows) {
    const auto grid=GridFor(clip,columns*rows);
    return grid.count!=0 && grid.columns==columns && grid.rows==rows;
}

enum class SleepStage { Entering, Resting, Waking };
enum class JumpPhase { Crouch, Airborne, Landing };
struct Options {
    SleepStage sleep=SleepStage::Entering;
    JumpPhase jump=JumpPhase::Crouch;
    // The caller supplies actual Box2D contact and upward-positive velocity.
    // This module supplies no root translation, scale, angle or jump height.
    bool grounded=true;
    double velocityY=0;
    std::uint64_t contactElapsedMs=0;
    // Zero selects the clip's default duration. Groom stays within4–8s;
    // meals accept3.5–12s without shortening their entrance or exit.
    std::uint64_t durationMs=0;
    // Choose once per event, not per render: paw/face, chest, or hind leg.
    unsigned variant=0;
    // Zero retains the original sheet layout. Authored eight-frame sleep
    // sheets use a different story: awake/yawn/drowsy, then resting poses.
    unsigned sheetFrames=0;
};
struct Frame {
    unsigned frame=0;
    std::uint64_t localMs=0,holdMs=0;
    bool looping=false,holding=false,finished=false,groundContact=true;
};
constexpr std::uint64_t kPunchImpactMs=300;
constexpr std::uint64_t kPunchSecondImpactMs=600;
constexpr unsigned kPunchImpactFrame=4;
constexpr unsigned kPunchSecondImpactFrame=5;
constexpr std::array<std::uint64_t,2> kPunchImpacts{{kPunchImpactMs,kPunchSecondImpactMs}};
constexpr std::uint64_t kSleepIntroMs=1200;
constexpr std::uint64_t kSleepStoryIntroMs=3800;
constexpr std::uint64_t kSleepStoryRestMs=120000;
constexpr std::uint64_t kSleepWakeMs=3000;
constexpr std::uint64_t kGroomDefaultMs=6000;
constexpr std::uint64_t kGroomMinMs=4000;
constexpr std::uint64_t kGroomMaxMs=8000;
constexpr std::uint64_t kBoxDurationMs=8500;
constexpr std::uint64_t kCareMinDurationMs=3500;
constexpr std::uint64_t kCareMaxDurationMs=12000;
constexpr std::uint64_t kCareOutroMs=1400;
constexpr std::uint64_t kLandingMs=280;
constexpr std::uint64_t kEatDurationMs=5000;
constexpr std::uint64_t kFishDurationMs=5000;
constexpr std::uint64_t kTreatDurationMs=4200;

inline unsigned SelectSleepVariant(double roll,bool authored) {
    if(!std::isfinite(roll))return 0;
    roll=std::clamp(roll,0.0,1.0);
    if(!authored)return std::min(5u,static_cast<unsigned>(roll*6));
    // Choose once per sleep event. The authored story is the usual sleep;
    // the five existing alternate poses remain available as rare events.
    if(roll<.75)return 0;
    if(roll<.80)return 1;
    if(roll<.85)return 2;
    if(roll<.90)return 3;
    if(roll<.95)return 4;
    return 5;
}

inline std::uint64_t GroomDuration(std::uint64_t requested) {
    return requested==0?kGroomDefaultMs:std::clamp(requested,kGroomMinMs,kGroomMaxMs);
}
inline std::uint64_t CareDuration(Clip clip,std::uint64_t requested) {
    return requested==0?(clip==Clip::Treat?kTreatDurationMs:clip==Clip::Fish?kFishDurationMs:kEatDurationMs):
                        std::clamp(requested,kCareMinDurationMs,kCareMaxDurationMs);
}
inline Frame Uniform(std::uint64_t elapsed,unsigned first,unsigned count,
                     std::uint64_t frameMs,bool loop) {
    if(count==0||frameMs==0)return {first,0,0,false,true,true,true};
    const std::uint64_t duration=frameMs*count;
    if(!loop && elapsed>=duration)return {first+count-1,frameMs,frameMs,false,true,true,true};
    const auto time=loop?elapsed%duration:elapsed;
    return {first+static_cast<unsigned>(time/frameMs),time%frameMs,frameMs,loop,false,false,true};
}

template<std::size_t N>
inline Frame Timed(std::uint64_t elapsed,const std::array<std::uint64_t,N>& durations) {
    std::uint64_t start=0;
    for(unsigned index=0;index<N;++index) {
        if(elapsed-start<durations[index])
            return {index,elapsed-start,durations[index],false,false,false,true};
        start+=durations[index];
    }
    return {static_cast<unsigned>(N-1),durations[N-1],durations[N-1],false,true,true,true};
}

// A bounded story visits one related family of poses once. Stretch its
// holds to the host's actual event duration; never wrap the whole storyboard
// back to its entrance or switch grooming posture families mid-event.
template<std::size_t N>
inline Frame Story(std::uint64_t elapsed,const std::array<unsigned,N>& frames,
                   const std::array<std::uint64_t,N>& weights,std::uint64_t duration) {
    static_assert(N>0);
    std::uint64_t total=0;for(const auto weight:weights)total+=weight;
    if(duration==0||total==0)return {frames[N-1],0,0,false,true,true,true};
    const bool finished=elapsed>=duration;
    if(finished)elapsed=duration-1;
    std::uint64_t start=0,accumulated=0;
    for(unsigned index=0;index<N;++index) {
        accumulated+=weights[index];
        const auto end=duration*accumulated/total;
        if(elapsed<end) {
            const auto hold=end-start;
            return {frames[index],finished?hold:elapsed-start,hold,false,finished,finished,true};
        }
        start=end;
    }
    return {frames[N-1],0,0,false,true,true,true};
}

inline Frame SampleSleep(std::uint64_t elapsed,const Options& options={}) {
    if(GridFor(Clip::Sleep,options.sheetFrames).count==0)
        return {0,0,0,false,true,true,true};
    if(options.sheetFrames==8) {
        if(options.sleep==SleepStage::Waking) {
            // Rise from the curled posture through a low stretch and tucked
            // paws before sitting up. The resting loop never replays these
            // large changes; once awake, keep the quiet final frame.
            return Story(elapsed,std::array<unsigned,5>{{6,7,2,1,0}},
                std::array<std::uint64_t,5>{{700,650,650,600,400}},kSleepWakeMs);
        }
        if(options.sleep==SleepStage::Entering && elapsed<kSleepStoryIntroMs)
            return Timed(elapsed,std::array<std::uint64_t,3>{{900,1300,1600}});
        const auto rest=options.sleep==SleepStage::Entering?elapsed-kSleepStoryIntroMs:elapsed;
        // Sleep stays curled for minutes. A seated twitch or full stretch
        // is a large posture change and belongs to waking, not a fast cycle
        // between incompatible sleeping silhouettes.
        auto pose=Timed(rest%kSleepStoryRestMs,
            std::array<std::uint64_t,5>{{12000,42000,2000,50000,14000}});
        constexpr std::array<unsigned,5> frames{{3,4,5,4,3}};
        pose.frame=frames[pose.frame];pose.looping=true;
        return pose;
    }
    // The default12-frame layout retains its entrance/rest ordinals.
    // Its waking hold uses the same calmer duration as the uploaded sheet.
    if(options.sleep==SleepStage::Waking)return Uniform(elapsed,8,4,kSleepWakeMs/4,false);
    if(options.sleep==SleepStage::Entering && elapsed<kSleepIntroMs) {
        return elapsed<500?Frame{0,elapsed,500,false,false,false,true}:
                           Frame{1,elapsed-500,700,false,false,false,true};
    }
    return Uniform(options.sleep==SleepStage::Entering?elapsed-kSleepIntroMs:elapsed,2,6,600,true);
}

inline Frame Sample(Clip clip,std::uint64_t elapsed,const Options& options={}) {
    if(GridFor(clip,options.sheetFrames).count==0)return {0,0,0,false,true,true,true};
    switch(clip) {
    case Clip::Groom: {
        const auto duration=GroomDuration(options.durationMs);
        switch(options.variant%3) {
        case 1:return Story(elapsed,std::array<unsigned,6>{{0,9,10,11,12,0}},
            std::array<std::uint64_t,6>{{650,950,1200,1200,1150,850}},duration);
        case 2:return Story(elapsed,std::array<unsigned,5>{{0,13,14,15,0}},
            std::array<std::uint64_t,5>{{750,1300,1600,1350,1000}},duration);
        default:return Story(elapsed,std::array<unsigned,10>{{0,1,2,3,4,5,6,7,8,0}},
            std::array<std::uint64_t,10>{{600,600,700,600,600,600,600,600,500,600}},duration);
        }
    }
    case Clip::Sleep:return SampleSleep(elapsed,options);
    case Clip::Box:
        // The host physically approaches and touches the rim first. Source
        // frames0/1 place the cat on opposite sides of the prop; replaying
        // them after arrival would visibly teleport the cat across the box.
        // Once centered, use the continuous entered/hide/peek/recover poses.
        {
            auto pose=Timed(elapsed,std::array<std::uint64_t,6>{{1200,1000,2600,1100,1400,1200}});
            pose.frame+=2;return pose;
        }
    case Clip::Fish:
        return Story(elapsed,std::array<unsigned,8>{{0,1,2,3,4,5,6,7}},
            std::array<std::uint64_t,8>{{600,700,900,700,700,600,400,400}},
            CareDuration(clip,options.durationMs));
    case Clip::Punch: {
        // Preserve the host's two contacts at 300/600ms. The short second
        // anticipation reuses windup drawings; it never translates the root.
        constexpr std::array<unsigned,15> frames{{0,1,2,3,4,5,2,3,5,6,7,8,9,10,11}};
        auto pose=Timed(elapsed,std::array<std::uint64_t,15>{{90,80,70,60,80,70,70,80,70,180,200,260,300,340,700}});
        pose.frame=frames[pose.frame];
        return pose;
    }
    case Clip::Eat: {
        const auto outroAt=CareDuration(clip,options.durationMs)-kCareOutroMs;
        if(elapsed<1000) {
            auto pose=Timed(elapsed,std::array<std::uint64_t,2>{{600,400}});
            constexpr std::array<unsigned,2> frames{{0,4}};pose.frame=frames[pose.frame];return pose;
        }
        // Only the adjacent head-down drawings chew. Cycling all the upright
        // and empty-bowl poses refilled the bowl and jerked the head upright.
        if(elapsed<outroAt)return Uniform(elapsed-1000,1,2,350,true);
        auto pose=Timed(elapsed-outroAt,std::array<std::uint64_t,4>{{300,250,300,550}});
        pose.frame+=8;return pose;
    }
    case Clip::Treat: {
        const auto outroAt=CareDuration(clip,options.durationMs)-kCareOutroMs;
        if(elapsed<1100) {
            auto pose=Timed(elapsed,std::array<std::uint64_t,4>{{400,200,250,250}});
            constexpr std::array<unsigned,4> frames{{0,3,4,5}};pose.frame=frames[pose.frame];return pose;
        }
        if(elapsed<outroAt) {
            auto pose=Timed((elapsed-1100)%880,std::array<std::uint64_t,4>{{220,220,220,220}});
            constexpr std::array<unsigned,4> frames{{6,7,8,7}};pose.frame=frames[pose.frame];pose.looping=true;return pose;
        }
        // Withdraw once and settle; never restart the tube's entrance or
        // cycle through every independently painted facial expression.
        auto pose=Timed(elapsed-outroAt,std::array<std::uint64_t,4>{{250,250,300,600}});
        constexpr std::array<unsigned,4> frames{{15,16,20,23}};pose.frame=frames[pose.frame];return pose;
    }
    case Clip::Jump: {
        if(!options.grounded) {
            const double speed=std::isfinite(options.velocityY)?options.velocityY:0;
            // Frame4 is the source sheet's planted prelaunch pose. Once
            // Box2D has left support, use only the true flight drawings.
            const unsigned frame=speed>140?5:speed>70?6:speed>25?7:speed>8?8:
                                 speed>=-8?9:speed>=-100?10:11;
            return {frame,0,0,false,true,false,false};
        }
        if(options.jump!=JumpPhase::Crouch)
            return Uniform(options.contactElapsedMs,12,4,kLandingMs/4,false);
        auto pose=Uniform(elapsed,0,4,55,false);
        // Crouch waits for actual takeoff. An elapsed deadline cannot launch
        // an airborne drawing while Box2D still reports floor contact.
        pose.finished=false;
        return pose;
    }
    case Clip::None:break;
    }
    return {0,0,0,false,true,true,true};
}

// The host owns impact effects. Consumers can inspect cue crossings without
// replaying missed drawings or triggering effects from renderer sampling.
struct Tick { Frame pose; bool impact=false; unsigned impactCount=0; };
class Player {
public:
    void Start(Clip clip,const Options& options={}) {
        clip_=clip;options_=options;elapsed_=0;paused_=false;
    }
    void Pause(bool paused) { paused_=paused; }
    bool Paused()const { return paused_; }
    std::uint64_t Elapsed()const { return elapsed_; }
    void SetOptions(const Options& options) { options_=options; }
    void Wake() {
        if(clip_!=Clip::Sleep || options_.sleep==SleepStage::Waking)return;
        options_.sleep=SleepStage::Waking;elapsed_=0;
    }
    Frame Current()const { return Sample(clip_,elapsed_,options_); }
    Tick Advance(std::uint64_t milliseconds) {
        if(paused_ || milliseconds==0)return {Current(),false,0};
        const auto before=elapsed_;
        elapsed_+=std::min(milliseconds,std::numeric_limits<std::uint64_t>::max()-elapsed_);
        unsigned impacts=0;
        if(clip_==Clip::Punch)for(const auto cue:kPunchImpacts)
            if(before<cue && elapsed_>=cue)++impacts;
        return {Current(),impacts!=0,impacts};
    }
private:
    Clip clip_=Clip::Groom;
    Options options_{};
    std::uint64_t elapsed_=0;
    bool paused_=false;
};

} // namespace cat::sprite_sequence
