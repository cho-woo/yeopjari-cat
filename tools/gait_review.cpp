// Read-only QA bridge. Compile with an archived include directory and
// GAIT_REVIEW_BASELINE to compare the release rig with the working source.
#include "cat_animation.hpp"
#include "cat_locomotion_rig.hpp"
#include <chrono>

extern "C" {
void* gait_review_create(const std::uint32_t* pixels,int width,int height,int character) {
    auto* rig=new cat::walk::Rig;
    if(!rig->Prepare(pixels,width,height,width*4,cat::walk::ProfileFor(static_cast<cat::Character>(character)))) {
        delete rig;return nullptr;
    }
    return rig;
}
void gait_review_destroy(void* p) {delete static_cast<cat::walk::Rig*>(p);}
int gait_review_raster(void* p,int gait,double distance,double width,double height) {
    const auto motion=gait==1?cat::animation::Motion::Stalk:gait==2?cat::animation::Motion::Zoomies:cat::animation::Motion::Walk;
    cat::animation::Locomotion locomotion;
    while(distance>40) {if(!locomotion.Advance(motion,40,.1))return 0;distance-=40;}
    if(!locomotion.Advance(motion,distance,.1))return 0;
    const auto pose=cat::animation::Evaluate(motion,0,500000,1,locomotion);
    return static_cast<cat::walk::Rig*>(p)->Raster(pose.walkPhase,pose.walkStride,width,height,pose.walkDuty,pose.walkLift);
}
const std::uint32_t* gait_review_pixels(void* p) {return static_cast<cat::walk::Rig*>(p)->Pixels();}
int gait_review_width(void* p) {return static_cast<cat::walk::Rig*>(p)->Width();}
int gait_review_height(void* p) {return static_cast<cat::walk::Rig*>(p)->Height();}
double gait_review_inset(void* p) {return static_cast<cat::walk::Rig*>(p)->GroundInsetPixels();}
double gait_review_offset(void* p) {
#ifdef GAIT_REVIEW_BASELINE
    (void)p;return 0;
#else
    return static_cast<cat::walk::Rig*>(p)->BodyOffset();
#endif
}
double gait_review_benchmark(void* p,int gait,int count,double width,double height) {
    if(count<=0)return -1;
    const auto motion=gait==1?cat::animation::Motion::Stalk:gait==2?cat::animation::Motion::Zoomies:cat::animation::Motion::Walk;
    cat::animation::Locomotion locomotion;
    const auto pose=cat::animation::Evaluate(motion,0,500000,1,locomotion);
    auto& rig=*static_cast<cat::walk::Rig*>(p);
    for(int i=0;i<8;++i)if(!rig.Raster(i/8.,pose.walkStride,width,height,pose.walkDuty,pose.walkLift))return -1;
    const auto start=std::chrono::steady_clock::now();
    for(int i=0;i<count;++i)
        if(!rig.Raster((i%256)/256.,pose.walkStride,width,height,pose.walkDuty,pose.walkLift))return -1;
    return std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count()/count;
}
}
