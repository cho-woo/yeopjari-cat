#pragma once

#include "cat_deformation.hpp"
#include "cat_expression.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace cat::idle_gestures {

struct Sampled {
    deform::Joints joints{};
    expression::Face face{};
};

inline double Ease(double value) {
    const auto t=std::clamp(value,0.,1.);return t*t*(3.-2.*t);
}
inline double Pulse(double local,double start,double rise,double hold,double fall) {
    const auto t=local-start;
    if(t<=0||t>=rise+hold+fall)return 0;
    if(t<rise)return Ease(t/rise);
    if(t<rise+hold)return 1;
    return 1-Ease((t-rise-hold)/fall);
}
inline std::uint32_t Hash(std::uint32_t value) {
    value^=value>>16;value*=0x7feb352du;value^=value>>15;value*=0x846ca68bu;
    return value^(value>>16);
}

// Small connected movements of the supplied sitting drawing. The animation
// clock belongs to the host and stops with menus/drag/hide, not wall time.
// Complete a phrase in four seconds; later phrases vary their timing without
// a new random decision every frame or long motionless intervals.
inline Sampled Sample(std::uint64_t elapsedMs) {
    Sampled result;
    if(elapsedMs<=350)return result; // Let a newly planted pose settle first.
    constexpr double pi=3.14159265358979323846;
    const auto phrase=elapsedMs/4000;
    const auto type=static_cast<unsigned>(phrase%8);
    const auto variation=Hash(static_cast<std::uint32_t>(phrase));
    const double local=static_cast<double>(elapsedMs%4000);
    // 64.6s is a complete number of both breathing and tail periods, so even
    // an indefinitely running actor has no wrap-around jump.
    const double t=static_cast<double>(elapsedMs%64600);
    const double settle=Ease((static_cast<double>(elapsedMs)-350)/900.);

    // Breath only expands the chest locally. Neither the face aspect nor the
    // physical root/ground line changes. Tail bases stay attached to the body.
    result.joints.breath=.20*settle*std::sin(t*2*pi/3800.);
    result.joints.tailRaise=.22*settle*(1-std::cos(t*2*pi/3400.));
    const double start=500.+variation%350;
    const double ear=Pulse(local,start,220,130,420);
    const double paw=Pulse(local,start+180,400,150,520);
    if(type==0||type==6)result.joints.leftEar=.48*ear;
    else if(type==2||type==7)result.joints.rightEar=.43*ear;
    else if(type==4) {
        result.joints.leftEar=.38*ear;
        result.joints.rightEar=.31*Pulse(local,start+110,240,130,420);
    } else if(type==1||type==5)result.joints.frontLeft=.85*paw;
    else if(type==3)result.joints.frontRight=.80*paw;

    // A quiet, tiny inquisitive nod accompanies a paw rather than moving or
    // scaling the whole cat. The original eyes/mouth remain painted textures.
    if(type==1||type==3||type==5)
        result.joints.headNod=(type==3?-.10:.10)*Pulse(local,start+120,460,100,600);
    const double blinkStart=1900.+(variation>>9)%700;
    result.face.blink=Pulse(local,blinkStart,95,35,145);
    if(type==1||type==6)
        result.face.blink=std::max(result.face.blink,Pulse(local,blinkStart+450,85,25,145));
    result.joints=deform::Bounded(result.joints);
    return result;
}

inline bool FineCadence(const Sampled& sample) {
    return sample.face.blink>0||sample.joints.leftEar>0||sample.joints.rightEar>0||
        sample.joints.frontLeft>0||sample.joints.frontRight>0;
}
inline bool NeedsFineCadence(std::uint64_t elapsedMs) {
    // Look one ordinary resting tick ahead so a blink begins at the active
    // cadence; slow tail/chest movement retains the economical resting timer.
    return FineCadence(Sample(elapsedMs))||FineCadence(Sample(elapsedMs+40));
}

} // namespace cat::idle_gestures
