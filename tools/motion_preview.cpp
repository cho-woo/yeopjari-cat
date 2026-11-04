// Developer preview bridge: uses production pose, deformation and transform math.
// No Win32 application state, saved records or source artwork are modified.
#include "../src/cat_animation.hpp"
#include "../src/cat_deformation.hpp"
#include "../src/cat_render_geometry.hpp"
#include "../src/cat_belly_rig.hpp"

extern "C" {
int preview_evaluate(int motion, unsigned long long elapsed, int direction, double* output) {
    if (!output || motion < 0 || motion > static_cast<int>(cat::animation::Motion::Pride)) return 0;
    const auto pose = cat::animation::Evaluate(static_cast<cat::animation::Motion>(motion), elapsed, 10000 + elapsed, direction);
    const double values[] = {static_cast<double>(pose.frame), pose.x, pose.lift, pose.scaleX, pose.scaleY,
        pose.angle, static_cast<double>(pose.flip), static_cast<double>(pose.centerPivot),
        static_cast<double>(pose.blendFrame), pose.blend, static_cast<double>(pose.groundContact),
        pose.joints.leftEar, pose.joints.rightEar, pose.joints.headNod, pose.joints.frontLeft,
        pose.joints.frontRight, pose.joints.tailRaise, pose.joints.breath, pose.joints.crouch,
        static_cast<double>(pose.bellyRig)};
    for (int i=0;i<20;++i) output[i] = i >= 11 && i < 19 ? std::lround(values[i]*32)/32.0 : values[i];
    return 1;
}
int preview_warp(const std::uint32_t* source, int width, int height, std::uint32_t* destination,
                 const double* values, int shape) {
    if (!values || shape < 0 || shape > static_cast<int>(cat::deform::Shape::TailChase)) return 0;
    const cat::deform::Joints joints{values[0], values[1], values[2], values[3],
        values[4], values[5], values[6], values[7]};
    return cat::deform::Warp(source,width,height,width*4,destination,width*4,joints,
                            static_cast<cat::deform::Shape>(shape));
}
int preview_mask(const std::uint32_t* source, int width, int height, std::uint32_t* destination,
                 int frame, int head) {
    if (!source || !destination || width < 1 || height < 1) return 0;
    for (int y=0;y<height;++y) for (int x=0;x<width;++x) {
        const auto coverage=cat::belly::HeadCoverage(frame,{(x+.5)/width,(y+.5)/height});
        destination[y*width+x]=cat::belly::MaskPixel(source[y*width+x],coverage,head!=0);
    }
    return 1;
}
void preview_bone(int frame, double width, double height, double* output) {
    const auto bone=cat::belly::BoneFor(frame,width,height);
    output[0]=bone.x;output[1]=bone.y;output[2]=bone.angle;
}
void preview_blend_bone(int first,double firstWidth,double firstHeight,
                        int second,double secondWidth,double secondHeight,double weight,double* output) {
    const auto bone=cat::belly::BlendBone(first,firstWidth,firstHeight,second,secondWidth,secondHeight,weight);
    output[0]=bone.x;output[1]=bone.y;output[2]=bone.angle;
}
double preview_clearance(double width, double height, double angle, int centerPivot) {
    return cat::render::GroundClearance(width,height,angle,centerPivot != 0);
}
}
