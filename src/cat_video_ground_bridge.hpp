#pragma once
#include "cat_sprite_assets.hpp"
#include <cmath>
#include <cstdint>
#include <limits>
#include <string_view>

namespace cat::video_ground_bridge {

enum class Phase { Neutral, Starting, Side, Braking, Stopping };
struct FrameSample {
    bool valid=false;
    unsigned frame=0;
    unsigned step=cat::sprite_sequence::kMaxProgramSteps;
    std::uint64_t localMs=0;
    bool holding=false;
};
struct JumpTurnProfile {
    bool valid=false;
    std::uint64_t entryMs=0,exitOffsetMs=0,sideEntryMs=0;
};
inline JumpTurnProfile JumpTurns(const cat::sprite_assets::Descriptor& walk) {
    // This profile names existing source-video poses, not generated frames:
    // source2..11 turns toward the three-quarter jump drawing; source117..120
    // continues the recovered jump's slight three-quarter face back to front.
    if(walk.clip!=cat::sprite_sequence::Clip::Walk || walk.nominalFramesPerSecond!=24 ||
            walk.frameCount!=61 || walk.program.intro.count!=28 || walk.program.outro.count!=13 ||
            !walk.sourcePath || std::string_view(walk.sourcePath).find("assets/natural-gait/")!=0)return {};
    return {true,cat::sprite_sequence::Duration(walk.program,{walk.program.intro.first,10}),
        cat::sprite_sequence::Duration(walk.program,{walk.program.outro.first,9}),
        cat::sprite_sequence::Duration(walk.program,{walk.program.outro.first,8})};
}

// Presentation-only ground bridge. Box2D still owns displacement/contact and
// the ordinary stop gate owns when a pending action's clock may begin.
class GroundBridge {
public:
    bool Configure(std::uint64_t introMs,std::uint64_t outroMs,std::uint64_t movementDelayMs=0,
                   std::uint64_t jumpSideEntryMs=0) {
        if(introMs>86400000||outroMs>86400000||movementDelayMs>introMs||jumpSideEntryMs>outroMs)return false;
        introMs_=introMs;outroMs_=outroMs;movementDelayMs_=movementDelayMs;jumpSideEntryMs_=jumpSideEntryMs;
        return true;
    }
    void Reset(std::uint64_t ticks=0) {
        phase_=Phase::Neutral;started_=lastTick_=ticks;carriedAge_=0;clockReady_=false;
        startForSide_=sideAction_=loopReady_=completedOutro_=false;
        jumpAction_=jumpTurnReady_=preparingJump_=startFromSide_=false;
        jumpReturn_=false;
    }
    void Freeze(std::uint64_t milliseconds) {
        started_=Add(started_,milliseconds);lastTick_=Add(lastTick_,milliseconds);
    }
    void RebaseClock(std::uint64_t ticks) {
        carriedAge_=Age();started_=lastTick_=ticks;clockReady_=true;
    }
    void BeginExit(std::uint64_t ticks,std::uint64_t offsetMs=0,bool jumpReturn=false) {
        clockReady_=true;lastTick_=ticks;Begin(Phase::Stopping,ticks);
        carriedAge_=std::min(offsetMs,outroMs_);
        completedOutro_=loopReady_=sideAction_=false;
        jumpReturn_=jumpReturn;
        if(carriedAge_>=outroMs_)FinishExit();
    }
    bool Update(std::uint64_t ticks,bool requestedTravel,bool grounded,double relativeVelocity,
                bool allowStart=true,bool sideAction=false,bool jumpAction=false) {
        const auto prior=phase_;
        if(clockReady_&&ticks<lastTick_)RebaseClock(ticks);
        clockReady_=true;lastTick_=ticks;
        sideAction_=sideAction&&!requestedTravel;
        jumpAction_=jumpAction&&sideAction_&&jumpSideEntryMs_>0;
        if(!jumpAction_)jumpTurnReady_=preparingJump_=false;
        const bool requestedSide=requestedTravel||sideAction_;
        if(!grounded||!std::isfinite(relativeVelocity)) {
            phase_=Phase::Neutral;started_=ticks;carriedAge_=0;completedOutro_=loopReady_=false;
            jumpTurnReady_=preparingJump_=startFromSide_=jumpReturn_=false;
            return phase_!=prior;
        }
        const bool moving=std::abs(relativeVelocity)>.5;
        if(phase_==Phase::Neutral) {
            if(moving) {
                // A cursor interruption, drag release or landing can clear
                // presentation state while Box2D still carries momentum.
                // Keep the real walking cycle visible until the feet stop;
                // a front neutral pose must never replace a moving body.
                completedOutro_=loopReady_=false;
                preparingJump_=jumpAction_;
                Begin(requestedSide&&!preparingJump_?Phase::Side:Phase::Braking,ticks);
            } else if(requestedSide&&allowStart) {
                completedOutro_=false;
                startForSide_=sideAction_;
                startFromSide_=false;
                Begin(Phase::Starting,ticks);
                if(StartDuration()==0)ReleaseEntrance();
            }
        } else if(phase_==Phase::Starting) {
            if(!requestedSide) {
                // Explicit interruption can occur after the first steps
                // release the motor. Preserve moving paws through braking.
                Begin(moving?Phase::Braking:Phase::Neutral,ticks);
                completedOutro_=loopReady_=false;
            } else {
                startForSide_=sideAction_;
                if(Age()>=StartDuration())ReleaseEntrance();
            }
        } else if(phase_==Phase::Side) {
            if(jumpAction_&&!jumpTurnReady_) {
                preparingJump_=true;
                if(moving)Begin(Phase::Braking,ticks);
                else BeginJumpFromSide(ticks);
            } else if(!requestedSide) {
                Begin(moving?Phase::Braking:Phase::Stopping,ticks);
                if(!moving&&outroMs_==0)FinishExit();
            }
        } else if(phase_==Phase::Braking) {
            if(preparingJump_&&jumpAction_) {
                if(!moving)BeginJumpFromSide(ticks);
            } else if(requestedSide)phase_=Phase::Side; // Reversal/renewed travel does not repeat the entrance.
            else if(!moving) {
                Begin(Phase::Stopping,ticks);
                if(outroMs_==0)FinishExit();
            }
        } else if(phase_==Phase::Stopping) {
            if(requestedSide){phase_=Phase::Side;jumpReturn_=false;}
            else if(moving)Begin(Phase::Braking,ticks);
            else if(Age()>=outroMs_)FinishExit();
        }
        return phase_!=prior;
    }
    bool Starting() const {return phase_==Phase::Starting;}
    bool StartingForSideAction() const {return Starting()&&startForSide_;}
    bool JumpReturning() const {return jumpReturn_&&Stopping();}
    bool Stopping() const {return phase_==Phase::Braking||phase_==Phase::Stopping;}
    bool Braking() const {return phase_==Phase::Braking;}
    bool Neutral() const {return phase_==Phase::Neutral;}
    bool Side() const {return phase_==Phase::Side;}
    Phase CurrentPhase() const {return phase_;}
    bool HoldMotor() const {
        return sideAction_||Stopping()||(Starting()&&Age()<movementDelayMs_);
    }
    bool ConsumeLoopReady() {const bool ready=loopReady_;loopReady_=false;return ready;}
    std::uint64_t Elapsed() const {return Age();}

