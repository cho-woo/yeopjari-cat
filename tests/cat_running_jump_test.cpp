#include "../src/cat_running_jump.hpp"
#include "../src/cat_motion_gate.hpp"
#include <iostream>
#include <stdexcept>
#include <string>

namespace {
unsigned checks=0;
#define CHECK(x) do{++checks;if(!(x))throw std::runtime_error(std::to_string(__LINE__)+": " #x);}while(false)
using namespace cat;
void PlanningUsesActualMotion() {
    physics::Pose p;p.velocityX=81.6;
    const auto walk=running_jump::Begin(animation::Motion::Walk,p,1,3000);
    CHECK(walk.active&&walk.velocityX==81.6&&walk.resume==animation::Motion::Walk&&walk.baseSpeed==81.6);
    p.velocityX=-177.23;
    const auto run=running_jump::Begin(animation::Motion::Zoomies,p,1.32,900);
    CHECK(run.active&&run.velocityX==p.velocityX&&run.resume==animation::Motion::Zoomies&&run.resumeDurationMs==1200);
    CHECK(std::abs(run.baseSpeed*1.32-177.23)<1e-8);
    CHECK(!running_jump::Begin(animation::Motion::Eat,p).active);
    CHECK(running_jump::Begin(animation::Motion::Idle,p).resume==animation::Motion::Idle);
    p.onPlatform=true;CHECK(!running_jump::Begin(animation::Motion::Walk,p).active);
    p.onPlatform=false;p.grounded=false;CHECK(!running_jump::Begin(animation::Motion::Walk,p).active);
    p.grounded=true;p.velocityX=0;CHECK(!running_jump::Begin(animation::Motion::Walk,p).active);
}
void RealBox2DArcKeepsMomentum() {
    for(unsigned fps:{30,60,120})for(double speed:{-177.23,-81.6,81.6,177.23}) {
        physics::Controller body;body.Reset(1000,96,3000);body.SetCeiling(700);
        body.SetTargetVelocity(speed);
        for(unsigned i=0;i<fps*2;++i)body.Update(1./fps);
        const auto start=body.CurrentPose();
        CHECK(std::abs(start.velocityX-speed)<.02);
        CHECK(body.JumpWithMomentum(55));
        const auto launched=body.CurrentPose();
        CHECK(launched.x==start.x&&std::abs(launched.velocityX-start.velocityX)<1e-8);
        CHECK(!body.JumpWithMomentum(55));
        double time=0,peak=0,airDistance=0;bool landed=false;
        for(unsigned i=1;i<fps*2;++i) {
            // Even the opposite arrow must not apply a second air motor.
            body.SetTargetVelocity(-speed);body.Update(1./fps);time=i/static_cast<double>(fps);
            const auto pose=body.CurrentPose();
            CHECK(std::isfinite(pose.x)&&std::isfinite(pose.lift));
            peak=std::max(peak,pose.lift);
            if(pose.landed){landed=true;airDistance=pose.x-start.x;break;}
            CHECK(std::abs(pose.velocityX-launched.velocityX)<.02);
            const double expectedX=start.x+launched.velocityX*time;
            CHECK(std::abs(pose.x-expectedX)<std::abs(speed)/60+.05);
            const double expectedY=launched.velocityY*time-450*time*time;
            CHECK(std::abs(pose.lift-expectedY)<6);
            CHECK(pose.relativeTravelX==start.relativeTravelX);
        }
        CHECK(landed&&peak>52&&peak<56);
        CHECK(airDistance*speed>0&&std::abs(airDistance)>std::abs(speed)*.6);
    }
}
}
int main(){try{PlanningUsesActualMotion();RealBox2DArcKeepsMomentum();std::cout<<"Running jump: "<<checks<<" checks passed\n";return 0;}
catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
