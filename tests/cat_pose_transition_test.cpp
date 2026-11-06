#include "cat_pose_transition.hpp"
#include <cassert>
#include <iostream>

using namespace cat::animation;
int main() {
    PoseTransition transition;
    auto loaf=Evaluate(Motion::Loaf,6000,6000,1);
    auto idle=Evaluate(Motion::Idle,0,6000,1); idle.joints={};
    assert(loaf.joints.crouch==1);
    assert(transition.Apply(loaf,1000,1).joints.crouch==1);
    assert(transition.Apply(idle,1016,2).joints.crouch==1);
    double last=1;
    for(unsigned elapsed=1;elapsed<=180;++elapsed) {
        const auto pose=transition.Apply(idle,1016+elapsed,2);
        assert(pose.frame==4 && pose.blendFrame==-1 && pose.angle==0 && pose.lift==0);
        assert(pose.joints.crouch<=last && last-pose.joints.crouch<.009);
        assert(cat::deform::SittingPoint({.2,1},pose.joints).y==1);
        const auto a=cat::deform::SittingPoint({.40,.47},pose.joints);
        const auto b=cat::deform::SittingPoint({.70,.45},pose.joints);
        assert(std::abs(std::hypot(a.x-b.x,a.y-b.y)-std::hypot(.3,.02))<1e-12);
        last=pose.joints.crouch;
    }
    assert(last==0);

    // Interrupt midway without jumping back to either requested pose.
    transition.Reset(); transition.Apply(loaf,0,1); transition.Apply(idle,16,2);
    const auto middle=transition.Apply(idle,106,2);
    assert(std::abs(middle.joints.crouch-.5)<1e-12);
    auto changed=transition.Apply(loaf,122,3);
    assert(changed.joints.crouch==middle.joints.crouch);
    assert(transition.Apply(loaf,122,3).joints.crouch==changed.joints.crouch);
    transition.Pause(5000);
    assert(transition.Apply(loaf,5122,3).joints.crouch==changed.joints.crouch);
    assert(transition.Apply(loaf,5302,3).joints.crouch==1);

    // Every unsupported rig, identity, facing or contact action bypasses it.
    for(unsigned kind=0;kind<8;++kind) {
        transition.Reset(); transition.Apply(loaf,0,1);
        auto target=idle;
        if(kind==0)target.frame=16;
        if(kind==1)target.bellyRig=true;
        if(kind==2)target.flip=true;
        if(kind==3)target.groundContact=false;
        if(kind==4)target.joints.pawReachX=.5;
        if(kind==5)target.angle=2;
        const auto result=transition.Apply(target,16,2,kind==6?1:0,kind!=7);
        assert(result.joints.crouch==0 && result.joints.pawReachX==target.joints.pawReachX);
    }
    // Moving target joints are followed exactly once the transition ends.
    transition.Reset(); transition.Apply(loaf,0,1); transition.Apply(idle,16,2);
    for(unsigned t=17;t<600;++t) {
        auto target=Evaluate(Motion::Watch,t,t,1);
        const auto result=transition.Apply(target,t,2);
        if(t>=196)assert(result.joints.headNod==target.joints.headNod);
    }
    transition.Reset();transition.Apply(loaf,4000,1);
    auto balanced=idle;balanced.joints.headNod=.35;
    const auto late=transition.Apply(balanced,5200,2,0,true,4180);
    assert(late.joints.headNod==balanced.joints.headNod&&late.joints.crouch==balanced.joints.crouch);
    // An action still inside its real transition follows elapsed action time.
    transition.Reset();transition.Apply(loaf,4000,1);
    const auto delayed=transition.Apply(idle,4270,2,0,true,4180);
    assert(std::abs(delayed.joints.crouch-loaf.joints.crouch*.5)<1e-12);
    std::cout << "Canonical posture transitions: continuity, interrupted blending, pause, contact bypass, real action start and fixed head/feet passed\n";
}
