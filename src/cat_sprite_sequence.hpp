#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>

namespace cat::sprite_sequence {

// The first eight ordinals are stable asset identifiers. None is not a sheet.
enum class Clip { Groom, Sleep, Box, Fish, Punch, Eat, Jump, Treat, None, Walk, Run, Knead, Reach,
    BallPlay, ToyPlay, Brake, Slide, Stumble, Climb, FlyHunt, RunningTurn, CursorPounce };
constexpr std::array<Clip,8> kClips{{Clip::Groom,Clip::Sleep,Clip::Box,Clip::Fish,
                                   Clip::Punch,Clip::Eat,Clip::Jump,Clip::Treat}};
constexpr std::array<Clip,21> kAuthoredClips{{Clip::Groom,Clip::Sleep,Clip::Box,Clip::Fish,
    Clip::Punch,Clip::Eat,Clip::Jump,Clip::Treat,Clip::Walk,Clip::Run,Clip::Knead,Clip::Reach,
    Clip::BallPlay,Clip::ToyPlay,Clip::Brake,Clip::Slide,Clip::Stumble,Clip::Climb,Clip::FlyHunt,Clip::RunningTurn,Clip::CursorPounce}};
constexpr unsigned kMaxAuthoredFrames=256,kMaxProgramSteps=512,kMaxProgramCues=64;
constexpr std::uint32_t kMaxStepMs=60000,kMaxProgramMs=86400000;
constexpr bool KnownClip(Clip clip) {
    return static_cast<unsigned>(clip)<8 || clip==Clip::Walk || clip==Clip::Run ||
        clip==Clip::Knead || clip==Clip::Reach ||
        (clip>=Clip::BallPlay&&clip<=Clip::CursorPounce);
}
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
    case Clip::Walk:case Clip::Run:case Clip::Knead:case Clip::Reach:grid={8,1,8};break;
    case Clip::BallPlay:case Clip::ToyPlay:case Clip::Brake:case Clip::Slide:case Clip::Stumble:case Clip::Climb:case Clip::FlyHunt:case Clip::RunningTurn:case Clip::CursorPounce:
        grid={8,1,8};break;
    case Clip::None:break;
    }
    return sheetFrames==0 || sheetFrames==grid.count?grid:Grid{};
}
constexpr bool GridAllowed(Clip clip,unsigned columns,unsigned rows) {
    const auto grid=GridFor(clip,columns*rows);
    return grid.count!=0 && grid.columns==columns && grid.rows==rows;
}
// Explicit programs declare their own frame lists; the grid only supplies
// storage capacity. Keep the three-argument legacy layout check unchanged.
constexpr bool GridAllowed(Clip clip,unsigned columns,unsigned rows,unsigned frameCount) {
    return KnownClip(clip) && columns>0 && rows>0 && columns<=kMaxAuthoredFrames && rows<=kMaxAuthoredFrames &&
        static_cast<std::uint64_t>(columns)*rows<=kMaxAuthoredFrames && frameCount>0 &&
        frameCount<=static_cast<std::uint64_t>(columns)*rows;
}

enum class SleepStage { Entering, Resting, Waking };
enum class JumpPhase { Crouch, Airborne, Landing };
enum class Stage { Automatic, Entering, Looping, Exiting };
struct Options {
    SleepStage sleep=SleepStage::Entering;
    JumpPhase jump=JumpPhase::Crouch;
    // The caller supplies actual Box2D contact and upward-positive velocity.
    // This module supplies no root translation, scale, angle or jump height.
    bool grounded=true;
    double velocityY=0;
    // Optional normalized real takeoff-to-landing phase for momentum catches.
    // Other jump programs retain their established velocity clock.
    double flightProgress=-1;
    std::uint64_t contactElapsedMs=0;
    // Zero selects the clip's default duration. Groom stays within4–8s;
    // meals accept3.5–12s without shortening their entrance or exit.
    std::uint64_t durationMs=0;
    // Choose once per event, not per render: paw/face, chest, or hind leg.
    unsigned variant=0;
    // Zero retains the original sheet layout. Authored eight-frame sleep
    // sheets use a different story: awake/yawn/drowsy, then resting poses.
    unsigned sheetFrames=0;
    // Explicit authored programs use separate host-controlled phase clocks.
    // Exiting starts at elapsed=0; a sleeping/hidden cat never exits by timeout.
    Stage stage=Stage::Automatic;
    // Ground-relative travel supplies this phase for authored Walk/Run. Merely
    // rendering another timer tick must not advance a blocked animal's feet.
    double cyclePhase=0;
};
struct Frame {
    unsigned frame=0;
    std::uint64_t localMs=0,holdMs=0;
    bool looping=false,holding=false,finished=false,groundContact=true;
    // Playlist position distinguishes two uses of the same drawing. Legacy
    // clips have no authored step and leave this sentinel unchanged.
    unsigned step=kMaxProgramSteps;
};

