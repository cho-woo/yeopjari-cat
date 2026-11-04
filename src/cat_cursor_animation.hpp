#pragma once
#include "cat_animation.hpp"

namespace cat::cursor {
enum class AttackKind { Gentle, Strong };
struct Geometry {
    double width=100,height=110,pawAnchorX=.69;
};
inline std::uint64_t DurationMs(AttackKind kind) {return kind==AttackKind::Gentle ? 850 : 650;}
// The host shares these windows for proximity/contact effects. A window alone
// is never evidence that the cursor was reached.
inline int ContactIndex(AttackKind kind,std::uint64_t elapsed) {
    if(kind==AttackKind::Strong)return elapsed>=260&&elapsed<440 ? 1 : 0;
    return elapsed>=220&&elapsed<360 ? 1 : elapsed>=500&&elapsed<650 ? 2 : 0;
}
inline double Pulse(double t,double begin,double peak,double end) {
    return animation::SmoothStep((t-begin)/(peak-begin))*(1-animation::SmoothStep((t-peak)/(end-peak)));
}
inline Geometry Bounded(Geometry g) {
    g.width=std::isfinite(g.width)?std::clamp(g.width,20.0,250.0):100;
    g.height=std::isfinite(g.height)?std::clamp(g.height,20.0,200.0):110;
    g.pawAnchorX=std::isfinite(g.pawAnchorX)?std::clamp(g.pawAnchorX,.60,.85):.69;
    return g;
}
inline animation::Pose AttackPose(AttackKind kind,std::uint64_t elapsed,int direction,
                                  double targetDx,double targetHeight,Geometry geometry={}) {
    animation::Pose p;
    p.flip=direction<0;
    if(elapsed>=DurationMs(kind))return p;
    const auto g=Bounded(geometry);
    const double facing=p.flip?-1.0:1.0;
    const double targetX=std::isfinite(targetDx)?facing*targetDx:0;
    const double targetY=std::isfinite(targetHeight)?targetHeight:0;
    const double t=static_cast<double>(elapsed);
    const double amount=kind==AttackKind::Gentle ?
        std::max(Pulse(t,70,270,430),Pulse(t,430,560,810)) :
        animation::SmoothStep((t-200)/90)*(1-animation::SmoothStep((t-400)/220));
    const double windup=kind==AttackKind::Strong ? Pulse(t,20,150,220) : 0;
    // Aim from the actual paw centre. Overshooting the reachable foreleg is
    // clamped locally, so a high/far pointer produces a miss, not a floating cat.
    double x=(targetX-(g.pawAnchorX-.5)*g.width)/(.10*g.width);
    double y=(targetY-.04*g.height)/(.14*g.height);
    x=std::clamp(x,-1.0,1.0);y=std::clamp(y,kind==AttackKind::Gentle?.14:.30,1.0);
    const double total=std::abs(x)+y;
    if(total>1){x/=total;y/=total;}
    const double force=kind==AttackKind::Gentle?.72:1.0;
    p.joints.pawReachX=x*amount*force-.28*windup;
    p.joints.pawReachY=y*amount*force+.12*windup;
    p.joints.pawAnchorX=g.pawAnchorX;
    // Eyes stay on the same head. Alert ears do not fold or scale the skull.
    const double focus=animation::SmoothStep(t/100)*(1-animation::SmoothStep((t-(DurationMs(kind)-150))/150));
    p.face.wide=(kind==AttackKind::Gentle?.12:.28)*focus;
    p.face.brow=(kind==AttackKind::Gentle?.10:-.25)*focus;
    return p;
}
inline deform::Point PawPosition(const animation::Pose& pose,Geometry geometry={}) {
    const auto g=Bounded(geometry);
    // Match production's cached 1/32 control quantization before contact math.
    auto joints=pose.joints;
    joints.pawReachX=std::round(joints.pawReachX*32)/32;
    joints.pawReachY=std::round(joints.pawReachY*32)/32;
    joints.pawAnchorX=std::round(joints.pawAnchorX*32)/32;
    const auto point=deform::SittingPoint({g.pawAnchorX,.96},joints);
    return {(pose.flip?-1:1)*(point.x-.5)*g.width,(1-point.y)*g.height};
}
inline bool NearPaw(const animation::Pose& pose,Geometry geometry,double targetDx,double targetHeight,double radius=6) {
    if(!std::isfinite(targetDx)||!std::isfinite(targetHeight)||!std::isfinite(radius)||radius<0)return false;
    const auto paw=PawPosition(pose,geometry);
    return std::hypot(targetDx-paw.x,targetHeight-paw.y)<=radius;
}
} // namespace cat::cursor
