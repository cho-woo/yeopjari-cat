#include "../src/cat_cursor_animation.hpp"
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>
namespace {
int checks=0;
#define CHECK(c) do{++checks;if(!(c))throw std::runtime_error(std::string(__FILE__)+":"+std::to_string(__LINE__)+" " #c);}while(false)
using namespace cat::cursor;
using cat::cursor_rig::Controls;
void ConnectedGeometry() {
    const Geometry geometry{98.6,109.7,.69};
    for(auto kind:{AttackKind::Tiptoe,AttackKind::BothPaws}) {
        int count=0,previous=0;
        for(unsigned t=0;t<=DurationMs(kind)+100;++t) {
            const auto p=AttackPose(kind,t,1,18,80,geometry),mirror=AttackPose(kind,t,-1,-18,80,geometry);
            CHECK(p.frame==4&&p.spriteClip==cat::sprite_sequence::Clip::None&&p.blendFrame<0);
            CHECK(p.x==0&&p.lift==0&&p.angle==0&&p.scaleX==1&&p.scaleY==1&&p.groundContact);
            const auto paws=PawPositions(p,geometry),mirrored=PawPositions(mirror,geometry);
            for(int side=0;side<2;++side){
                CHECK(std::isfinite(paws[side].x)&&std::isfinite(paws[side].y));
                CHECK(std::abs(paws[side].x+mirrored[side].x)<1e-9&&paws[side].y==mirrored[side].y);
                if(cat::cursor_rig::Active(p.cursorReach)) {
                    const auto shoulder=cat::cursor_rig::Shoulder(p.cursorReach,side),paw=cat::cursor_rig::PawUV(p.cursorReach,side);
                    const auto elbow=cat::cursor_rig::Elbow(p.cursorReach,side);
                    CHECK(std::hypot((elbow.x-shoulder.x)*.90,elbow.y-shoulder.y)<.121);
                    CHECK(std::hypot((paw.x-elbow.x)*.90,paw.y-elbow.y)<.121);
                    CHECK(std::abs(paws[side].x-(paw.x-.5)*geometry.width)<1e-9);
                    CHECK(std::abs(paws[side].y-(1-paw.y)*geometry.height)<1e-9);
                }
            }
            for(double x:{.30,.33,.81,.86}) {
                const auto foot=cat::cursor_rig::BodyPoint({x,1},p.cursorReach);
                CHECK(foot.x==x&&foot.y==1); // Fixed grounded hind soles, no floating root.
            }
            const auto headA=cat::cursor_rig::BodyPoint({.4,.3},p.cursorReach),headB=cat::cursor_rig::BodyPoint({.7,.5},p.cursorReach);
            CHECK(std::abs(headA.x-headB.x+.3)<1e-12&&std::abs(headA.y-headB.y+.2)<1e-12);
            const auto contact=ContactIndex(kind,t);if(contact&&contact!=previous)++count;previous=contact;
            if(t>=DurationMs(kind))CHECK(!cat::cursor_rig::Active(p.cursorReach));
        }
        CHECK(count==(kind==AttackKind::Tiptoe?1:2));
    }
    CHECK(ContactIndex(AttackKind::Tiptoe,610)==1&&ContactIndex(AttackKind::Tiptoe,900)==0);
    CHECK(ContactIndex(AttackKind::BothPaws,520)==1&&ContactIndex(AttackKind::BothPaws,930)==2);
}
void SmoothHopAndGroomEnd() {
    const auto prepared=HopPose(280,false,true,0,0,1,20,140);
    const auto flying=HopPose(280,true,false,400,0,1,20,140);
    CHECK(prepared.cursorReach.leftLift==flying.cursorReach.leftLift);
    CHECK(prepared.cursorReach.rise==flying.cursorReach.rise);
    CHECK(flying.lift==0&&!flying.groundContact&&flying.frame==4);
    const auto lastAir=HopPose(900,true,false,-400,0,1,20,140);
    const auto landed=HopPose(900,true,true,0,0,1,20,140);
    CHECK(std::abs(lastAir.cursorReach.leftLift-landed.cursorReach.leftLift)<1e-9);
    const auto settled=HopPose(1420,true,true,0,520,1,20,140);
    CHECK(!cat::cursor_rig::Active(settled.cursorReach));
    CHECK(!settled.idleActing);
    CHECK(!GroomPose(0,1,7500).idleActing);
    CHECK(!BallPawPose(0,1,34,12).idleActing);
    CHECK(!BallPawPose(1050,1,34,12).idleActing);
    for(const auto kind:{AttackKind::Tiptoe,AttackKind::BothPaws}) {
        CHECK(!AttackPose(kind,0,1,18,80).idleActing);
        CHECK(!AttackPose(kind,DurationMs(kind),1,18,80).idleActing);
    }
    for(unsigned duration:{1300,3000,4200,5000,9000}) {
        for(unsigned t=duration-20;t<=duration+20;++t) {
            const auto pose=GroomPose(t,1,duration);
            CHECK(!cat::cursor_rig::Active(pose.cursorReach));
            CHECK(pose.face.blink==0&&pose.frame==4&&pose.spriteClip==cat::sprite_sequence::Clip::None);
            CHECK(!pose.idleActing); // Same full-resolution neutral as the following Idle.
        }
    }
    for(unsigned t=0;t<=1050;++t){
        const auto p=BallPawPose(t,1,34,12);const auto paws=PawPositions(p);
        CHECK(p.lift==0&&p.x==0&&p.frame==4&&p.groundContact);
        if(t==490){CHECK(paws[1].x>28&&paws[1].y<12);CHECK(paws[0].y<6);}
    }
}
void ExactPaintedRepairEndpoint() {
    using namespace cat::cursor_rig;
    constexpr int w=80,h=90;const int outputHeight=OutputHeight(h);
    std::vector<std::uint32_t> source(w*h),paint(w*h),before(w*outputHeight),after(w*outputHeight);
    for(int y=0;y<h;++y)for(int x=0;x<w;++x){
        const auto a=static_cast<unsigned>(90+(x*3+y*7)%166);
        source[y*w+x]=(a<<24)|((a*9/10)<<16)|((a*8/10)<<8)|(a*7/10);
        paint[y*w+x]=0xfff4e4cf;
    }
    Source parts;CHECK(Prepare(parts,source.data(),w,h));CHECK(SetBody(parts,paint.data(),w,h));
    UpdateHead(parts,source.data());
    CHECK(Render(parts,before.data(),w,outputHeight,{1e-9,0,0,0}));
    CHECK(Render(parts,after.data(),w,outputHeight,{}));
    for(std::size_t i=0;i<before.size();++i)for(int channel=0;channel<4;++channel)
        CHECK(std::abs(static_cast<int>((before[i]>>(channel*8))&255)-static_cast<int>((after[i]>>(channel*8))&255))<=2);
}
void RasterBoundsAndNoReallocation() {
    using namespace cat::cursor_rig;
    constexpr int width=80,height=90;
    const int outputHeight=OutputHeight(height),padding=outputHeight-height;
    std::vector<std::uint32_t> source(width*height,0xffdde8e9),out(width*outputHeight+4,0x12345678);
    Source parts;CHECK(Prepare(parts,source.data(),width,height));
    const auto* torso=parts.torso.data();const auto* left=parts.leftArm.data();const auto* layer=parts.layer.data();
    CHECK(Render(parts,out.data()+2,width,outputHeight,{}));
    for(int y=0;y<outputHeight;++y)for(int x=0;x<width;++x)
        CHECK(out[2+y*width+x]==(y<padding?0u:source[(y-padding)*width+x]));
    for(int frame=0;frame<180;++frame){
        const auto pose=AttackPose(AttackKind::Tiptoe,frame*8,1,20,85);
        CHECK(Render(parts,out.data()+2,width,outputHeight,pose.cursorReach));
        CHECK(parts.torso.data()==torso&&parts.leftArm.data()==left&&parts.layer.data()==layer);
        CHECK(out[0]==0x12345678&&out[1]==0x12345678&&out[out.size()-1]==0x12345678&&out[out.size()-2]==0x12345678);
        // The added headroom may never clip the original last sole scanline.
        CHECK((out[2+(outputHeight-1)*width+width/4]>>24)>200);
        for(int i=2;i<2+width*outputHeight;++i){const auto p=out[i],a=p>>24;
            CHECK((p&255)<=a&&((p>>8)&255)<=a&&((p>>16)&255)<=a);}
    }
    CHECK(source.front()==0xffdde8e9&&source.back()==0xffdde8e9);
    CHECK(!Render(parts,out.data(),width,outputHeight-1,{}));
    CHECK(!Prepare(parts,nullptr,width,height));
    const auto bad=Bounded({std::numeric_limits<double>::infinity(),-3,5,std::numeric_limits<double>::quiet_NaN()});
    CHECK(bad.rise==0&&bad.leftLift==0&&bad.rightLift==1&&bad.aimX==0);
}
}
int main(){try{ConnectedGeometry();SmoothHopAndGroomEnd();ExactPaintedRepairEndpoint();RasterBoundsAndNoReallocation();
std::cout<<"Connected cursor reach rig: "<<checks<<" checks passed\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