// Authored art count, playlist length, exposure time and the host's refresh
// interval are deliberately independent. Reusing a step never creates art.
struct Step { unsigned frame=0; std::uint32_t durationMs=0; };
struct Range { unsigned first=0,count=0; };
enum class Finish { Hold=0, Complete=1 };
enum Event : std::uint32_t { Contact=1, Takeoff=2, Landing=4, Marker=8 };
struct Cue { unsigned step=0; std::uint32_t offsetMs=0,events=0; };
struct Program {
    unsigned stepCount=0;
    std::array<Step,kMaxProgramSteps> steps{};
    Range intro{},loop{},outro{};
    Finish finish=Finish::Complete;
    std::uint32_t durationMs=0;
    unsigned cueCount=0;
    std::array<Cue,kMaxProgramCues> cues{};
};
constexpr std::uint64_t Duration(const Program& program,Range range) {
    if(range.first>program.stepCount || range.count>program.stepCount-range.first ||
       program.stepCount>kMaxProgramSteps)return 0;
    std::uint64_t result=0;
    for(unsigned i=0;i<range.count;++i)result+=program.steps[range.first+i].durationMs;
    return result;
}
constexpr bool ValidProgram(const Program& program,unsigned frameCount=kMaxAuthoredFrames) {
    if(frameCount==0 || frameCount>kMaxAuthoredFrames || program.stepCount==0 ||
       program.stepCount>kMaxProgramSteps || program.cueCount>kMaxProgramCues ||
       program.durationMs>kMaxProgramMs ||
       (program.finish!=Finish::Hold && program.finish!=Finish::Complete))return false;
    if(program.intro.first!=0 || program.intro.count>program.stepCount ||
       program.loop.first!=program.intro.count || program.loop.count>program.stepCount-program.intro.count ||
       program.outro.first!=program.intro.count+program.loop.count ||
       program.outro.count!=program.stepCount-program.outro.first)return false;
    for(unsigned i=0;i<program.stepCount;++i)
        if(program.steps[i].frame>=frameCount || program.steps[i].durationMs==0 ||
           program.steps[i].durationMs>kMaxStepMs)return false;
    for(unsigned i=0;i<program.cueCount;++i) {
        const auto& cue=program.cues[i];
        if(cue.step>=program.stepCount || cue.offsetMs>=program.steps[cue.step].durationMs ||
           cue.events==0 || (cue.events&~std::uint32_t{15})!=0)return false;
    }
    const auto edges=Duration(program,program.intro)+Duration(program,program.outro);
    return program.durationMs==0 || program.durationMs>=edges;
}
inline Frame SampleRange(const Program& program,Range range,std::uint64_t elapsed,
                         bool repeat,bool finish=false) {
    const auto duration=Duration(program,range);
    if(range.count==0 || duration==0)return {0,0,0,false,true,finish,true};
    const bool ended=!repeat && elapsed>=duration;
    auto local=repeat?elapsed%duration:std::min(elapsed,duration-1);
    for(unsigned i=0;i<range.count;++i) {
        const auto& step=program.steps[range.first+i];
        if(local<step.durationMs)
            return {step.frame,ended?step.durationMs:local,step.durationMs,repeat,ended,ended&&finish,true,range.first+i};
        local-=step.durationMs;
    }
    return {}; // All validated ranges have positive exposures.
}
inline std::uint64_t ProgramDuration(const Program& program,const Options& options={}) {
    const auto edges=Duration(program,program.intro)+Duration(program,program.outro);
    const auto requested=options.durationMs?options.durationMs:program.durationMs;
    return requested?std::max(edges,std::min<std::uint64_t>(requested,kMaxProgramMs)):
        edges+Duration(program,program.loop);
}
inline Stage ProgramStage(Clip clip,const Options& options) {
    if(options.stage!=Stage::Automatic)return options.stage;
    if(clip==Clip::Sleep) {
        if(options.sleep==SleepStage::Waking)return Stage::Exiting;
        if(options.sleep==SleepStage::Resting)return Stage::Looping;
    }
    return Stage::Automatic;
}
inline double UnitPhase(double phase) {
    return std::isfinite(phase)?phase-std::floor(phase):0;
}
// Upward-positive velocity is supplied by Box2D in screen pixels/second. The
// flight strip is sampled through ascent/apex/descent; it adds no translation.
inline std::uint64_t FlightClock(const Program& program,const Options& options) {
    const auto duration=Duration(program,program.loop);
    const double speed=std::isfinite(options.velocityY)?options.velocityY:0;
    const double progress=std::isfinite(options.flightProgress)&&options.flightProgress>=0?
        std::clamp(options.flightProgress,0.,1.):std::clamp((200.0-speed)/400.0,0.0,1.0);
    return duration?std::min(duration-1,static_cast<std::uint64_t>(progress*static_cast<double>(duration))):0;
}
inline Frame SampleProgram(Clip clip,const Program& program,std::uint64_t elapsed,
                           const Options& options={}) {
    if(!KnownClip(clip) || !ValidProgram(program,options.sheetFrames?options.sheetFrames:kMaxAuthoredFrames))
        return {0,0,0,false,true,true,true};
    const bool finish=program.finish==Finish::Complete;
    const auto intro=Duration(program,program.intro),loop=Duration(program,program.loop);
    const auto stage=ProgramStage(clip,options);
    if(clip==Clip::Walk || clip==Clip::Run) {
        const Range range=program.loop.count?program.loop:Range{0,program.stepCount};
        const auto duration=Duration(program,range);
        return SampleRange(program,range,static_cast<std::uint64_t>(UnitPhase(options.cyclePhase)*duration),true);
    }
    if(clip==Clip::Jump || clip==Clip::CursorPounce) {
        if(!options.grounded) {
            auto pose=SampleRange(program,program.loop,FlightClock(program,options),false);
            pose.finished=false;pose.groundContact=false;return pose;
        }
        if(options.jump==JumpPhase::Crouch)
            return SampleRange(program,program.intro,elapsed,false);
        return SampleRange(program,program.outro,options.contactElapsedMs,false,finish);
    }
    if(stage==Stage::Entering)return SampleRange(program,program.intro,elapsed,false);
    if(stage==Stage::Looping) {
        if(loop)return SampleRange(program,program.loop,elapsed,true);
        return SampleRange(program,program.intro,intro,false);
    }
    if(stage==Stage::Exiting) {
        if(program.outro.count)return SampleRange(program,program.outro,elapsed,false,finish);
        auto pose=SampleRange(program,{0,program.stepCount},Duration(program,{0,program.stepCount}),false,finish);
        return pose;
    }
    if(elapsed<intro)return SampleRange(program,program.intro,elapsed,false);
    // Persistent rest must not restart its entrance or wake on a timer.
    if(clip==Clip::Sleep || clip==Clip::Box) {
        if(loop)return SampleRange(program,program.loop,elapsed-intro,true);
        return SampleRange(program,program.intro,intro,false);
    }
    const auto duration=ProgramDuration(program,options);
    const auto outroAt=duration-Duration(program,program.outro);
    if(elapsed>=outroAt && program.outro.count)
        return SampleRange(program,program.outro,elapsed-outroAt,false,finish);
    if(elapsed>=duration)
        return SampleRange(program,{0,program.stepCount},Duration(program,{0,program.stepCount}),false,finish);
    if(loop)return SampleRange(program,program.loop,elapsed-intro,true);
    return SampleRange(program,program.intro,intro,false);
}

