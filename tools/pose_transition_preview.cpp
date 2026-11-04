// Compile once against the archived baseline headers and once against current
// production headers. The renderer calls the real Evaluate/Warp in both cases.
#include "cat_animation.hpp"
#ifndef PREVIEW_BASELINE
#include "cat_pose_transition.hpp"
static cat::animation::PoseTransition transition;
#endif

namespace {
cat::deform::Joints Joints(const double* values) {
    return {values[0],values[1],values[2],values[3],values[4],values[5],values[6],values[7]};
}
}

extern "C" {
void transition_preview_reset() {
#ifndef PREVIEW_BASELINE
    transition.Reset();
#endif
}
int transition_preview_pose(unsigned elapsed, unsigned identity, double* output) {
    if(!output || elapsed>1000) return 0;
    using namespace cat::animation;
    const bool loaf=elapsed<400;
    auto pose=Evaluate(loaf?Motion::Loaf:Motion::Idle,loaf?6000+elapsed:elapsed-400,10000+elapsed,1);
#ifndef PREVIEW_BASELINE
    pose=transition.Apply(pose,10000+elapsed,loaf?1:2,identity);
#else
    (void)identity;
#endif
    const double values[]={pose.joints.leftEar,pose.joints.rightEar,pose.joints.headNod,
        pose.joints.frontLeft,pose.joints.frontRight,pose.joints.tailRaise,pose.joints.breath,pose.joints.crouch};
    for(int i=0;i<8;++i) output[i]=std::lround(values[i]*32)/32.0;
    return pose.frame==4 && pose.blendFrame==-1 && pose.x==0 && pose.lift==0 &&
        pose.angle==0 && pose.scaleX==1 && pose.scaleY==1;
}
int transition_preview_warp(const std::uint32_t* source,int width,int height,std::uint32_t* destination,const double* values) {
    return source && destination && values && cat::deform::Warp(source,width,height,width*4,
        destination,width*4,Joints(values),cat::deform::Shape::Sitting);
}
void transition_preview_metrics(const double* values,double* output) {
    const auto joints=Joints(values);
    const auto a=cat::deform::SittingPoint({.40,.47},joints);
    const auto b=cat::deform::SittingPoint({.70,.45},joints);
    const auto foot=cat::deform::SittingPoint({.2,1},joints);
    output[0]=a.y;
    output[1]=std::hypot(a.x-b.x,a.y-b.y);
    output[2]=foot.y;
}
}
