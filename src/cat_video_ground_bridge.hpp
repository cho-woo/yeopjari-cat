#pragma once
#include "cat_sprite_assets.hpp"
#include <cmath>
#include <cstdint>
#include <limits>

namespace cat::video_ground_bridge {

enum class Phase { Neutral, Starting, Side, Braking, Stopping };
struct FrameSample {
    bool valid=false;
    unsigned frame=0;
    unsigned step=cat::sprite_sequence::kMaxProgramSteps;
    std::uint64_t localMs=0;
    bool holding=false;
};

// Presentation-only ground bridge. Box2D still owns displacement/contact and
// the ordinary stop gate owns when a pending action's clock may begin.
class GroundBridge {
public:
    bool Configure(std::uint64_t introMs,std::uint64_t outroMs,std::uint64_t movementDelayMs=0) {
        if(introMs>86400000||outroMs>86400000||movementDelayMs>introMs)return false;
        introMs_=introMs;outroMs_=outroMs;movementDelayMs_=movementDelayMs;
        return true;
    }
    void Reset(std::uint64_t ticks=0) {
        phase_=Phase::Neutral;started_=lastTick_=ticks;carriedAge_=0;clockReady_=false;
        startForSide_=sideAction_=loopReady_=completedOutro_=false;
    }
    void Freeze(std::uint64_t milliseconds) {
        started_=Add(started_,milliseconds);lastTick_=Add(lastTick_,milliseconds);
    }
    void RebaseClock(std::uint64_t ticks) {
        carriedAge_=Age();started_=lastTick_=ticks;clockReady_=true;
    }
    bool Update(std::uint64_t ticks,bool requestedTravel,bool grounded,double relativeVelocity,
                bool allowStart=true,bool sideAction=false) {
        const auto prior=phase_;
        if(clockReady_&&ticks<lastTick_)RebaseClock(ticks);
        clockReady_=true;lastTick_=ticks;
        sideAction_=sideAction&&!requestedTravel;
        const bool requestedSide=requestedTravel||sideAction_;
        if(!grounded||!std::isfinite(relativeVelocity)) {
            phase_=Phase::Neutral;started_=ticks;carriedAge_=0;completedOutro_=loopReady_=false;
            return phase_!=prior;
        }
        const bool moving=std::abs(relativeVelocity)>.5;
        if(phase_==Phase::Neutral) {
            if(requestedSide&&allowStart) {
                completedOutro_=false;
                if(moving)phase_=Phase::Side;
                else {
                    startForSide_=sideAction_;
                    Begin(Phase::Starting,ticks);
                    if(StartDuration()==0)ReleaseEntrance();
                }
            }
        } else if(phase_==Phase::Starting) {
            if(!requestedSide) {
                // Explicit user interruption is permitted; no synthetic rewind.
                phase_=Phase::Neutral;completedOutro_=loopReady_=false;
            } else {
                startForSide_=sideAction_;
                if(Age()>=StartDuration())ReleaseEntrance();
            }
        } else if(phase_==Phase::Side) {
            if(!requestedSide) {
                Begin(moving?Phase::Braking:Phase::Stopping,ticks);
                if(!moving&&outroMs_==0)FinishExit();
            }
        } else if(phase_==Phase::Braking) {
            if(requestedSide)phase_=Phase::Side; // Reversal/renewed travel does not repeat the entrance.
            else if(!moving) {
                Begin(Phase::Stopping,ticks);
                if(outroMs_==0)FinishExit();
            }
        } else if(phase_==Phase::Stopping) {
            if(requestedSide)phase_=Phase::Side;
            else if(moving)Begin(Phase::Braking,ticks);
            else if(Age()>=outroMs_)FinishExit();
        }
        return phase_!=prior;
    }
    bool Starting() const {return phase_==Phase::Starting;}
    bool StartingForSideAction() const {return Starting()&&startForSide_;}
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
        if(Starting())range=layout.program.intro;
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
    std::uint64_t StartDuration() const {return startForSide_?movementDelayMs_:introMs_;}
    void Begin(Phase phase,std::uint64_t ticks) {phase_=phase;started_=ticks;carriedAge_=0;}
    void ReleaseEntrance() {phase_=Phase::Side;if(!startForSide_)loopReady_=true;}
    void FinishExit() {phase_=Phase::Neutral;completedOutro_=true;loopReady_=false;}
    Phase phase_=Phase::Neutral;
    std::uint64_t introMs_=0,outroMs_=0,movementDelayMs_=0,started_=0,lastTick_=0,carriedAge_=0;
    bool clockReady_=false,startForSide_=false,sideAction_=false,loopReady_=false,completedOutro_=false;
};
} // namespace cat::video_ground_bridge
