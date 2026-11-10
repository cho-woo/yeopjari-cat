#include "../src/cat_ball_play.hpp"
#include "../src/cat_cursor_animation.hpp"
#include "../src/cat_physics.hpp"
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
unsigned checks=0;
#define CHECK(x) do{++checks;if(!(x))throw std::runtime_error(std::to_string(__LINE__)+": " #x);}while(false)
using namespace cat::toy;
void ContactNeedsTheActualCircleAndAVisibleStroke() {
    BallPose ball;ball.x=450;ball.lift=0;
    bool consumed=false;
    CHECK(!BallPawContactAt(329,consumed,438,12,ball,1,true)&&!consumed);
    CHECK(!BallPawContactAt(400,consumed,433.9,12,ball,1,true)&&!consumed);
    CHECK(!BallPawContactAt(400,consumed,438,12,ball,1,false)&&!consumed);
    CHECK(BallPawContactAt(400,consumed,438,12,ball,1,true)&&consumed);
    CHECK(!BallPawContactAt(401,consumed,438,12,ball,1,true));
    consumed=false;ball.lift=40;
    CHECK(!BallPawContactAt(400,consumed,438,52,ball,1,true)&&!consumed);
    ball.lift=0;CHECK(!BallPawContactAt(660,consumed,438,12,ball,1,true)&&consumed);
    consumed=false;CHECK(!BallPawContactAt(450,consumed,std::numeric_limits<double>::quiet_NaN(),12,ball,1,true));
}
void ReversalBrakesBeforeChangingTheApproachSide() {
    BallPose ball;ball.x=350;ball.velocity=-20;
    const auto braking=ChaseBall(400,65,ball,44,1,96,960,81.6);
    CHECK(braking.braking&&braking.velocity==0&&braking.direction==1&&braking.target==400);
    const auto turned=ChaseBall(400,0,ball,44,1,96,960,81.6);
    CHECK(!turned.braking&&turned.direction==-1&&turned.target==391&&turned.velocity<0);
    ball.x=500;ball.velocity=40;
    const auto ahead=ChaseBall(400,40,ball,44,1,96,960,81.6);
    CHECK(ahead.target==462&&ahead.velocity>0&&ahead.velocity<=81.6);
    for(const double x:{60.,96.,400.,960.,996.}) {
        ball.x=x;ball.velocity=0;const auto plan=ChaseBall(400,0,ball,44,1,96,960,81.6);
        CHECK(std::isfinite(plan.velocity)&&plan.target>=96&&plan.target<=960&&std::abs(plan.velocity)<=81.6);
    }
}
void RealPawsImpulseTheRollingBallAndRepeatWithoutTeleporting() {
    for(int fps:{30,60,120})for(double growth:{.55,1.,1.1}) {
        constexpr double origin=120;
        const cat::cursor::Geometry geometry{329./367*110,366./367*110,.69};
        const auto reach=cat::cursor::BallPawPose(490,1,45,12/growth,geometry);
        const double standOff=std::max(16.,cat::cursor::PawPosition(reach,geometry).x*growth+12+4*growth-4);
        cat::physics::Controller body;body.Reset(400,96,1184);body.SetCeiling(700);
        PhysicsBall ball(origin+505);int side=1;bool striking=false,consumed=false;
        unsigned kicks=0;std::uint64_t strokeStart=0;double previousX=400;
        for(int frame=1;frame<=fps*25;++frame) {
            const double dt=1./fps;const auto ticks=static_cast<std::uint64_t>(std::lround(frame*1000./fps));
            ball.Advance(dt,origin+60,origin+1220);
            auto physical=body.CurrentPose();
            const auto chase=ChaseBall(origin+physical.x,physical.velocityX,ball.CurrentPose(),standOff,side,
                origin+96,origin+1184,81.6);
            if(!striking) {
                side=chase.direction;
                if(chase.arrived){striking=true;consumed=false;strokeStart=ticks;}
            }
            body.SetTargetVelocity(striking?0:chase.velocity);body.Update(dt);physical=body.CurrentPose();
            CHECK(std::isfinite(physical.x)&&std::abs(physical.x-previousX)<=82*dt+1.5);
            previousX=physical.x;
            if(!striking)continue;
            const auto elapsed=ticks-strokeStart;
            const auto pose=cat::cursor::BallPawPose(elapsed,side,(ball.x-origin-physical.x)/growth,
                (ball.lift+12)/growth,geometry);
            const auto paw=cat::cursor::PawPosition(pose,geometry);
            const double pawX=origin+physical.x+paw.x*growth,pawY=paw.y*growth;
            const auto before=ball.CurrentPose();
            if(BallPawContactAt(elapsed,consumed,pawX,pawY,before,growth,physical.grounded&&std::abs(physical.velocityX)<=2)) {
                CHECK(std::hypot(before.x-pawX,before.lift+12-pawY)<=12+4*growth);
                ball.Kick(side,58,28);++kicks;
                CHECK(ball.x==before.x&&ball.lift==before.lift);
                CHECK(!BallPawContactAt(elapsed,consumed,pawX,pawY,ball.CurrentPose(),growth,true));
            }
            if(elapsed>=1050)striking=false;
        }
        CHECK(kicks>=3);
    }
}
}
int main(){try{ContactNeedsTheActualCircleAndAVisibleStroke();ReversalBrakesBeforeChangingTheApproachSide();
    RealPawsImpulseTheRollingBallAndRepeatWithoutTeleporting();std::cout<<"Ball play: "<<checks<<" checks passed\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
