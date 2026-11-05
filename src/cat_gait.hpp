#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace cat::gait {
struct Foot { double x=0, y=0; bool stance=true; std::uint64_t contact=0; };
using Feet = std::array<Foot,4>;
struct Parameters { double stride=24, duty=.68, lift=3.2; };

inline double Smooth(double t) { t=std::clamp(t,0.0,1.0); return t*t*(3-2*t); }
inline double WalkWeight(double duty) {
    return Smooth((duty-.62)/.06)*(1-Smooth((duty-.70)/.08));
}
inline double NeutralX(int leg,double stride,double duty) {
    // The source is already an extended pose: the near forepaw starts in
    // front of its shoulder and the hind paw behind its hip. Centre a slow
    // walking step under those joints instead of extending it twice.
    return (leg==0?3.0:leg==2?-4.0:0.0)*WalkWeight(duty)*std::min(stride/24.,1.0);
}
inline double LegPhase(int leg,double duty) {
    const double trot=std::clamp((.66-duty)/.12,0.0,1.0);
    // A lateral-sequence walk plants near hind -> near fore -> far hind ->
    // far fore. Contralateral legs must remain half a cycle apart. The old
    // far-leg offsets put both front paws and both hind paws only a quarter
    // cycle apart, producing an uneven shuffle despite correct contacts.
    const double hind=.25+.25*trot;
    return leg==0 ? hind : leg==1 ? hind+.5-std::floor(hind+.5) : leg==3 ? .5 : 0;
}
inline Foot FootAt(double phase,double stride=24,double duty=.68,double lift=3.2) {
    if(!std::isfinite(phase)||!std::isfinite(stride)||!std::isfinite(duty)||!std::isfinite(lift))return {};
    phase-=std::floor(phase); duty=std::clamp(duty,.5,.85);
    const double half=stride*duty/2;
    if(phase<duty)return {half-phase*stride,0,true};
    const double t=(phase-duty)/(1-duty), sine=std::sin(3.14159265358979323846*t);
    return {-half+2*half*Smooth(t)-stride*(1-duty)*(2*t*t*t-3*t*t+t),-lift*sine*sine,false};
}
inline bool SameFeet(const Feet& a,const Feet& b) {
    for(unsigned i=0;i<a.size();++i)
        if(a[i].x!=b[i].x||a[i].y!=b[i].y||a[i].stance!=b[i].stance)return false;
    return true;
}
inline Parameters ForSpeed(bool stalking,double speed) {
    const double run=Smooth((speed-45)/120);
    const Parameters slow=stalking?Parameters{20,.78,1.8}:Parameters{};
    return {slow.stride+(52-slow.stride)*run,slow.duty+(.54-slow.duty)*run,
        slow.lift+(4.8-slow.lift)*run};
}

