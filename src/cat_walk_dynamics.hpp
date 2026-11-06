#pragma once

#include "cat_gait.hpp"
#include <algorithm>
#include <array>
#include <cmath>

namespace cat::walk_dynamics {

// Logical body pixels, before real-age growth and monitor DPI. Motion follows
// traveled-distance phase; this module never advances a clock or changes a paw.
enum class Mode { Walk, Trot, Gallop };
struct Point { double x=0, y=0; };
struct Frame {
    double torsoX=0, torsoY=0, pitchDegrees=0;
    double spineStretch=0, tailDegrees=0;
    double pivotU=.43, pivotV=.72;
    double airborneWeight=0;
    std::array<Point,4> rootOffsets{}; // near hind, far hind, near fore, far fore
};
inline bool Neutral(const Frame& frame) {
    if(frame.torsoX!=0 || frame.torsoY!=0 || frame.pitchDegrees!=0 ||
       frame.spineStretch!=0 || frame.tailDegrees!=0)return false;
    for(const auto offset:frame.rootOffsets)if(offset.x!=0 || offset.y!=0)return false;
    return true;
}
inline bool Same(const Frame& a,const Frame& b) {
    if(a.torsoX!=b.torsoX || a.torsoY!=b.torsoY || a.pitchDegrees!=b.pitchDegrees ||
       a.spineStretch!=b.spineStretch || a.tailDegrees!=b.tailDegrees ||
       a.pivotU!=b.pivotU || a.pivotV!=b.pivotV || a.airborneWeight!=b.airborneWeight)return false;
    for(unsigned leg=0;leg<4;++leg)
        if(a.rootOffsets[leg].x!=b.rootOffsets[leg].x || a.rootOffsets[leg].y!=b.rootOffsets[leg].y)return false;
    return true;
}
inline bool Finite(const Frame& frame) {
    if(!std::isfinite(frame.torsoX)||!std::isfinite(frame.torsoY)||!std::isfinite(frame.pitchDegrees)||
       !std::isfinite(frame.spineStretch)||!std::isfinite(frame.tailDegrees)||
       !std::isfinite(frame.pivotU)||!std::isfinite(frame.pivotV)||!std::isfinite(frame.airborneWeight))return false;
    for(const auto offset:frame.rootOffsets)if(!std::isfinite(offset.x)||!std::isfinite(offset.y))return false;
    return true;
}
constexpr double kPi=3.1415926535897932384626433832795;

inline double Smooth(double x) {
    x=std::clamp(x,0.0,1.0);
    return x*x*(3-2*x);
}
inline double Wrap(double phase) { return phase-std::floor(phase); }
inline Mode SelectMode(Mode current,double speed) {
    if(!std::isfinite(speed) || speed<30)return Mode::Walk;
    if(speed>100 || (current==Mode::Gallop && speed>=85))return Mode::Gallop;
    if(current==Mode::Walk && speed<=35)return Mode::Walk;
    return Mode::Trot;
}
inline cat::gait::Parameters ParametersFor(Mode mode) {
    // The short painted limbs cannot support the old 52px sprint stride.
    // Trot duty .54 also selects exact diagonal pairs in gait::LegPhase.
    if(mode==Mode::Gallop)return {42,.32,8.0,cat::gait::Sequence::Gallop};
    return mode==Mode::Trot ? cat::gait::Parameters{24,.54,4.0}
                            : cat::gait::Parameters{18,.72,3.1};
}
inline cat::gait::Parameters ParametersForStalk() { return {16,.78,1.8}; }
inline double PawBias(Mode mode,unsigned leg) {
    if(mode!=Mode::Gallop || leg>=4)return 0;
    return leg==2?4.0:leg==3?2.5:0.0;
}
inline Frame Blend(const Frame& a,const Frame& b,double weight) {
    if(weight<=0)return a;
    if(weight>=1)return b;
    auto mix=[&](double x,double y){return x+(y-x)*weight;};
    Frame result;
    result.torsoX=mix(a.torsoX,b.torsoX);result.torsoY=mix(a.torsoY,b.torsoY);
    result.pitchDegrees=mix(a.pitchDegrees,b.pitchDegrees);
    result.spineStretch=mix(a.spineStretch,b.spineStretch);result.tailDegrees=mix(a.tailDegrees,b.tailDegrees);
    result.pivotU=mix(a.pivotU,b.pivotU);result.pivotV=mix(a.pivotV,b.pivotV);
    result.airborneWeight=mix(a.airborneWeight,b.airborneWeight);
    for(unsigned leg=0;leg<result.rootOffsets.size();++leg) {
        result.rootOffsets[leg].x=mix(a.rootOffsets[leg].x,b.rootOffsets[leg].x);
        result.rootOffsets[leg].y=mix(a.rootOffsets[leg].y,b.rootOffsets[leg].y);
    }
    return result;
}
inline Frame Evaluate(double phase,double speed,Mode mode=Mode::Walk,
                      const cat::gait::Feet* actualFeet=nullptr) {
    Frame frame;
    if(!std::isfinite(phase)||!std::isfinite(speed))return frame;
    const double activity=Smooth(std::max(0.0,speed)/12.0);
    if(activity==0)return frame;
    // A faster walk uses a diagonal trot, not fore/hind gallop pairs under
    // an upright body. Hysteresis changes happen where both body poses agree.
    if(mode==Mode::Trot && speed<=35)return Evaluate(phase,speed,Mode::Walk,actualFeet);
    if(mode==Mode::Gallop && speed<=100)return Evaluate(phase,speed,Mode::Trot,actualFeet);
    phase=Wrap(phase);
    const double angle=phase*2*kPi;
    const bool trot=mode==Mode::Trot;

    // The body loads twice per walk/trot stride. Four separate torso bobs
    // made the large head vibrate faster than the visible leg cycle.
    // There is no decorative airborne lift when the physical body is grounded.
    const double load=.5-.5*std::cos(2*angle);
    frame.torsoX=activity*(trot?.18:.10)*std::sin(2*angle);
    frame.torsoY=activity*((trot?.45:.25)+(trot?.60:.30)*load);
    frame.pitchDegrees=activity*(trot?.55:.35)*std::sin(2*angle);

    const auto gait=ParametersFor(mode);
    if(mode==Mode::Gallop) {
        // Fore contacts at 0/.10; hind contacts at .50/.60. The small
        // unsupported intervals are .42-.50 and .92-1.00. This is mesh
        // animation relative to Box2D's root, not a physical jump request.
        const double half=phase-std::floor(phase*2)*.5;
        const double flight=std::clamp((half-.42)/.08,0.,1.);
        frame.airborneWeight=half>.42 ? std::pow(std::sin(kPi*flight),2) : 0;
        const double gather=.5+.5*std::cos(2*kPi*(phase-.20));
        const double compression=.95+.85*gather;
        double clearance=12;
        for(unsigned leg=0;leg<4;++leg) {
            const auto foot=actualFeet?(*actualFeet)[leg]:cat::gait::FootAt(
                phase+cat::gait::LegPhase(leg,gait.duty,gait.sequence),
                gait.stride,gait.duty,gait.lift);
            if(foot.stance)clearance=0;
            else clearance=std::min(clearance,std::max(0.,-foot.y));
        }
        if(actualFeet && clearance==0)frame.airborneWeight=0;
        // Close-to-landing paws can be only a fraction of a pixel clear.
        // A large decorative body hop there would force IK past its reach.
        const double flightLift=std::min(1.6,.55*clearance);
        const double runDrop=compression*(1-frame.airborneWeight)-flightLift*frame.airborneWeight;
        frame.torsoY=activity*runDrop;
        // Retain the lowered running posture through flight. Letting the
        // short far hind root rise while its toe has just left the ground
        // exceeds the painted limb's reach even when the goal is correct.
        frame.torsoY=std::max(frame.torsoY,1.0);
        frame.torsoX=activity*.25*std::sin(angle);
        frame.pitchDegrees=activity*(1.20+.85*std::cos(2*kPi*(phase-.20)));
        frame.spineStretch=-.025*std::cos(2*kPi*(phase-.20));
        frame.tailDegrees=-45;
    }
    for(unsigned leg=0;leg<frame.rootOffsets.size();++leg) {
        const double limbPhase=Wrap(phase+cat::gait::LegPhase(leg,gait.duty,gait.sequence));
        // Sliding shoulder blades and small hip excursions articulate the
        // attachment, rather than scaling the animal or moving its paw goal.
        const bool fore=leg>=2;
        double sine=std::sin(limbPhase*2*kPi),compression=1-std::cos(limbPhase*2*kPi);
        frame.rootOffsets[leg].x=activity*(fore?.48:.24)*sine;
        frame.rootOffsets[leg].y=activity*(fore?.08:.06)*compression;
    }
    if(mode==Mode::Trot)return Blend(Evaluate(phase,speed,Mode::Walk,actualFeet),frame,Smooth((speed-35)/35));
    if(mode==Mode::Gallop)return Blend(Evaluate(phase,speed,Mode::Trot,actualFeet),frame,Smooth((speed-100)/50));
    return frame;
}

inline double SpineWeight(double y,double heightBody) {
    return heightBody>0?Smooth((y/heightBody-.55)/.17):0;
}
// Exact inverse of the lower-spine stretch, after undoing the rigid affine.
inline double UnstretchX(double x,double y,const Frame& frame,double widthBody,double heightBody) {
    const double pivot=widthBody*frame.pivotU;
    return pivot+(x-pivot)/(1+frame.spineStretch*SpineWeight(y,heightBody));
}

// Apply this SAME rigid body transform to the torso image and limb roots.
// Keep ground-referenced paw targets outside this transform. Applying it to
// the completed animal bitmap would reintroduce planted-foot skating.
inline Point BodyPoint(Point point,const Frame& frame,double widthBody,double heightBody) {
    const double angle=frame.pitchDegrees*kPi/180.0;
    const double c=std::cos(angle),s=std::sin(angle);
    const double pivotX=widthBody*frame.pivotU,pivotY=heightBody*frame.pivotV;
    const double x=(point.x-pivotX)*(1+frame.spineStretch*SpineWeight(point.y,heightBody));
    const double y=point.y-pivotY;
    return {pivotX+x*c-y*s+frame.torsoX,pivotY+x*s+y*c+frame.torsoY};
}
inline Point RootPoint(Point root,unsigned leg,const Frame& frame,double widthBody,double heightBody) {
    const auto transformed=BodyPoint(root,frame,widthBody,heightBody);
    if(leg>=frame.rootOffsets.size())return transformed;
    return {transformed.x+frame.rootOffsets[leg].x,transformed.y+frame.rootOffsets[leg].y};
}

} // namespace cat::walk_dynamics
