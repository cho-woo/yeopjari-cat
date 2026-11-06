// Isolated numeric authoring probe. No production renderer is changed here.
#include "cat_walk_dynamics.hpp"
#include "cat_articulated_walk.hpp"
#include <cmath>
#include <iostream>
#include <limits>

extern "C" int dynamic_walk_frame(double phase,double speed,int mode,double* output) {
    if(!output)return 0;
    const auto selected=mode==2?cat::walk_dynamics::Mode::Gallop:mode==1?cat::walk_dynamics::Mode::Trot:cat::walk_dynamics::Mode::Walk;
    const auto frame=cat::walk_dynamics::Evaluate(phase,speed,selected);
    const auto parameters=cat::walk_dynamics::ParametersFor(selected);
    output[0]=frame.torsoX;output[1]=frame.torsoY;output[2]=frame.pitchDegrees;
    output[3]=parameters.stride;output[4]=parameters.duty;output[5]=parameters.lift;
    for(unsigned leg=0;leg<4;++leg) {
        output[6+2*leg]=frame.rootOffsets[leg].x;
        output[7+2*leg]=frame.rootOffsets[leg].y;
    }
    return 1;
}
namespace {
struct Review {
    cat::articulated::Rig rig;
    cat::walk_dynamics::Mode mode;
    cat::gait::Cycle cycle;
    explicit Review(cat::walk_dynamics::Mode selected)
        :mode(selected),cycle(cat::walk_dynamics::ParametersFor(selected),false,0){}
};
}
extern "C" void* dynamic_walk_create(const std::uint32_t* pixels,int width,int height,int mode) {
    auto* review=new Review(mode==2?cat::walk_dynamics::Mode::Gallop:mode==1?cat::walk_dynamics::Mode::Trot:cat::walk_dynamics::Mode::Walk);
    if(!review->rig.Prepare(pixels,width,height,width*4)){delete review;return nullptr;}
    return review;
}
extern "C" void dynamic_walk_destroy(void* ptr){delete static_cast<Review*>(ptr);}
extern "C" void dynamic_walk_leg(void* ptr,int index,double* output) {
    const auto& leg=static_cast<Review*>(ptr)->rig.Legs()[index];
    output[0]=leg.root.x;output[1]=leg.root.y;output[2]=leg.ankle.x;output[3]=leg.ankle.y;
    output[4]=leg.targetPaw.x;output[5]=leg.targetPaw.y;
    output[6]=leg.upperLength;output[7]=leg.lowerLength;output[8]=leg.clamped;
}
extern "C" int dynamic_walk_step(void* ptr,double distance,double seconds,double width,double height,double* output) {
    auto& review=*static_cast<Review*>(ptr);
    review.cycle.Advance(distance,seconds,true,false);
    auto feet=review.cycle.Targets();
    for(unsigned leg=0;leg<4;++leg) {
        feet[leg].x+=cat::walk_dynamics::PawBias(review.mode,leg);
    }
    auto frame=cat::walk_dynamics::Evaluate(review.cycle.Phase(),review.cycle.Speed(),review.mode,&feet);
    if(!review.rig.Raster(feet,width,height,frame))return 0;
    if(output) {
        output[0]=review.rig.ReachError();output[1]=frame.torsoY;output[2]=frame.pitchDegrees;output[3]=frame.airborneWeight;
        for(unsigned leg=0;leg<4;++leg){const auto& solved=review.rig.Legs()[leg];
            output[4+4*leg]=solved.paw.x;output[5+4*leg]=solved.paw.y;
            output[6+4*leg]=feet[leg].stance;output[7+4*leg]=static_cast<double>(feet[leg].contact);}
    }
    return 1;
}
#ifndef DYNAMIC_WALK_PROBE_LIBRARY
int main() {
    using namespace cat::walk_dynamics;
    std::array<double,3> maximumDrop{};
    double maximumPitch=0,maximumRoot=0;
    for(Mode mode:{Mode::Walk,Mode::Trot,Mode::Gallop})for(double speed:{0.,1.,5.,12.,22.,165.})
    for(int sample=0;sample<1000;++sample) {
        const double phase=sample/1000.;
        const auto frame=Evaluate(phase,speed,mode),wrapped=Evaluate(phase+1,speed,mode);
        if(!std::isfinite(frame.torsoY)||frame.torsoY < -1.6||frame.torsoY>1.80+1e-9 ||
           std::abs(frame.pitchDegrees)>2.05+1e-9 || std::abs(frame.torsoY-wrapped.torsoY)>1e-10 ||
           std::abs(frame.spineStretch)>.025+1e-9 || std::abs(frame.tailDegrees)>47+1e-9)return 1;
        if(speed==0&&(frame.torsoX!=0||frame.torsoY!=0||frame.pitchDegrees!=0))return 2;
        for(const auto offset:frame.rootOffsets)maximumRoot=std::max(maximumRoot,std::hypot(offset.x,offset.y));
        auto& drop=maximumDrop[static_cast<unsigned>(mode)];drop=std::max(drop,frame.torsoY);
        maximumPitch=std::max(maximumPitch,std::abs(frame.pitchDegrees));
        // Rigid image/root transform cannot pulse or resize the painted head.
        const auto a=BodyPoint({20,20},frame,110,100),b=BodyPoint({60,20},frame,110,100);
        if(std::abs(std::hypot(b.x-a.x,b.y-a.y)-40)>1e-10)return 3;
        for(const Point original:{Point{20,20},Point{60,80}}) {
            const auto moved=BodyPoint(original,frame,110,100);
            const double radians=frame.pitchDegrees*kPi/180,c=std::cos(radians),s=std::sin(radians);
            const double pivotX=110*frame.pivotU,pivotY=100*frame.pivotV;
            const double x=moved.x-pivotX-frame.torsoX,y=moved.y-pivotY-frame.torsoY;
            const double sourceY=pivotY-x*s+y*c;
            const double sourceX=UnstretchX(pivotX+x*c+y*s,sourceY,frame,110,100);
            if(std::hypot(sourceX-original.x,sourceY-original.y)>1e-10)return 5;
        }
    }
    const auto invalid=Evaluate(std::numeric_limits<double>::quiet_NaN(),22);
    if(invalid.torsoY!=0||invalid.pitchDegrees!=0)return 4;
    std::cout<<"walk_max_drop_body_px="<<maximumDrop[0]<<" trot_max_drop_body_px="<<maximumDrop[1]
             <<" gallop_max_drop_body_px="<<maximumDrop[2]
             <<" max_pitch_degrees="<<maximumPitch<<" max_local_root_offset="<<maximumRoot<<'\n';
}
#endif
