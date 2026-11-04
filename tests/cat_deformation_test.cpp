#include "../src/cat_deformation.hpp"
#include <vector>
#include <iostream>
#include <stdexcept>
#include <string>
#include <limits>

namespace {
int checks=0;
#define CHECK(x) do { ++checks; if (!(x)) throw std::runtime_error(std::string(__FILE__)+":"+std::to_string(__LINE__)+" " #x); } while(false)
using namespace cat::deform;
bool Same(Point a,Point b) { return a.x==b.x && a.y==b.y; }
void LocalJointsStayAttached() {
    const Joints ears{1,1};
    for (int y=36;y<=100;++y) for(int x=0;x<=100;++x)
        CHECK(Same({x/100.,y/100.},SittingPoint({x/100.,y/100.},ears)));
    CHECK(SittingPoint({.23,.07},ears).y>.07);
    CHECK(SittingPoint({.82,.07},ears).y>.07);
    CHECK(SittingPoint({.23,.07},ears).x>.23);
    CHECK(SittingPoint({.82,.07},ears).x<.82);
    Joints paw; paw.frontLeft=1;
    CHECK(Same({.43,.73},SittingPoint({.43,.73},paw)));
    CHECK(SittingPoint({.43,.96},paw).y<.96);
    CHECK(Same({.69,.96},SittingPoint({.69,.96},paw)));
    CHECK(Same({.57,.4},SittingPoint({.57,.4},paw)));
    Joints tail;tail.tailRaise=1;
    CHECK(Same({.33,.9},SittingPoint({.33,.9},tail)));
    CHECK(Same({.12,.98},SittingPoint({.12,.98},tail)));
    CHECK(SittingPoint({.12,.76},tail).y<.76);
    CHECK(Same({.57,.8},SittingPoint({.57,.8},tail)));
    Joints all{1,1,1,1,1,1,1,1};
    for (Shape shape:{Shape::Sitting,Shape::ReclineSide,Shape::ReclineBack})
    for (int y=0;y<=100;++y) for (int x=0;x<=100;++x) {
        Point p{x/100.,y/100.};auto q=DeformPoint(p,all,shape);
        CHECK(std::isfinite(q.x) && std::isfinite(q.y));
        CHECK(std::abs(q.x-p.x)<.06 && std::abs(q.y-p.y)<.15);
        if(shape!=Shape::Sitting && y>=94) CHECK(Same(p,q));
    }
}
void IdentityAndBodyPixels() {
    constexpr int w=97,h=111,stride=w+3;
    std::vector<std::uint32_t> src(stride*h), dst(stride*h,0x12345678);
    for (int y=0;y<h;++y)for(int x=0;x<w;++x) {
        const unsigned a=(x*3+y*7)%256,b=(x+y)%(a+1),g=(x*2+y)%(a+1),r=(x+y*3)%(a+1);
        src[y*stride+x]=(a<<24)|(r<<16)|(g<<8)|b;
    }
    CHECK(Warp(src.data(),w,h,stride*4,dst.data(),stride*4,{}));
    for (int y=0;y<h;++y)for(int x=0;x<w;++x) CHECK(dst[y*stride+x]==src[y*stride+x]);
    CHECK(Warp(src.data(),w,h,stride*4,src.data(),stride*4,{}));
    CHECK(Warp(src.data(),w,h,stride*4,dst.data(),stride*4,{1,1}));
    int changes=0;
    for(int y=0;y<h;++y)for(int x=0;x<w;++x) {
        if((y+.5)/h>=.36) CHECK(dst[y*stride+x]==src[y*stride+x]);
        else changes+=dst[y*stride+x]!=src[y*stride+x];
        const auto p=dst[y*stride+x],a=p>>24;
        CHECK((p&255)<=a && ((p>>8)&255)<=a && ((p>>16)&255)<=a);
    }
    CHECK(changes>100);
    for(int y=0;y<h;++y)for(int x=w;x<stride;++x) CHECK(dst[y*stride+x]==0x12345678);
    for(Shape shape:{Shape::ReclineSide,Shape::ReclineBack}) {
        CHECK(Warp(src.data(),w,h,stride*4,dst.data(),stride*4,{0,0,1,1,1,0,1},shape));
        for(int y=0;y<h;++y)for(int x=0;x<w;++x)
            if((y+.5)/h>=.94) CHECK(dst[y*stride+x]==src[y*stride+x]);
    }
}
void WatertightAndFinite() {
    constexpr int w=101,h=103;
    std::vector<std::uint32_t> src(w*h,0xff684020),dst(w*h);
    for(Shape shape:{Shape::Sitting,Shape::ReclineSide,Shape::ReclineBack})
    for(int phase=0;phase<12;++phase) {
        const double t=phase/11.;
        Joints j{t,1-t,2*t-1,t,1-t,t,1-2*t,t};
        CHECK(Warp(src.data(),w,h,w*4,dst.data(),w*4,j,shape));
        // No cracks in a solid texture across any shared mesh diagonal/edge.
        // Exclude outside border, which can correctly reveal transparency.
        for(int y=16;y<h-6;++y)for(int x=8;x<w-8;++x) CHECK(dst[y*w+x]==src[y*w+x]);
        // A deformed mesh must not fold or reverse triangle winding.
        for(int y=0;y<32;++y)for(int x=0;x<32;++x) {
            Point a{x/32.,y/32.},b{(x+1)/32.,y/32.},c{x/32.,(y+1)/32.},d{(x+1)/32.,(y+1)/32.};
            a=DeformPoint(a,j,shape);b=DeformPoint(b,j,shape);c=DeformPoint(c,j,shape);d=DeformPoint(d,j,shape);
            CHECK(detail::Cross(a,b,d)>0 && detail::Cross(a,d,c)>0);
        }
    }
}
void CrouchPreservesHeadAndGround() {
    for (double amount : {0.0,.25,.65,1.0}) {
        Joints j; j.crouch=amount;
        for (Point p : {Point{.23,.07},Point{.82,.07},Point{.40,.47},Point{.70,.47},Point{.57,.65}}) {
            const auto q=SittingPoint(p,j);
            CHECK(q.x==p.x);
            CHECK(std::abs(q.y-p.y-.10*amount)<1e-12);
        }
        // Eyes and ear tips move together; all pairwise head distances remain.
        const Point eyesA{.40,.47},eyesB{.70,.45},ear{.82,.07};
        const auto a=SittingPoint(eyesA,j),b=SittingPoint(eyesB,j),c=SittingPoint(ear,j);
        CHECK(std::abs(std::hypot(a.x-b.x,a.y-b.y)-std::hypot(eyesA.x-eyesB.x,eyesA.y-eyesB.y))<1e-12);
        CHECK(std::abs(std::hypot(a.x-c.x,a.y-c.y)-std::hypot(eyesA.x-ear.x,eyesA.y-ear.y))<1e-12);
        for (int x=0;x<=100;++x) for(double y:{.98,.99,1.0})
            CHECK(Same({x/100.,y},SittingPoint({x/100.,y},j)));
        // The torso compresses monotonically without folding through itself.
        for(int y=0;y<1000;++y) {
            const auto upper=SittingPoint({.57,y/1000.},j);
            const auto lower=SittingPoint({.57,(y+1)/1000.},j);
            CHECK(lower.y>upper.y);
        }
    }
    Joints full;full.crouch=1;
    CHECK(!Zero(full));
    Joints excessive;excessive.crouch=2;
    CHECK(Same(SittingPoint({.5,.4},full),SittingPoint({.5,.4},excessive)));
    excessive.crouch=-1;
    CHECK(Same(SittingPoint({.5,.4},excessive),{.5,.4}));
    excessive.crouch=std::numeric_limits<double>::quiet_NaN();
    CHECK(!Finite(excessive));
    constexpr int w=97,h=111;
    std::vector<std::uint32_t> src(w*h),dst(w*h);
    for(int y=0;y<h;++y)for(int x=0;x<w;++x)src[y*w+x]=0xff000000|((x*7+y*11)&0xffffff);
    CHECK(Warp(src.data(),w,h,w*4,dst.data(),w*4,full));
    for(int y=0;y<h;++y)for(int x=0;x<w;++x)
        if((y+.5)/h>=.98) CHECK(src[y*w+x]==dst[y*w+x]);
}
void InvalidInputDoesNotWrite() {
    std::vector<std::uint32_t> src(100,0xff223344),dst(100,0x98765432);
    Joints invalid;invalid.leftEar=std::numeric_limits<double>::quiet_NaN();
    CHECK(!Warp(src.data(),10,10,40,dst.data(),40,invalid));
    CHECK(Same(SittingPoint({.4,.5},invalid),{.4,.5}));
    CHECK(Same(SittingPoint({std::numeric_limits<double>::quiet_NaN(),.5},{}),{}));
    CHECK(!Warp(src.data(),0,10,40,dst.data(),40,{}));
    CHECK(!Warp(src.data(),4097,10,20000,dst.data(),20000,{}));
    CHECK(!Warp(src.data(),10,10,39,dst.data(),40,{}));
    CHECK(!Warp(src.data(),10,10,40,dst.data(),-40,{}));
    CHECK(!Warp(src.data(),10,10,40,dst.data(),40,{},static_cast<Shape>(9)));
    CHECK(!Warp(src.data(),10,10,40,src.data(),40,{1}));
    CHECK(!Warp(src.data(),9,9,40,src.data()+1,40,{1}));
    for(auto p:dst) CHECK(p==0x98765432);
}
}
int main() {
    try { LocalJointsStayAttached();IdentityAndBodyPixels();WatertightAndFinite();CrouchPreservesHeadAndGround();InvalidInputDoesNotWrite();
        std::cout<<"Local deformation: "<<checks<<" checks passed\n";
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
