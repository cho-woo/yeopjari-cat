#include "cat_animation.hpp"
#include "cat_companion.hpp"
#include "cat_physics.hpp"
#include <cassert>
#include <cmath>
#include <iostream>

int main() {
    unsigned contacts=0,starts=0;
    for(double dt:{1./25,1./60,1./120})for(int facing:{-1,1})for(double growth:{.62,1.})for(double width:{.82,1.26}) {
        cat::physics::Controller physics;physics.Reset(800,0,1600);
        cat::cream::Motion motion;const double scale=growth*width;
        bool moved=false;
        for(unsigned n=0;n<unsigned(8/dt);++n) {
            const double time=n*dt;const bool walk=time>=1 && time<5;
            const auto physical=physics.CurrentPose();
            const bool ready=motion.Prepare(dt,walk||std::abs(physical.velocityX)>2,true,physical.grounded);
            physics.SetTargetVelocity(ready&&walk?facing*22:0);
            const auto before=physics.RenderPose();const auto feet=motion.Feet();
            physics.Update(dt);const auto after=physics.RenderPose();
            const double delta=after.relativeTravelX-before.relativeTravelX;
            motion.Travel(delta/scale,dt,after.grounded);
            if(!ready && !moved)assert(std::abs(after.x-800)<1e-4);
            if(std::abs(delta)>1e-5) {
                if(!moved){assert(motion.Sit()<.003);++starts;moved=true;}
                for(unsigned leg=0;leg<4;++leg){const auto& next=motion.Feet()[leg];
                    if(feet[leg].stance&&next.stance&&feet[leg].contact==next.contact) {
                        assert(std::abs(delta+facing*(next.x-feet[leg].x)*scale)<1e-5);++contacts;
                    }
                }
            }
            auto pose=cat::animation::Evaluate(walk?cat::animation::Motion::Walk:cat::animation::Motion::Idle,n*40,n*40,facing);
            assert(cat::animation::ApplyCreamLocomotion(pose,walk?cat::animation::Motion::Walk:cat::animation::Motion::Idle,motion));
            assert(pose.creamRig && pose.spriteClip==cat::sprite_sequence::Clip::None && pose.x==0 && pose.lift==0);
        }
        assert(moved&&motion.Sit()==1&&!motion.Active());
        assert(physics.Jump(55));double apex=0;unsigned landings=0;
        for(unsigned n=0;n<unsigned(3/dt);++n){
            physics.Update(dt);const auto actual=physics.CurrentPose();apex=std::max(apex,actual.lift);
            motion.Prepare(dt,false,true,actual.grounded);
            if(!actual.grounded){auto pose=cat::animation::AirbornePose(actual.velocityY,facing);assert(!cat::animation::ApplyCreamLocomotion(pose,cat::animation::Motion::Walk,motion));}
            if(actual.landed)++landings;
        }
        assert(apex>45 && apex<65 && landings==1);
    }
    assert(starts==24 && contacts>4000);
    for(int old=0;old<cat::kLegacyCharacterCount;++old){
        auto before=cat::companion::NewState(cat::ToUnix({2025,1,1}));
        before.preferences={static_cast<cat::Character>(old),cat::Outfit::Scarf};before.sniffs=17;
        cat::companion::State after;std::string error;
        assert(cat::companion::Parse(cat::companion::Serialize(before),after,error));
        assert(after.preferences.character==cat::Character::Cream);
        before.preferences.character=cat::Character::Cream;
        assert(cat::companion::Serialize(before)==cat::companion::Serialize(after));
    }
    std::cout<<"Cream/Box2D: "<<contacts<<" planted contacts; start, stop, growth, 25/60/120 Hz, both directions, jump and saved character migration passed\n";
}
