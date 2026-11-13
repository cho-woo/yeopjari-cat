#pragma once
#include <algorithm>
#include <cstdint>

namespace cat::gait_transition {
// A complete authored acceleration/deceleration owns its clock. New input is
// still sampled, but cannot restart the film or replace its half-played pose.
class Transition {
public:
    void Reset(){active_=false;}
    bool Active()const{return active_;}
    bool ToRun()const{return toRun_;}
    std::uint64_t Elapsed(std::uint64_t now)const{return now>=started_?now-started_:0;}
    bool Begin(bool toRun,std::uint64_t now,std::uint64_t duration,double speed,double target) {
        if(active_||!duration||duration>10000)return false;
        active_=true;toRun_=toRun;started_=now;duration_=duration;from_=speed;to_=target;return true;
    }
    bool Finish(std::uint64_t now) {
        if(!active_||Elapsed(now)<duration_)return false;
        active_=false;return true;
    }
    void Pause(std::uint64_t ms){if(active_)started_+=ms;}
    double Speed(std::uint64_t now)const {
        const double t=std::clamp(double(Elapsed(now))/std::max<std::uint64_t>(1,duration_),0.,1.);
        return from_+(to_-from_)*t*t*(3-2*t);
    }
private:
    bool active_=false,toRun_=false;
    std::uint64_t started_=0,duration_=0;
    double from_=0,to_=0;
};
}