// Effects can consume a crossed cue even when its drawing was skipped, while
// cursor/prank contact additionally requires that the contact pose is visible
// now. A later reuse of that drawing or an indefinite final hold is not a hit.
inline unsigned ProgramContactIndex(Clip clip,const Program& program,std::uint64_t elapsed,
                                    const Options& options={}) {
    const auto pose=SampleProgram(clip,program,elapsed,options);
    if(pose.step==kMaxProgramSteps || pose.holding || pose.finished)return 0;
    unsigned index=0,selected=0;
    std::uint32_t latestOffset=0;
    for(unsigned i=0;i<program.cueCount;++i) {
        const auto& cue=program.cues[i];
        if((cue.events&Contact)==0)continue;
        ++index;
        // A step may contain several contacts. Select the latest elapsed
        // offset, independent of cue registration order; equal offsets keep
        // the first registered contact as the stable visible ordinal.
        if(pose.step==cue.step && pose.localMs>=cue.offsetMs &&
                (selected==0 || cue.offsetMs>latestOffset)) {
            selected=index;latestOffset=cue.offsetMs;
        }
    }
    return selected;
}
inline bool ProgramContactActive(Clip clip,const Program& program,std::uint64_t elapsed,
                                 const Options& options={}) {
    return ProgramContactIndex(clip,program,elapsed,options)!=0;
}

