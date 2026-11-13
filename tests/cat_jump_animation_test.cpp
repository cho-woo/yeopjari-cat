#include "../src/cat_jump_animation.hpp"
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
int checks=0;
#define CHECK(c) do{++checks;if(!(c))throw std::runtime_error(std::string(__FILE__)+":"+std::to_string(__LINE__)+" " #c);}while(false)
using namespace cat::jump_animation;
void OnePaintingHasStableFaceAndPlantedContact() {
    CHECK(kSourceFrame==29);
    for(unsigned t=0;t<=600;++t) {
        const auto controls=Preparation(t);
        CHECK(Finite(controls)&&controls.fore==0&&controls.hind==0);
        for(const Point paw: {Point{.466,.968},Point{.777,.968},Point{.360,.968},Point{.590,.968}}) {
            const auto q=PointFor(paw,controls);CHECK(q.x==paw.x&&q.y==paw.y);
        }
        const Point eye{.72,.42},ear{.60,.18},nose{.84,.54};
        const auto a=PointFor(eye,controls),b=PointFor(ear,controls),c=PointFor(nose,controls);
        CHECK(a.x-eye.x==0&&b.x-ear.x==0&&c.x-nose.x==0);
        CHECK(std::abs((a.y-eye.y)-(b.y-ear.y))<1e-12);
        CHECK(std::abs((a.y-eye.y)-(c.y-nose.y))<1e-12);
    }
    CHECK(Preparation(0).crouch==0&&Preparation(kPreparationMs).crouch==0);
    CHECK(Contact(0,1).crouch==0&&Contact(kContactReleaseMs,1).crouch==0);
    CHECK(Contact(100,1).crouch>.8);
}
void PhysicsControlsPawsInsteadOfNominalFrameClock() {
    for(double height:{18.,30.,55.,110.,180.})for(double support:{0.,170.,700.}) {
        Driver driver;driver.Prepare(300,300,support);
        constexpr double gravity=900.;const double launch=std::sqrt(2*gravity*height),duration=2*launch/gravity;
        auto previous=driver.Flight(launch,support);CHECK(previous.fore==0&&previous.hind==0);
        CHECK(driver.SourceFlightProgress()==0);
        double maximum=0;
        for(double t=1./120;t<duration;t+=1./120) {
            const double vy=launch-gravity*t,root=support+launch*t-.5*gravity*t*t;
            const auto c=driver.Flight(vy,root);
            CHECK(std::abs(driver.SourceFlightProgress()-t/duration)<1e-9);
            CHECK(Finite(c)&&c.crouch==0);
            CHECK(c.fore>=0&&c.fore<=.84&&c.hind>=0&&c.hind<=.70);
            CHECK(std::abs(c.fore-previous.fore)<.20);
            // Sampling the same physical pose again never advances paws.
            const auto held=driver.Flight(vy,root);
            CHECK(held.fore==c.fore&&held.hind==c.hind);
            maximum=std::max(maximum,c.fore);previous=c;
        }
        CHECK(maximum>.75);
        const auto contact=driver.Land(0,1,support);
        CHECK(contact.fore==previous.fore&&contact.hind==previous.hind);
        const auto resting=driver.Land(600,1,support);
        CHECK(resting.fore==0&&resting.hind==0&&resting.crouch==0&&resting.tail==0);
    }
    Driver fallback;fallback.Ground(700);
    CHECK(Finite(fallback.Flight(-100,690))); // falling off a real ledge
    CHECK(Finite(fallback.Flight(std::numeric_limits<double>::quiet_NaN(),0)));
}
void NoFoldoversOrAlphaSeams() {
    // Entire accepted control domain, including mixed contact/tuck samples.
    for(unsigned stage=0;stage<4;++stage)for(unsigned strength=0;strength<=10;++strength) {
        const double t=strength/10.;Controls c{};
        if(stage==0)c.crouch=t;
        if(stage==1)c.fore=t;
        if(stage==2)c.hind=t;
        if(stage==3)c={t,t,t,t};
        for(unsigned y=0;y<120;++y)for(unsigned x=0;x<120;++x) {
            const Point a{x/120.,y/120.},b{(x+1)/120.,y/120.},d{x/120.,(y+1)/120.};
            CHECK(cat::deform::detail::Cross(PointFor(a,c),PointFor(b,c),PointFor(d,c))>0);
        }
    }
    constexpr int width=151,height=119;
    std::vector<std::uint32_t> source(width*height,0xff637485),target(source.size());
    CHECK(Warp(source.data(),width,height,target.data(),{}));
    CHECK(source==target); // unchanged pixels are bit-exact, no double exposure
    CHECK(Warp(source.data(),width,height,target.data(),{0,.84,.70,.22}));
    for(int y=2;y<height-2;++y)for(int x=2;x<width-2;++x)
        CHECK(target[y*width+x]>>24==255);
    CHECK(!Warp(source.data(),width,height,source.data(),{}));
}
void ContactHasOneSmoothCompressionRamp() {
    for(double strength:{.15,.5,1.}) {
        Driver driver;driver.Ground(0);driver.Flight(-300,2);
        auto previous=driver.Land(0,strength,0);
        double peak=0;
        for(unsigned frame=1;frame<=30;++frame) {
            const auto controls=driver.Land(frame*1000/60,strength,0);
            CHECK(std::abs(controls.crouch-previous.crouch)<=.26);
            peak=std::max(peak,controls.crouch);previous=controls;
        }
        CHECK(peak>.8*strength&&previous.crouch==0);
    }
}
}
int main(){try{OnePaintingHasStableFaceAndPlantedContact();PhysicsControlsPawsInsteadOfNominalFrameClock();
    NoFoldoversOrAlphaSeams();ContactHasOneSmoothCompressionRamp();std::cout<<"Connected jump: "<<checks<<" checks passed\n";return EXIT_SUCCESS;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return EXIT_FAILURE;}}
