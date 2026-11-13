#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <array>

namespace cat::running_turn {
// Source-video phases: reach/brace, planted pivot, opposite-facing push-off.
// A physical stop can extend only the brace; it never skips the pivot artwork.
class Turn {
public:
    static constexpr std::uint64_t BrakeMs=1000,PivotMs=1500,LaunchMs=2083;
    enum class Phase {None,Braking,Pivot,Launching};
    void Reset(){phase_=Phase::None;age_=0;from_=to_=1;released_=false;}
    bool Begin(int from,int to,std::array<unsigned,3> timing={{BrakeMs,PivotMs,LaunchMs}}) {
        if(Active()||from==to||!from||!to)return false;
        if(!timing[0]||!timing[1]||!timing[2]||timing[0]+timing[1]+timing[2]>15000)return false;
        brake_=timing[0];pivot_=timing[1];launch_=timing[2];
        from_=from<0?-1:1;to_=to<0?-1:1;age_=0;released_=false;phase_=Phase::Braking;return true;
    }
    bool Active()const{return phase_!=Phase::None;}
    bool Released()const{return released_;}
    int From()const{return from_;}
    int To()const{return to_;}
    Phase CurrentPhase()const{return phase_;}
    std::uint64_t SpriteMs()const {
        const double offset=phase_==Phase::Pivot?brake_:phase_==Phase::Launching?brake_+pivot_:0;
        return static_cast<std::uint64_t>(std::max(0.,offset+age_));
    }
    bool Advance(double seconds,bool grounded,double physical,double displayed) {
        if(!Active()||!std::isfinite(seconds)||seconds<=0||seconds>.25)return false;
        if(!grounded||!std::isfinite(physical)||!std::isfinite(displayed)){Reset();return false;}
        age_+=seconds*1000;
        if(phase_==Phase::Braking) {
            age_=std::min<double>(age_,brake_);
            if(age_>=brake_&&std::abs(physical)<=.5&&std::abs(displayed)<=.5){phase_=Phase::Pivot;age_=0;}
        } else if(phase_==Phase::Pivot&&age_>=pivot_) {
            phase_=Phase::Launching;age_-=pivot_;released_=true;return true;
        } else if(phase_==Phase::Launching&&age_>=launch_){phase_=Phase::None;age_=0;}
        return false;
    }
    double Motor(double desired)const {
        if(!Active())return desired;
        if(phase_!=Phase::Launching)return 0;
        if(desired*to_<=0)return 0; // Never accelerate toward a stale target.
        const double t=std::clamp(age_/launch_,0.,1.);
        return desired*t*t*(3-2*t);
    }
private:
    Phase phase_=Phase::None;double age_=0;int from_=1,to_=1;bool released_=false;
    unsigned brake_=BrakeMs,pivot_=PivotMs,launch_=LaunchMs;
};
}