// Automatic care completion honors the authored final-pose policy. This is
// deliberately not a motion/physics lock: explicit commands can replace the
// action, and sleep, box visits and airborne phases keep their own host clocks.
inline bool HoldsFinalCarePose(Clip clip,const Program& program,std::uint64_t elapsed,
                               const Options& options={}) {
    if((clip!=Clip::Groom && clip!=Clip::Eat && clip!=Clip::Fish &&
        clip!=Clip::Treat && clip!=Clip::Punch) || program.finish!=Finish::Hold ||
       !options.grounded || options.stage!=Stage::Automatic ||
       elapsed<ProgramDuration(program,options))return false;
    const auto pose=SampleProgram(clip,program,elapsed,options);
    return pose.holding && !pose.finished;
}

struct ProgramEvents { std::uint32_t events=0;unsigned eventCount=0,contactCount=0; };
// Consume is called by the simulation, never by drawing. Every authored cue
// has its own bit, so skipped drawings, repeated loops and pauses cannot
// duplicate contact/effect requests. Reset only when a new action begins.
class ProgramCueCursor {
public:
    void Reset() { consumed_=0; }
    ProgramEvents Consume(Clip clip,const Program& program,std::uint64_t elapsed,
                          const Options& options={}) {
        ProgramEvents result;
        if(!KnownClip(clip) || !ValidProgram(program,options.sheetFrames?options.sheetFrames:kMaxAuthoredFrames))return result;
        for(unsigned i=0;i<program.cueCount;++i) {
            const auto bit=std::uint64_t{1}<<i;
            if((consumed_&bit)!=0 || !Due(clip,program,program.cues[i],elapsed,options))continue;
            consumed_|=bit;result.events|=program.cues[i].events;++result.eventCount;
            if((program.cues[i].events&Contact)!=0)++result.contactCount;
        }
        return result;
    }
private:
    static bool In(Range range,unsigned step) { return step>=range.first && step-range.first<range.count; }
    static std::uint64_t Offset(const Program& program,Range range,const Cue& cue) {
        return Duration(program,{range.first,cue.step-range.first})+cue.offsetMs;
    }
    static bool Due(Clip clip,const Program& program,const Cue& cue,std::uint64_t elapsed,const Options& options) {
        const auto stage=ProgramStage(clip,options);
        const bool inIntro=In(program.intro,cue.step),inLoop=In(program.loop,cue.step);
        const Range range=inIntro?program.intro:inLoop?program.loop:program.outro;
        const auto offset=Offset(program,range,cue);
        if(clip==Clip::Jump || clip==Clip::CursorPounce) {
            if(!options.grounded)return inLoop && FlightClock(program,options)>=offset;
            if(options.jump==JumpPhase::Crouch)return inIntro && elapsed>=offset;
            return !inIntro && !inLoop && options.contactElapsedMs>=offset;
        }
        if(clip==Clip::Walk || clip==Clip::Run) {
            const Range selected=program.loop.count?program.loop:Range{0,program.stepCount};
            if(!In(selected,cue.step))return false;
            const auto distanceClock=static_cast<std::uint64_t>(UnitPhase(options.cyclePhase)*Duration(program,selected));
            return distanceClock>=Offset(program,selected,cue);
        }
        if(stage==Stage::Entering)return inIntro && elapsed>=offset;
        if(stage==Stage::Looping)return inLoop && elapsed>=offset;
        if(stage==Stage::Exiting)return !inIntro && !inLoop && elapsed>=offset;
        const auto intro=Duration(program,program.intro);
        if(inIntro)return elapsed>=offset;
        if(clip==Clip::Sleep || clip==Clip::Box)return inLoop && elapsed>=intro+offset;
        const auto outroAt=ProgramDuration(program,options)-Duration(program,program.outro);
        if(inLoop)return intro+offset<outroAt && elapsed>=intro+offset;
        return elapsed>=outroAt+offset;
    }
    std::uint64_t consumed_=0;
};
struct ProgramTick { Frame pose;std::uint32_t events=0;unsigned eventCount=0,contactCount=0; };
class ProgramPlayer {
public:
    // Program descriptors are immutable and outlive the player; no art or
    // playlists are copied and playback performs no allocation or decoding.
    void Start(Clip clip,const Program& program,const Options& options={}) {
        clip_=clip;program_=&program;options_=options;elapsed_=0;paused_=false;cues_.Reset();
    }
    void Pause(bool paused) { paused_=paused; }
    bool Paused()const { return paused_; }
    std::uint64_t Elapsed()const { return elapsed_; }
    void SetOptions(const Options& options) { options_=options; }
    void Exit() { if(options_.stage!=Stage::Exiting){ options_.stage=Stage::Exiting;elapsed_=0; } }
    Frame Current()const { return program_?SampleProgram(clip_,*program_,elapsed_,options_):Frame{}; }
    ProgramTick Advance(std::uint64_t milliseconds) {
        if(!program_ || paused_ || milliseconds==0)return {Current(),0,0,0};
        elapsed_+=std::min(milliseconds,std::numeric_limits<std::uint64_t>::max()-elapsed_);
        const auto events=cues_.Consume(clip_,*program_,elapsed_,options_);
        return {Current(),events.events,events.eventCount,events.contactCount};
    }
private:
    Clip clip_=Clip::None;
    const Program* program_=nullptr;
    Options options_{};
    std::uint64_t elapsed_=0;
    bool paused_=false;
    ProgramCueCursor cues_{};
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
constexpr std::uint64_t kToyPawContactMs=330,kToyPawReleaseMs=660;
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

// One quiet reach, touch and recovery for an existing floor toy. These are
// complete frames from the common actor's paw sheet, not a second cat from
// the legacy action atlas. Keep the reach visible for the whole physical
// controller's contact window, including a contact delayed by a rolling ball.
inline Frame SampleToyPaw(std::uint64_t elapsed) {
    constexpr std::array<unsigned,8> frames{{0,1,2,3,5,6,8,11}};
    constexpr std::array<std::uint64_t,8> exposures{{120,90,70,50,
        kToyPawReleaseMs-kToyPawContactMs,120,180,240}};
    auto pose=Timed(elapsed,exposures);
    pose.frame=frames[pose.frame];
    return pose;
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
    case Clip::Walk:case Clip::Run: {
        const double phase=std::isfinite(options.cyclePhase)?options.cyclePhase-std::floor(options.cyclePhase):0;
        return {std::min(7u,static_cast<unsigned>(phase*8)),0,0,true,true,false,true};
    }
    case Clip::None:case Clip::Knead:case Clip::Reach:case Clip::BallPlay:case Clip::ToyPlay:
    case Clip::Brake:case Clip::Slide:case Clip::Stumble:case Clip::Climb:case Clip::FlyHunt:case Clip::RunningTurn:case Clip::CursorPounce:break;
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
