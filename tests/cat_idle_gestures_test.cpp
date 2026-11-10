#include "../src/cat_animation.hpp"
#include "../src/cat_species.hpp"
#include <array>
#include <iostream>
#include <stdexcept>

namespace {
unsigned checks=0;
#define CHECK(x) do{++checks;if(!(x))throw std::runtime_error(#x);}while(false)
using cat::animation::Motion;
using cat::animation::Pose;
std::array<double,10> Values(const cat::idle_gestures::Sampled& p) {
    return {{p.joints.leftEar,p.joints.rightEar,p.joints.frontLeft,p.joints.frontRight,
        p.joints.headNod,p.joints.tailRaise,p.joints.breath,p.face.blink,p.face.smile,p.face.mouth}};
}
void CompleteActingWithoutMovingTheRoot() {
    bool blink=false,partialBlink=false,leftEar=false,rightEar=false,leftPaw=false,rightPaw=false,tail=false;
    unsigned economical=0,precise=0;
    for(std::uint64_t t=0;t<20000;t+=16) {
        Pose pose;pose.spriteClip=cat::sprite_sequence::Clip::Walk;pose.spriteFrame=60;pose.flip=true;
        CHECK(cat::animation::ApplyReferenceNeutralHold(pose,Motion::Idle,true,0,true,t));
        CHECK(pose.frame==4&&pose.spriteClip==cat::sprite_sequence::Clip::None&&!pose.spriteTimed&&pose.flip);
        CHECK(pose.x==0&&pose.lift==0&&pose.scaleX==1&&pose.scaleY==1&&pose.angle==0&&pose.groundContact);
        CHECK(pose.blendFrame<0&&!pose.bellyRig&&!pose.walkingRig&&!pose.originalWalk&&!pose.jointWalk);
        CHECK(cat::deform::Finite(pose.joints));
        blink|=pose.face.blink>.9;partialBlink|=pose.face.blink>0&&pose.face.blink<.9;
        leftEar|=pose.joints.leftEar>.2;rightEar|=pose.joints.rightEar>.2;
        leftPaw|=pose.joints.frontLeft>.5;rightPaw|=pose.joints.frontRight>.5;tail|=pose.joints.tailRaise>.3;
        (cat::idle_gestures::NeedsFineCadence(t)?precise:economical)++;
        // Only a moving forepaw may leave its local baseline. Hind support
        // points and the root retain exactly the authored floor coordinates.
        for(const auto point:{cat::deform::Point{.18,1},cat::deform::Point{.90,1}}) {
            const auto moved=cat::deform::SittingPoint(point,pose.joints);
            CHECK(moved.x==point.x&&moved.y==point.y);
        }
        // The tiny head motion is a rigid turn; there is no face aspect scale.
        const auto l=cat::deform::SittingPoint({.45,.43},pose.joints);
        const auto r=cat::deform::SittingPoint({.67,.43},pose.joints);
        CHECK(std::abs(std::hypot(r.x-l.x,r.y-l.y)-.22)<1e-9);
    }
    CHECK(blink&&partialBlink&&leftEar&&rightEar&&leftPaw&&rightPaw&&tail);
    CHECK(precise>0&&economical>precise); // No permanent60FPS idle timer.
}
void ClockAndOwnershipBoundaries() {
    for(std::uint64_t t=0;t<180000;t+=7) {
        const auto a=Values(cat::idle_gestures::Sample(t));
        const auto b=Values(cat::idle_gestures::Sample(t+1));
        for(unsigned i=0;i<a.size();++i) {
            CHECK(std::isfinite(a[i]));
            CHECK(std::abs(a[i]-b[i])<.025); // Includes phrases/long-period wrap.
        }
    }
    for(std::uint64_t t=0;t<=350;++t) {
        const auto a=cat::idle_gestures::Sample(t);
        CHECK(cat::deform::Zero(a.joints)&&cat::expression::Zero(a.face));
    }
    // Clock freezing is deterministic: repeated paused draws cannot advance a
    // blink or lose a lifted paw, and a resumed host picks up the same value.
    const auto held=Values(cat::idle_gestures::Sample(5583));
    for(unsigned draw=0;draw<500;++draw)CHECK(Values(cat::idle_gestures::Sample(5583))==held);
    for(const auto motion:{Motion::Sleep,Motion::Walk,Motion::Eat,Motion::Jump,Motion::Groom,Motion::Treat}) {
        Pose pose;pose.spriteClip=cat::sprite_sequence::Clip::Jump;pose.spriteFrame=14;
        CHECK(!cat::animation::ApplyReferenceNeutralHold(pose,motion,true,0,true,5000));
        CHECK(pose.spriteClip==cat::sprite_sequence::Clip::Jump&&pose.spriteFrame==14);
    }
    for(const auto speed:{-.6,.6,30.}) {
        Pose pose;CHECK(!cat::animation::ApplyReferenceNeutralHold(pose,Motion::Idle,true,speed,true,5000));
    }
    Pose pose;CHECK(!cat::animation::ApplyReferenceNeutralHold(pose,Motion::Idle,false,0,true,5000));
    CHECK(!cat::animation::ApplyReferenceNeutralHold(pose,Motion::Idle,true,0,false,5000));
    pose.spriteClip=cat::sprite_sequence::Clip::Fish;
    CHECK(!cat::animation::ApplyReferenceNeutralHold(pose,Motion::Idle,true,0,true,5000));
    CHECK(cat::animation::ApplyReferenceSleepHold(pose,Motion::Sleep,5000,true,3900));
    CHECK(pose.frame==6&&cat::deform::Zero(pose.joints)&&cat::expression::Zero(pose.face));
}
}
int main() {
    try {CompleteActingWithoutMovingTheRoot();ClockAndOwnershipBoundaries();
        std::cout<<"Cute idle: "<<checks<<" checks passed\n";return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