    FrameSample Sample(const cat::sprite_assets::Descriptor& layout) const {
        using namespace cat::sprite_sequence;
        if(!cat::sprite_assets::Valid(layout))return {};
        Range range{};std::uint64_t elapsed=Age();
        if(Starting())range=startFromSide_?layout.program.outro:layout.program.intro;
        else if(phase_==Phase::Stopping)range=layout.program.outro;
        else if(Neutral()) {
            if(completedOutro_&&layout.program.outro.count) {
                range=layout.program.outro;elapsed=Duration(layout.program,range);
            } else if(layout.program.intro.count) {
                range=layout.program.intro;elapsed=0;
            } else return {true,layout.standingFrame,kMaxProgramSteps,0,true};
        } else return {}; // Braking uses the real distance-driven cycle.
        if(!range.count)return {true,layout.standingFrame,kMaxProgramSteps,0,true};
        std::uint64_t offset=0;
        for(unsigned i=0;i<range.count;++i) {
            const unsigned step=range.first+i;const auto& pose=layout.program.steps[step];
            if(elapsed<offset+pose.durationMs)
                return {true,pose.frame,step,elapsed-offset,false};
            offset+=pose.durationMs;
        }
        const unsigned last=range.first+range.count-1;
        return {true,layout.program.steps[last].frame,last,layout.program.steps[last].durationMs,true};
    }
private:
    static std::uint64_t Add(std::uint64_t a,std::uint64_t b) {
        const auto maximum=std::numeric_limits<std::uint64_t>::max();
        return b>maximum-a?maximum:a+b;
    }
    std::uint64_t Age() const {return Add(carriedAge_,lastTick_>=started_?lastTick_-started_:0);}
    std::uint64_t StartDuration() const {return startFromSide_?jumpSideEntryMs_:startForSide_?movementDelayMs_:introMs_;}
    void Begin(Phase phase,std::uint64_t ticks) {phase_=phase;started_=ticks;carriedAge_=0;}
    void BeginJumpFromSide(std::uint64_t ticks) {
        startForSide_=startFromSide_=true;preparingJump_=false;Begin(Phase::Starting,ticks);
    }
    void ReleaseEntrance() {phase_=Phase::Side;if(jumpAction_)jumpTurnReady_=true;else if(!startForSide_)loopReady_=true;}
    void FinishExit() {phase_=Phase::Neutral;completedOutro_=true;loopReady_=jumpReturn_=false;}
    Phase phase_=Phase::Neutral;
    std::uint64_t introMs_=0,outroMs_=0,movementDelayMs_=0,jumpSideEntryMs_=0,started_=0,lastTick_=0,carriedAge_=0;
    bool clockReady_=false,startForSide_=false,sideAction_=false,loopReady_=false,completedOutro_=false;
    bool jumpAction_=false,jumpTurnReady_=false,preparingJump_=false,startFromSide_=false;
    bool jumpReturn_=false;
};
} // namespace cat::video_ground_bridge