// Contact coordinates belong to the rendered body, not to the requested
// action's clock. A planted paw subtracts every real root displacement, even
// while stride length, speed or the walk/trot sequence is changing.
class Cycle {
public:
    void Reset() { *this=Cycle{}; }
    double Phase() const { return phase_; }
    double Speed() const { return speed_; }
    Parameters Settings(bool stalking=false) const { return initialized_?settings_:ForSpeed(stalking,0); }
    Feet Targets(bool stalking=false) const {
        if(initialized_)return feet_;
        Feet result{};const auto p=ForSpeed(stalking,0);
        for(unsigned i=0;i<result.size();++i) {
            result[i]=FootAt(LegPhase(i,p.duty),p.stride,p.duty,p.lift);
            result[i].x+=NeutralX(i,p.stride,p.duty);
        }
        return result;
    }
    void Advance(double distance,double seconds,bool walking,bool stalking) {
        if(!initialized_)Initialize(stalking);
        // The shared 240Hz integration gives the same contacts at 30/60/120Hz;
        // it does not advance animation time when the desktop is paused.
        const int count=std::max(1,static_cast<int>(std::ceil(seconds*240-1e-10)));
        const double dt=seconds/count, travel=distance/count;
        for(int sample=0;sample<count;++sample) {
            speed_+=(std::min(500.0,distance/seconds)-speed_)*(-std::expm1(-dt/.075));
            if(!walking)continue;
            const auto wanted=ForSpeed(stalking,speed_);
            const double response=-std::expm1(-dt/.14);
            settings_.stride+=(wanted.stride-settings_.stride)*response;
            settings_.duty+=(wanted.duty-settings_.duty)*response;
            settings_.lift+=(wanted.lift-settings_.lift)*response;
            if(travel==0)continue;
            const double advance=travel/settings_.stride;
            const double oldPhase=phase_;
            phase_=Wrap(phase_+advance);
            if(phase_>1-1e-12)phase_=0;
            for(unsigned i=0;i<feet_.size();++i) {
                auto& leg=legs_[i];
                const double offset=Wrap(leg.phase-oldPhase);
                double error=LegPhase(i,settings_.duty)-offset;
                error-=std::floor(error+.5);
                // Retiming a diagonal pair is gradual and never rewinds a
                // leg. Contact anchors absorb the changing phase offsets.
                const double correction=std::clamp(error*dt/.20,-advance*.35,advance*.35);
                AdvanceLeg(i,advance+correction,travel);
            }
        }
    }
private:
    struct Leg {
        double phase=0,startPhase=.68,startX=0,targetX=0,tangent=0,lift=3.2;
    };
    static double Wrap(double phase) { return phase-std::floor(phase); }
    void Initialize(bool stalking) {
        initialized_=true;settings_=ForSpeed(stalking,0);
        for(unsigned i=0;i<feet_.size();++i) {
            auto& leg=legs_[i];leg.phase=LegPhase(i,settings_.duty);
            feet_[i]=FootAt(leg.phase,settings_.stride,settings_.duty,settings_.lift);
            const double neutral=NeutralX(i,settings_.stride,settings_.duty);
            feet_[i].x+=neutral;
            leg.startPhase=settings_.duty;leg.startX=neutral-settings_.stride*settings_.duty/2;
            leg.targetX=neutral+settings_.stride*settings_.duty/2;
            leg.tangent=-settings_.stride*(1-settings_.duty);leg.lift=settings_.lift;
        }
    }
    void BeginSwing(unsigned index,double stride) {
        auto& leg=legs_[index];auto& foot=feet_[index];
        leg.startPhase=leg.phase;leg.startX=foot.x;
        leg.targetX=NeutralX(index,settings_.stride,settings_.duty)+settings_.stride*settings_.duty/2;
        leg.tangent=-stride*(1-leg.startPhase);leg.lift=settings_.lift;
        foot.stance=false;
    }
    void Swing(unsigned index) {
        const auto& leg=legs_[index];auto& foot=feet_[index];
        const double t=std::clamp((leg.phase-leg.startPhase)/(1-leg.startPhase),0.0,1.0);
        const double square=t*t,cube=square*t,sine=std::sin(3.14159265358979323846*t);
        foot.x=(2*cube-3*square+1)*leg.startX+(-2*cube+3*square)*leg.targetX+
            (2*cube-3*square+t)*leg.tangent;
        foot.y=-leg.lift*sine*sine;
    }
    void AdvanceLeg(unsigned index,double advance,double distance) {
        auto& leg=legs_[index];auto& foot=feet_[index];
        const double stride=distance/advance;
        double remaining=advance;
        while(remaining>1e-12) {
            // A slower phase correction must not stretch a planted leg past
            // the original drawing's anatomy. Lift from its exact contact,
            // never clamp or relocate the paw while it bears weight.
            const double reach=std::min(14.0,settings_.stride*settings_.duty/2+1.5);
            const double rearLimit=NeutralX(index,settings_.stride,settings_.duty)-reach;
            if(foot.stance&&(leg.phase>=settings_.duty-1e-12||foot.x<=rearLimit+1e-12))BeginSwing(index,stride);
            const double boundary=foot.stance?std::min(settings_.duty,
                leg.phase+std::max(0.0,foot.x-rearLimit)/stride):1;
            const double step=std::min(remaining,std::max(0.0,boundary-leg.phase));
            leg.phase+=step;remaining-=step;
            if(foot.stance) {foot.x-=step*stride;foot.y=0;}
            else Swing(index);
            if(leg.phase>=boundary-1e-12) {
                if(foot.stance)BeginSwing(index,stride);
                else {leg.phase=0;foot={leg.targetX,0,true,foot.contact+1};}
            }
        }
    }
    bool initialized_=false;
    double phase_=0,speed_=0;
    Parameters settings_;
    Feet feet_{};
    std::array<Leg,4> legs_{};
};
} // namespace cat::gait
