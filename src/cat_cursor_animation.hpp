#pragma once
#include "cat_animation.hpp"

namespace cat::cursor {
enum class AttackKind { Gentle, Strong, Tiptoe, BothPaws };
struct Geometry {
    double width=100,height=110,pawAnchorX=.69;
};
inline std::uint64_t DurationMs(AttackKind kind) {
    return kind==AttackKind::Tiptoe?1400:kind==AttackKind::BothPaws?1550:kind==AttackKind::Gentle?850:650;
}
// The host shares these windows for proximity/contact effects. A window alone
// is never evidence that the cursor was reached.
inline int ContactIndex(AttackKind kind,std::uint64_t elapsed) {
    if(kind==AttackKind::Tiptoe)return elapsed>=610&&elapsed<900?1:0;
    if(kind==AttackKind::BothPaws)return elapsed>=520&&elapsed<760?1:elapsed>=930&&elapsed<1160?2:0;
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
    if(kind==AttackKind::Tiptoe||kind==AttackKind::BothPaws) {
        const double end=static_cast<double>(DurationMs(kind));
        const double focus=animation::SmoothStep(t/180)*(1-animation::SmoothStep((t-(end-220))/220));
        const double rise=animation::SmoothStep(t/430)*(1-animation::SmoothStep((t-(end-390))/390));
        const double reach=kind==AttackKind::Tiptoe?Pulse(t,170,700,1230):std::max(Pulse(t,100,590,860),Pulse(t,800,1040,1430));
        p.cursorReach={rise,reach,reach,std::clamp(targetX/g.width,-1.0,1.0)};
        p.face.wide=.10*focus;p.face.brow=.08*focus;
        p.joints.leftEar=.10*focus;p.joints.rightEar=.06*focus;
        p.joints.tailRaise=.12*focus*(.5+.5*std::sin(t*.007));
        p.idleActing=cursor_rig::Active(p.cursorReach);
        return p;
    }
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
// The real Box2D launch/contact owns every pixel of world translation.
// This same source4 body remains selected through wind-up, flight and recovery.
inline animation::Pose HopPose(std::uint64_t elapsed,bool launched,bool grounded,double velocityY,
                               std::uint64_t contactElapsed,int direction,double targetDx,double targetHeight,
                               Geometry geometry={}) {
    (void)targetHeight;
    animation::Pose p;p.flip=direction<0;p.groundContact=grounded;
    const auto g=Bounded(geometry);
    const double aim=std::clamp((p.flip?-1:1)*targetDx/g.width,-1.0,1.0);
    if(!launched) {
        const double t=std::clamp(elapsed/280.0,0.0,1.0);
        p.cursorReach={.18*animation::SmoothStep(t),.20*animation::SmoothStep(t),.20*animation::SmoothStep(t),aim};
        p.joints.crouch=.22*std::sin(t*animation::kPi);
    } else if(!grounded) {
        // Velocity varies continuously through the apex; no atlas-frame jump.
        const double rising=animation::SmoothStep((velocityY+35)/150);
        const double takeoff=animation::SmoothStep((static_cast<double>(elapsed)-280)/180);
        const double reach=.20+(.32+.50*rising-.20)*takeoff;
        p.cursorReach={.18,reach,reach,aim};
        p.joints.tailRaise=.13;
        p.face.wide=.16;
    } else {
        const double t=std::clamp(contactElapsed/520.0,0.0,1.0);
        const double settle=1-animation::SmoothStep(t);
        p.cursorReach={.18*settle,.32*settle,.32*settle,aim};
        p.joints.crouch=.15*std::sin(t*animation::kPi);
        p.face.blink=.45*Pulse(contactElapsed,50,160,300);
    }
    p.idleActing=cursor_rig::Active(p.cursorReach);
    return p;
}
inline animation::Pose GroomPose(std::uint64_t elapsed,int direction=1,std::uint64_t totalDurationMs=0) {
    animation::Pose p;p.flip=direction<0;
    const std::uint64_t phrase=totalDurationMs?std::min<std::uint64_t>(4200,totalDurationMs):4200;
    if(totalDurationMs&&(phrase==0||elapsed>=(totalDurationMs/phrase)*phrase))return p;
    const double t=static_cast<double>(elapsed%phrase)*4200.0/phrase;
    const double held=animation::SmoothStep((t-180)/500)*(1-animation::SmoothStep((t-3300)/650));
    const double wipe=std::sin(std::clamp((t-780)/2350,0.0,1.0)*animation::kPi*4);
    p.cursorReach={0,0,held,.65*wipe,1};
    p.idleActing=held>0;
    p.face.blink=.82*held*(.60+.40*std::max(0.0,wipe));
    p.joints.tailRaise=.05*held;
    return p;
}
constexpr std::uint64_t kBallPawMs=1050;
inline int BallPawContactIndex(std::uint64_t elapsed) {return elapsed>=330&&elapsed<660?1:0;}
inline animation::Pose BallPawPose(std::uint64_t elapsed,int direction,double targetDx,double targetHeight,Geometry geometry={}) {
    (void)targetHeight;
    animation::Pose p;p.flip=direction<0;
    if(elapsed>=kBallPawMs)return p;
    const auto g=Bounded(geometry);const double reach=Pulse(elapsed,100,490,970);
    p.cursorReach={0,0,reach,std::clamp((p.flip?-1:1)*targetDx/g.width,-1.0,1.0),0,1};
    p.idleActing=reach>0;
    p.face.wide=.10*reach;return p;
}
inline deform::Point PawPosition(const animation::Pose& pose,Geometry geometry={}) {
    if(pose.spritePawsValid) {
        const auto point=pose.spritePaws[1];
        return {(pose.flip?-1:1)*point.x,point.y};
    }
    const auto g=Bounded(geometry);
    if(cursor_rig::Active(pose.cursorReach)) {
        const auto point=cursor_rig::PawUV(pose.cursorReach,1);
        return {(pose.flip?-1:1)*(point.x-.5)*g.width,(1-point.y)*g.height};
    }
    // Match production's cached 1/32 control quantization before contact math.
    auto joints=pose.joints;
    joints.pawReachX=std::round(joints.pawReachX*32)/32;
    joints.pawReachY=std::round(joints.pawReachY*32)/32;
    joints.pawAnchorX=std::round(joints.pawAnchorX*32)/32;
    const auto point=deform::SittingPoint({g.pawAnchorX,.96},joints);
    return {(pose.flip?-1:1)*(point.x-.5)*g.width,(1-point.y)*g.height};
}
inline std::array<deform::Point,2> PawPositions(const animation::Pose& pose,Geometry geometry={}) {
    if(pose.spritePawsValid) {
        auto points=pose.spritePaws;
        for(auto& point:points)point.x*=pose.flip?-1:1;
        return points;
    }
    const auto g=Bounded(geometry);
    if(cursor_rig::Active(pose.cursorReach)) {
        std::array<deform::Point,2> paws{};
        for(int i=0;i<2;++i){const auto point=cursor_rig::PawUV(pose.cursorReach,i);
            paws[i]={(pose.flip?-1:1)*(point.x-.5)*g.width,(1-point.y)*g.height};}
        return paws;
    }
    const auto other=deform::SittingPoint({.425,.958},pose.joints);
    return {{PawPosition(pose,g),{(pose.flip?-1:1)*(other.x-.5)*g.width,(1-other.y)*g.height}}};
}
inline bool NearPaw(const animation::Pose& pose,Geometry geometry,double targetDx,double targetHeight,double radius=6) {
    if(!std::isfinite(targetDx)||!std::isfinite(targetHeight)||!std::isfinite(radius)||radius<0)return false;
    const auto paw=PawPosition(pose,geometry);
    return std::hypot(targetDx-paw.x,targetHeight-paw.y)<=radius;
}
} // namespace cat::cursor
