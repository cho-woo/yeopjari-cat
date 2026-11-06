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
    // Zero selects the clip's default duration. Groom stays within 4–8s;
    // Eat/Treat accept 2.5–12s without shortening their entrance or exit.
    std::uint64_t durationMs=0;
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
constexpr std::uint64_t kSleepStoryIntroMs=1400;
constexpr std::uint64_t kSleepStoryRestMs=60000;
constexpr std::uint64_t kSleepWakeMs=880;
constexpr std::uint64_t kLandingMs=280;
constexpr std::uint64_t kEatDurationMs=5000;
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
    return requested==0?6000:std::clamp<std::uint64_t>(requested,4000,8000);
}
inline std::uint64_t CareDuration(Clip clip,std::uint64_t requested) {
    return requested==0?(clip==Clip::Treat?kTreatDurationMs:kEatDurationMs):
                        std::clamp<std::uint64_t>(requested,2500,12000);
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

inline Frame SampleSleep(std::uint64_t elapsed,const Options& options={}) {
    if(GridFor(Clip::Sleep,options.sheetFrames).count==0)
        return {0,0,0,false,true,true,true};
    if(options.sheetFrames==8) {
        if(options.sleep==SleepStage::Waking) {
            // Reuse the actual story drawings in reverse: drowsy, yawn,
            // awake. Once awake, hold frame0 rather than returning to sleep.
            auto pose=Timed(elapsed,std::array<std::uint64_t,3>{{220,330,330}});
            constexpr std::array<unsigned,3> frames{{2,1,0}};
            pose.frame=frames[pose.frame];
            return pose;
        }
        if(options.sleep==SleepStage::Entering && elapsed<kSleepStoryIntroMs)
            return Timed(elapsed,std::array<std::uint64_t,3>{{250,650,500}});
        const auto rest=options.sleep==SleepStage::Entering?elapsed-kSleepStoryIntroMs:elapsed;
        // Curled/deep sleep occupy over96% of this minute. Dreaming,
        // stretching in sleep and a small twitch are brief rare accents;
        // neither awake nor the entrance yawn occurs in the resting loop.
        auto pose=Timed(rest%kSleepStoryRestMs,
            std::array<std::uint64_t,9>{{5000,18000,800,16000,600,14000,900,4000,700}});
        constexpr std::array<unsigned,9> frames{{3,4,5,4,7,4,6,3,4}};
        pose.frame=frames[pose.frame];pose.looping=true;
        return pose;
    }
    // The default12-frame contract and its ordinals/timing remain intact.
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
        if(elapsed>=duration)return {15,140,140,false,true,true,true};
        return Uniform(elapsed,0,16,140,true);
    }
    case Clip::Sleep:return SampleSleep(elapsed,options);
    case Clip::Box:
        // The host physically approaches and touches the rim first. Source
        // frames0/1 place the cat on opposite sides of the prop; replaying
        // them after arrival would visibly teleport the cat across the box.
        // Once centered, use the continuous entered/hide/peek/recover poses.
        {
            auto pose=Timed(elapsed,std::array<std::uint64_t,6>{{560,520,600,500,470,700}});
            pose.frame+=2;return pose;
        }
    case Clip::Fish:
        return Timed(elapsed,std::array<std::uint64_t,8>{{160,140,180,200,260,240,220,400}});
    case Clip::Punch: {
        // Preserve the host's two contacts at 300/600ms. The short second
        // anticipation reuses windup drawings; it never translates the root.
        constexpr std::array<unsigned,15> frames{{0,1,2,3,4,5,2,3,5,6,7,8,9,10,11}};
        auto pose=Timed(elapsed,std::array<std::uint64_t,15>{{90,80,70,60,80,70,70,80,70,80,80,100,120,120,180}});
        pose.frame=frames[pose.frame];
        return pose;
    }
    case Clip::Eat: {
        const auto outroAt=CareDuration(clip,options.durationMs)-800;
        if(elapsed<400)return Uniform(elapsed,0,2,200,false);
        if(elapsed<outroAt)return Uniform(elapsed-400,2,7,120,true);
        // Depleting the bowl and finishing happen once, after chewing.
        auto pose=Timed(elapsed-outroAt,std::array<std::uint64_t,3>{{260,260,280}});
        pose.frame+=9;
        return pose;
    }
    case Clip::Treat: {
        const auto outroAt=CareDuration(clip,options.durationMs)-800;
        if(elapsed<540)return Uniform(elapsed,0,6,90,false);
        if(elapsed<outroAt)return Uniform(elapsed-540,6,9,90,true);
        // The hand/tube entrance is never replayed by the licking loop.
        auto pose=Timed(elapsed-outroAt,std::array<std::uint64_t,9>{{90,90,90,90,90,90,90,90,80}});
        pose.frame+=15;
        return pose;
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
