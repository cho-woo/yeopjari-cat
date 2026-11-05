// Read-only QA bridge. Compile with an archived include directory and
// GAIT_REVIEW_NO_BODY_OFFSET is only for releases predating torso translation.
#include "cat_animation.hpp"
#include "cat_locomotion_rig.hpp"
#include <chrono>

namespace {
struct Review { cat::walk::Rig rig; cat::animation::Locomotion locomotion; double elapsed=0; };
cat::animation::Motion MotionFor(int gait) {
    return gait==1?cat::animation::Motion::Stalk:gait==2?cat::animation::Motion::Zoomies:cat::animation::Motion::Walk;
}
std::array<double,3> ParametersFor(int gait) {
#ifdef GAIT_REVIEW_TRACKED_FEET
    const auto profile=cat::gait::ForSpeed(gait==1,gait==2?165:gait==1?12.5:22);
    return {{profile.stride,profile.duty,profile.lift}};
#else
    cat::animation::Locomotion locomotion;
    const auto pose=cat::animation::Evaluate(MotionFor(gait),0,500000,1,locomotion);
    return {{pose.walkStride,pose.walkDuty,pose.walkLift}};
#endif
}
}

extern "C" {
void* gait_review_create(const std::uint32_t* pixels,int width,int height,int character) {
    auto* review=new Review;
    auto* rig=&review->rig;
    if(!rig->Prepare(pixels,width,height,width*4,cat::walk::ProfileFor(static_cast<cat::Character>(character)))) {
        delete review;return nullptr;
    }
    return review;
}
void gait_review_destroy(void* p) {delete static_cast<Review*>(p);}
int gait_review_raster(void* p,int gait,double distance,double width,double height) {
    const auto settings=ParametersFor(gait);
    return static_cast<Review*>(p)->rig.Raster(distance/settings[0],settings[0],width,height,settings[1],settings[2]);
}
void gait_review_reset(void* p) {
    auto& review=*static_cast<Review*>(p);review.locomotion.Reset();review.elapsed=0;
}
int gait_review_step(void* p,int gait,double deltaDistance,double deltaSeconds,double width,double height,double* parameters) {
    auto& review=*static_cast<Review*>(p);const auto motion=MotionFor(gait);
    if(!review.locomotion.Advance(motion,deltaDistance,deltaSeconds))return 0;
    review.elapsed+=deltaSeconds;
    const auto pose=cat::animation::Evaluate(motion,static_cast<std::uint64_t>(review.elapsed*1000),500000,1,review.locomotion);
    if(parameters) {
        parameters[0]=pose.walkStride;parameters[1]=pose.walkDuty;parameters[2]=pose.walkLift;
        parameters[3]=pose.walkPhase;parameters[4]=review.locomotion.Speed();
    }
    return review.rig.Raster(pose.walkPhase,pose.walkStride,width,height,pose.walkDuty,pose.walkLift
#ifdef GAIT_REVIEW_TRACKED_FEET
        ,pose.walkFeetValid?&pose.walkFeet:nullptr
#endif
    );
}
const std::uint32_t* gait_review_pixels(void* p) {return static_cast<Review*>(p)->rig.Pixels();}
int gait_review_width(void* p) {return static_cast<Review*>(p)->rig.Width();}
int gait_review_height(void* p) {return static_cast<Review*>(p)->rig.Height();}
double gait_review_inset(void* p) {return static_cast<Review*>(p)->rig.GroundInsetPixels();}
double gait_review_offset(void* p) {
#ifdef GAIT_REVIEW_NO_BODY_OFFSET
    (void)p;return 0;
#else
    return static_cast<Review*>(p)->rig.BodyOffset();
#endif
}
double gait_review_parameter(int gait,int parameter) {
    return ParametersFor(gait)[parameter==0?0:parameter==1?1:2];
}
void gait_review_mask(int character,double x,double y,double* output) {
    const auto masks=cat::walk::Rig::Masks(x,y,cat::walk::ProfileFor(static_cast<cat::Character>(character)));
    for(int i=0;i<4;++i)output[i]=i<static_cast<int>(masks.size())?masks[i]:0;
}
double gait_review_benchmark(void* p,int gait,int count,double width,double height) {
    if(count<=0)return -1;
    const auto settings=ParametersFor(gait);
    auto& rig=static_cast<Review*>(p)->rig;
    for(int i=0;i<8;++i)if(!rig.Raster(i/8.,settings[0],width,height,settings[1],settings[2]))return -1;
    const auto start=std::chrono::steady_clock::now();
    for(int i=0;i<count;++i)
        if(!rig.Raster((i%256)/256.,settings[0],width,height,settings[1],settings[2]))return -1;
    return std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count()/count;
}
}
