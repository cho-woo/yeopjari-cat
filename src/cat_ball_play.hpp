#pragma once

#include "cat_ball_physics.hpp"
#include "cat_toy.hpp"
#include "ball_pounce.generated.hpp"
#include "video_action_contacts.generated.hpp"

namespace cat::toy {
inline cat::deform::Point PlayPaw(const Session& session,unsigned coat,unsigned frame) {
    return session.BallPounce()?cat::ball_movie::Paw(coat,frame):cat::video_actions::BallPaws(coat,frame)[1];
}
inline unsigned PlayStandOffFrame(const Session& session,unsigned coat) {
    return session.BallPounce()?cat::ball_movie::StandOffFrame(coat):cat::video_actions::BallStandOffFrame(coat);
}
inline unsigned PlayContactStroke(const Session& session,unsigned coat) {
    return session.BallPounce()&&session.elapsedMs>=(coat==8?2600:2900)?1:0;
}
inline unsigned PlayContactStart(const Session& session,unsigned coat) {
    if(session.BallPounce())return PlayContactStroke(session,coat)?(coat==8?2750:3083):cat::ball_movie::ContactStart(coat);
    return cat::video_actions::BallContactStartMs(coat);
}
inline unsigned PlayContactEnd(const Session& session,unsigned coat) {
    if(session.BallPounce())return PlayContactStroke(session,coat)?(coat==8?3375:3542):cat::ball_movie::ContactEnd(coat);
    return cat::video_actions::BallContactEndMs(coat);
}
inline double PlayContactStrength(const Session& session,unsigned coat) {
    // Pounce catches/nudges first, then the authored low sweep releases it.
    // These are two distinct visible strokes, each with one real contact.
    return session.BallPounce()&&!PlayContactStroke(session,coat)?.065:session.BallStrength();
}
struct BallStrike {
    bool contact=false;
    double nx=0,ny=0,pawVx=0,pawVy=0,closingSpeed=0,distance=0;
};

// Both actors observe the interpolated paw and ball that are actually drawn.
// A swept circle catches a fast downswing between 24fps source poses, but a
// timer, nearby body, stationary overlap or retracting paw never supplies force.
class BallContactTracker {
public:
    void Reset(){*this={};}
    BallStrike Observe(std::uint64_t serial,std::uint64_t now,std::uint64_t age,
            double px,double py,const BallPose& ball,double growth,int facing,
            bool available,std::uint64_t first,std::uint64_t last) {
        BallStrike hit;
        const bool finite=std::isfinite(px)&&std::isfinite(py)&&std::isfinite(ball.x)&&
            std::isfinite(ball.lift)&&std::isfinite(growth)&&growth>0;
        if(serial!=serial_){Reset();serial_=serial;}
        if(!available||!finite){had_=false;return hit;}
        const double bx=ball.x,by=ball.lift+PhysicsBall::Radius;
        const double seconds=now>time_?(now-time_)/1000.:0;
        if(had_&&!consumed_&&age>=first&&age<last&&seconds>0&&seconds<=.1) {
            const double rx=bx_-px_,ry=by_-py_;
            const double dx=(bx-px)-rx,dy=(by-py)-ry;
            const double radius=PhysicsBall::Radius+4*growth;
            const double a=dx*dx+dy*dy,b=2*(rx*dx+ry*dy),c=rx*rx+ry*ry-radius*radius;
            double t=0;bool overlaps=c<=0;
            if(!overlaps&&a>1e-10) {
                const double discriminant=b*b-4*a*c;
                if(discriminant>=0){t=(-b-std::sqrt(discriminant))/(2*a);overlaps=t>=0&&t<=1;}
            }
            if(overlaps) {
                const double hx=rx+dx*t,hy=ry+dy*t,d=std::hypot(hx,hy);
                if(d>.01) {
                    hit.nx=hx/d;hit.ny=hy/d;
                    hit.pawVx=(px-px_)/seconds;hit.pawVy=(py-py_)/seconds;
                    const double relativeVx=-dx/seconds,relativeVy=-dy/seconds;
                    hit.closingSpeed=relativeVx*hit.nx+relativeVy*hit.ny;
                    if(hit.closingSpeed>.75&&hit.nx*(facing<0?-1:1)>.05) {
                        hit.contact=consumed_=true;hit.distance=d;
                    }
                }
            }
        }
        had_=true;time_=now;px_=px;py_=py;bx_=bx;by_=by;
        return hit;
    }
    bool Consumed()const{return consumed_;}
private:
    bool had_=false,consumed_=false;
    std::uint64_t serial_=0,time_=0;
    double px_=0,py_=0,bx_=0,by_=0;
};

inline double PawStandOff(double pawX,double pawHeight,double growth) {
    const double dy=pawHeight-PhysicsBall::Radius,r=PhysicsBall::Radius+2*growth;
    return std::max(8.,pawX+std::sqrt(std::max(0.,r*r-dy*dy))*.7);
}
struct BallChase {
    double target=0,velocity=0;
    int direction=1;
    bool arrived=false,braking=false;
};
inline BallChase ChaseBall(double catX,double catVelocity,const BallPose& ball,double standOff,int side,
                           double low,double high,double maxSpeed) {
    BallChase result;result.target=catX;result.direction=side<0?-1:1;
    if(!std::isfinite(catX)||!std::isfinite(catVelocity)||!std::isfinite(ball.x)||!std::isfinite(ball.velocity)||
            !std::isfinite(ball.lift)||!std::isfinite(standOff)||!std::isfinite(maxSpeed)||
            !std::isfinite(low)||!std::isfinite(high)||standOff<=0||maxSpeed<=0)return result;
    const double delta=ball.x-catX;
    const int previousSide=result.direction;
    result.direction=std::abs(delta)>4?(delta<0?-1:1):previousSide;
    const double prediction=std::clamp(ball.velocity*.15,-12.,12.);
    const double predicted=ball.x+prediction;
    double target=predicted-result.direction*standOff;
    const double alternate=predicted+result.direction*standOff;
    if(std::abs(ClampCenter(target,low,high)-target)>2. && alternate>=low&&alternate<=high) {
        result.direction=-result.direction;target=alternate;
    }
    // Resolve the reachable side BEFORE deciding to brake. Near a wall the
    // cat may need to make room on the opposite side of the ball. Braking on
    // the raw ball direction first repeatedly restarted planted turns.
    if(result.direction!=previousSide&&std::abs(catVelocity)>.5) {
        result.direction=previousSide;result.braking=true;return result;
    }
    result.target=ClampCenter(target,low,high);
    const double separation=result.direction*delta;
    // Arrival must use the source paw's stand-off, not an entire ball-radius
    // band that lets the body stop with the ball underneath the planted paw.
    const bool close=separation>=std::max(4.,standOff-3)&&separation<=standOff+3;
    result.arrived=close&&std::abs(catVelocity)<=2&&std::abs(ball.velocity)<=2.5&&ball.lift<=8;
    if(result.arrived || (close&&(result.target-catX)*result.direction<0))return result;
    result.velocity=ApproachVelocity(result.target-catX,maxSpeed,300);
    return result;
}

inline bool BallPawContactAt(std::uint64_t elapsed,bool& consumed,double pawX,double pawHeight,
                             const BallPose& ball,double growth,bool available,
                             std::uint64_t contactStart=kPawContactMs,std::uint64_t contactEnd=kPawContactEndMs) {
    if(consumed||!available||!std::isfinite(pawX)||!std::isfinite(pawHeight)||!std::isfinite(ball.x)||
            !std::isfinite(ball.lift)||!std::isfinite(growth)||growth<=0)return false;
    if(elapsed>=contactEnd){consumed=true;return false;}
    if(elapsed<contactStart||ball.lift>24*growth)return false;
    const double distance=std::hypot(ball.x-pawX,ball.lift+PhysicsBall::Radius-pawHeight);
    if(distance>PhysicsBall::Radius+4*growth)return false;
    consumed=true;return true;
}
} // namespace cat::toy
