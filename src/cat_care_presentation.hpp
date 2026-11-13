#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>

namespace cat::care_presentation {
// Per-rendered-actor state only. It never owns action time, physics or saves.
// Internal drawings in a clip are not interpolated: their authored contacts
// remain exact. A neutral bridge is allowed only at a calm clip/idle boundary.
struct Tag {
    unsigned representation=0,identity=0;
    std::uint64_t generation=0;
    bool eligible=false,authored=false,flip=false,neutralBridge=false;
    std::uint64_t action=0;
};
struct Plan {
    bool capture=false,blend=false,bridge=false;
    double weight=1;
};
class Transition {
public:
    static constexpr std::uint64_t DurationMs=220;
    bool Active(std::uint64_t ticks)const{return active_&&ticks>=started_&&ticks-started_<DurationMs;}
    void Reset() {valid_=active_=waitingExit_=false;clockValid_=false;}
    void Pause(std::uint64_t milliseconds) {
        if(active_)started_=Add(started_,milliseconds);
        if(clockValid_)lastTicks_=Add(lastTicks_,milliseconds);
    }
    Plan Update(Tag target,std::uint64_t ticks,bool paused=false) {
        if(clockValid_ && ticks<lastTicks_)Reset();
        if(clockValid_ && active_ && ticks>=lastTicks_) {
            const auto gap=ticks-lastTicks_;
            // Explicit host pauses cover menus/drag. Missing presentations
            // during hide/minimize must not finish a transition off screen.
            if(paused || gap>250)started_=Add(started_,gap);
        }
        lastTicks_=ticks;clockValid_=true;
        const bool compatible=valid_ && target.eligible && previous_.eligible &&
            target.identity==previous_.identity && target.generation==previous_.generation &&
            target.flip==previous_.flip;
        Plan plan;
        if(!compatible) {active_=waitingExit_=false;}
        else if(previous_.representation!=target.representation || previous_.action!=target.action) {
            bridge_=previous_.authored && !target.authored && target.neutralBridge;
            started_=ticks;active_=true;waitingExit_=false;plan.capture=true;
        } else if(waitingExit_) {
            bridge_=false;started_=ticks;active_=true;waitingExit_=false;plan.capture=true;
        }
        if(active_) {
            const auto elapsed=ticks>=started_?ticks-started_:0;
            plan.bridge=bridge_;
            if(elapsed>=DurationMs) {
                active_=false;
                if(bridge_)waitingExit_=true;
            } else {
                const double t=static_cast<double>(elapsed)/DurationMs;
                plan.weight=t*t*(3-2*t);plan.blend=true;
            }
        }
        previous_=target;valid_=target.eligible;
        return plan;
    }
private:
    static std::uint64_t Add(std::uint64_t value,std::uint64_t amount) {
        return value+std::min(amount,std::numeric_limits<std::uint64_t>::max()-value);
    }
    Tag previous_{};
    std::uint64_t started_=0,lastTicks_=0;
    bool valid_=false,active_=false,clockValid_=false,bridge_=false,waitingExit_=false;
};

inline void BlendPremultiplied(const unsigned char* from,const unsigned char* to,
                              unsigned char* output,std::size_t bytes,double fraction) {
    const unsigned weight=static_cast<unsigned>(std::clamp(fraction,0.0,1.0)*255+.5);
    for(std::size_t index=0;index<bytes;++index)
        output[index]=static_cast<unsigned char>((from[index]*(255-weight)+to[index]*weight+127)/255);
}
} // namespace cat::care_presentation
