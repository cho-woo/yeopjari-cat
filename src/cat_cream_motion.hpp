#pragma once
#include "cat_gait.hpp"
#include <algorithm>
#include <cmath>

namespace cat::cream {
// Transient presentation state; no action clocks, saved records or physics are
// owned here. Hosts call Prepare before the motor and Travel after Box2D.
class Motion {
public:
    Motion() { Reset(); }
    void Reset(bool seated=true) {
        sit_=seated?1:0; sitVelocity_=0; quiet_=seated?.65:0; walking_=false;
        cycle_=cat::gait::Cycle({18,.72,3.2},false,0); cycle_.PlantNeutral();
        feet_={};settle_={};settleTime_=1;distance_=0;
    }
    bool Prepare(double seconds,bool wantsWalk,bool calm,bool grounded) {
        if(!std::isfinite(seconds)||seconds<=0||seconds>.25)return Ready();
        if(!grounded||!calm) { Reset(false);return true; }
        walking_=wantsWalk;
        if(wantsWalk)quiet_=0;else quiet_+=seconds;
        const double target=wantsWalk||quiet_<.65?0:1;
        // Critically damped posture: reversals retain velocity and the head
        // follows the same continuous rig instead of changing whole drawings.
        const double omega=11,decay=std::exp(-omega*seconds);
        const double change=sit_-target,term=(sitVelocity_+omega*change)*seconds;
        sitVelocity_=(sitVelocity_-omega*term)*decay;
        sit_=std::clamp(target+(change+term)*decay,0.,1.);
        if(std::abs(sit_-target)<.001 && std::abs(sitVelocity_)<.01){sit_=target;sitVelocity_=0;}
        return Ready();
    }
    void Travel(double bodyDistance,double seconds,bool grounded) {
        if(!std::isfinite(bodyDistance)||!std::isfinite(seconds)||seconds<=0||seconds>.25||
           std::abs(bodyDistance)>std::max(12.,500*seconds)){return;}
        if(!grounded)return;
        const double distance=std::abs(bodyDistance);
        if(distance>1e-5) {
            cycle_.Advance(distance,seconds,true,false);feet_=cycle_.Targets();
            settle_=feet_;settleTime_=0;distance_+=distance;
        } else if(!walking_ || !Ready()) {
            settleTime_=std::min(1.,settleTime_+seconds/.42);
            // Finish the last small recovery, then put every paw down before
            // lowering the pelvis. No phase advances on a blocked motor.
            for(unsigned i=0;i<4;++i) {
                const double delay=(i%2)*.22;
                const double t=std::clamp((settleTime_-delay)/(1-delay),0.,1.);
                const double w=t*t*(3-2*t);
                feet_[i]={settle_[i].x*(1-w),settle_[i].y*(1-w)-
                    2.4*std::sin(3.141592653589793*t),t==1};
            }
            if(settleTime_==1) {feet_={};cycle_.Reset();cycle_.PlantNeutral();}
        }
    }
    bool Ready()const{return sit_<.003;}
    bool Active()const{return std::abs(sitVelocity_)>.01 || (sit_>.001&&sit_<.999)||settleTime_<1;}
    double Sit()const{return sit_;}
    double Distance()const{return distance_;}
    const cat::gait::Feet& Feet()const{return feet_;}
private:
    cat::gait::Cycle cycle_;
    cat::gait::Feet feet_{},settle_{};
    double sit_=1,sitVelocity_=0,quiet_=0,settleTime_=1,distance_=0;
    bool walking_=false;
};
} // namespace cat::cream
