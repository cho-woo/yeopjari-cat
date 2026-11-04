#include "../src/cat_animation.hpp"
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
int checks=0;
#define CHECK(c) do{++checks;if(!(c))throw std::runtime_error(std::string(__FILE__)+":"+std::to_string(__LINE__)+" " #c);}while(false)
void SingleFaceAtEveryFlightSpeed() {
    auto previous=cat::animation::AirbornePose(-400,1);
    for(int i=-4000;i<=4000;++i) {
        const double vy=i/10.;
        const auto p=cat::animation::AirbornePose(vy,1),left=cat::animation::AirbornePose(vy,-1);
        CHECK(p.frame==16 && p.blendFrame==-1 && p.blend==0);
        CHECK(p.scaleX==1 && p.scaleY==1 && p.lift==0 && p.x==0 && !p.groundContact);
        CHECK(std::isfinite(p.angle) && std::abs(p.angle)<=2);
        CHECK(!p.flip && left.flip && left.angle==-p.angle && left.frame==p.frame);
        CHECK(p.joints.frontLeft>=0 && p.joints.frontLeft<=1 && p.joints.frontRight>=0 && p.joints.frontRight<=1);
        CHECK(p.joints.frontLeft==left.joints.frontLeft && p.joints.frontRight==left.joints.frontRight);
        CHECK(std::abs(p.angle-previous.angle)<.001);
        CHECK(std::abs(p.joints.frontLeft-previous.joints.frontLeft)<.002);
        previous=p;
    }
    const auto apex=cat::animation::AirbornePose(0,1);
    CHECK(apex.angle==0 && apex.joints.frontLeft==1 && apex.joints.frontRight==.85);
    for(double vy:{std::numeric_limits<double>::quiet_NaN(),std::numeric_limits<double>::infinity(),-std::numeric_limits<double>::infinity()}) {
        const auto p=cat::animation::AirbornePose(vy,1);
        CHECK(p.frame==16 && std::isfinite(p.angle) && cat::deform::Finite(p.joints));
    }
}
void TuckLeavesFaceAndBodyPixelsUnchanged() {
    using namespace cat::deform;
    const auto joints=cat::animation::AirbornePose(0,1).joints;
    for(int y=0;y<=64;++y)for(int x=0;x<=100;++x) {
        const Point p{x/100.,y/100.};const auto q=AirbornePoint(p,joints);
        CHECK(q.x==p.x && q.y==p.y);
    }
    for(auto p:{Point{.23,.8},Point{.45,.75},Point{.10,.30},Point{.72,.64},Point{.66,.77}}) {
        const auto q=AirbornePoint(p,joints);CHECK(q.x==p.x && q.y==p.y);
    }
    CHECK(AirbornePoint({.85,.77},joints).x<.85);
    CHECK(AirbornePoint({.66,.88},joints).y<.88);
    constexpr int w=97,h=111;
    std::vector<std::uint32_t> src(w*h),dst(w*h);
    for(int y=0;y<h;++y)for(int x=0;x<w;++x)src[y*w+x]=0xff000000|((x*17777+y*33333)&0xffffff);
    CHECK(Warp(src.data(),w,h,w*4,dst.data(),w*4,joints,Shape::Airborne));
    int changed=0;
    for(int y=0;y<h;++y)for(int x=0;x<w;++x) {
        if((y+.5)/h<=.64 || (x+.5)/w<=.50)CHECK(dst[y*w+x]==src[y*w+x]);
        else changed+=dst[y*w+x]!=src[y*w+x];
        CHECK((dst[y*w+x]>>24)==255); // no raster cracks/fold holes in opaque input
    }
    CHECK(changed>10);
    for(int y=0;y<100;++y)for(int x=0;x<100;++x) {
        Point a{x/100.,y/100.},b{(x+1)/100.,y/100.},c{x/100.,(y+1)/100.};
        a=AirbornePoint(a,joints);b=AirbornePoint(b,joints);c=AirbornePoint(c,joints);
        CHECK(detail::Cross(a,b,c)>0);
    }
}
}
int main(){try{SingleFaceAtEveryFlightSpeed();TuckLeavesFaceAndBodyPixelsUnchanged();std::cout<<"Airborne pose: "<<checks<<" checks passed\n";}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}
