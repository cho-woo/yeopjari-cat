#pragma once

#include "cat_ball_physics.hpp"
#include "cat_toy.hpp"

namespace cat::toy {
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
    const int desired=std::abs(delta)>4?(delta<0?-1:1):result.direction;
    if(desired!=result.direction) {
        if(std::abs(catVelocity)>.5){result.braking=true;return result;}
        result.direction=desired;
    }
    const double prediction=std::clamp(ball.velocity*.15,-12.,12.);
    const double predicted=ball.x+prediction;
    double target=predicted-result.direction*standOff;
    const double alternate=predicted+result.direction*standOff;
    if(std::abs(ClampCenter(target,low,high)-target)>standOff*.5 && alternate>=low&&alternate<=high) {
        if(std::abs(catVelocity)>.5){result.braking=true;return result;}
        result.direction=-result.direction;target=alternate;
    }
    result.target=ClampCenter(target,low,high);
    const double separation=result.direction*delta;
    // Arrival must use the source paw's stand-off, not an entire ball-radius
    // band that lets the body stop with the ball underneath the planted paw.
    const bool close=separation>=std::max(4.,standOff-3)&&separation<=standOff+3;
    result.arrived=close&&std::abs(catVelocity)<=2&&std::abs(ball.velocity)<=18&&ball.lift<=8;
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
