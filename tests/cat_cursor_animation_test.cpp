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
void TimingAndRealReach() {
    CHECK(DurationMs(AttackKind::Gentle)==850&&DurationMs(AttackKind::Strong)==650);
    CHECK(ContactIndex(AttackKind::Gentle,219)==0&&ContactIndex(AttackKind::Gentle,220)==1);
    CHECK(ContactIndex(AttackKind::Gentle,359)==1&&ContactIndex(AttackKind::Gentle,360)==0);
    CHECK(ContactIndex(AttackKind::Gentle,500)==2&&ContactIndex(AttackKind::Gentle,650)==0);
    CHECK(ContactIndex(AttackKind::Strong,259)==0&&ContactIndex(AttackKind::Strong,260)==1);
    CHECK(ContactIndex(AttackKind::Strong,439)==1&&ContactIndex(AttackKind::Strong,440)==0);
    for(auto kind:{AttackKind::Gentle,AttackKind::Strong}) {
        int windows=0,previous=0;
        for(unsigned t=0;t<=DurationMs(kind)+500;++t) {
            const int contact=ContactIndex(kind,t);if(contact&&contact!=previous)++windows;previous=contact;
            const Geometry g{100,110,.69};
            const auto p=AttackPose(kind,t,1,22,15,g),mirror=AttackPose(kind,t,-1,-22,15,g);
            CHECK(p.frame==4&&p.blendFrame==-1&&p.x==0&&p.lift==0&&p.angle==0);
            CHECK(p.scaleX==1&&p.scaleY==1&&p.groundContact);
            CHECK(cat::deform::Finite(p.joints));
            const auto tip=PawPosition(p,g),other=PawPosition(mirror,g);
            CHECK(std::abs(tip.x+other.x)<1e-10&&tip.y==other.y);
            CHECK(!NearPaw(p,g,90,70)); // Alert animation is not a successful hit.
            if(t>=DurationMs(kind))CHECK(cat::deform::Zero(p.joints));
            const auto next=PawPosition(AttackPose(kind,t+1,1,22,15,g),g);
            CHECK(std::hypot(next.x-tip.x,next.y-tip.y)<.6); // Quantized native controls.
        }
        CHECK(windows==(kind==AttackKind::Gentle?2:1));
    }
    const auto gentle=AttackPose(AttackKind::Gentle,270,1,19,4.4);
    const auto strong=AttackPose(AttackKind::Strong,320,1,19,4.4);
    CHECK(PawPosition(gentle).y>4.4); // Tap still lifts at a pointer on the resting paw.
    CHECK(PawPosition(strong).y>PawPosition(gentle).y);
    const auto hit=PawPosition(strong);
    CHECK(NearPaw(strong,{},hit.x,hit.y));
    CHECK(!NearPaw(strong,{},hit.x+20,hit.y));
    for(double bad:{std::numeric_limits<double>::quiet_NaN(),std::numeric_limits<double>::infinity()}) {
        const auto p=AttackPose(AttackKind::Strong,320,1,bad,bad,{bad,bad,bad});
        CHECK(cat::deform::Finite(p.joints));CHECK(!NearPaw(p,{},bad,4));
    }
}
void LocalForelegHasNoHeadOrSupportingFootMotion() {
    using namespace cat::deform;
    constexpr int w=121,h=133;
    std::vector<std::uint32_t> src(w*h),dst(w*h);
    for(int y=0;y<h;++y)for(int x=0;x<w;++x)src[y*w+x]=0xff000000|((x*17777+y*33333)&0xffffff);
    for(double anchor:{.69,.80})for(double dx:{-200.,0.,25.,200.})for(double height:{0.,15.,80.}) {
        const auto p=AttackPose(AttackKind::Strong,320,1,dx,height,{100,110,anchor});
        CHECK(Warp(src.data(),w,h,w*4,dst.data(),w*4,p.joints));
        int changed=0;
        for(int y=0;y<h;++y)for(int x=0;x<w;++x) {
            if((y+.5)/h<.65||(x+.5)/w<anchor-.20)CHECK(src[y*w+x]==dst[y*w+x]);
            changed+=src[y*w+x]!=dst[y*w+x];
        }
        CHECK(changed>30);
        for(int y=0;y<100;++y)for(int x=0;x<100;++x) {
            Point a{x/100.,y/100.},b{(x+1)/100.,y/100.},c{x/100.,(y+1)/100.};
            a=SittingPoint(a,p.joints);b=SittingPoint(b,p.joints);c=SittingPoint(c,p.joints);
            CHECK(detail::Cross(a,b,c)>0);
        }
        const auto eyeA=SittingPoint({.40,.47},p.joints),eyeB=SittingPoint({.70,.45},p.joints);
        CHECK(eyeA.x==.40&&eyeA.y==.47&&eyeB.x==.70&&eyeB.y==.45);
        const auto support=SittingPoint({.43,1},p.joints);
        CHECK(support.x==.43&&support.y==1);
    }
}
}
int main(){try{TimingAndRealReach();LocalForelegHasNoHeadOrSupportingFootMotion();
std::cout<<"Cursor attack animation: "<<checks<<" checks passed\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
