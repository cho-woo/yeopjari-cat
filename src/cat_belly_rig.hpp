#pragma once

#include "cat_deformation.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace cat::belly {

inline double HeadCoverage(int frame,deform::Point uv) {
    if (!std::isfinite(uv.x) || !std::isfinite(uv.y) || uv.x<0 || uv.x>1 || uv.y<0 || uv.y>1) return 0;
    if (frame==4) {
        // Keep the tail/torso below the neckline. The head includes both ears,
        // eyes, nose and cheeks, with a narrow feather at the neck attachment.
        const double neckline=.64+.065*deform::Bump(uv.x,.57,.45);
        return 1-deform::Smooth((uv.y-(neckline-.01))/.02);
    }
    if (frame==24 || frame==25) {
        // Cut the drawn face out of each reclined body; the renderer supplies
        // exactly one canonical head following its interpolated bone.
        if (uv.y>=.95) return 0;
        const double cx=frame==24 ? .77 : .79;
        const double radius=std::hypot((uv.x-cx)/.285,(uv.y-.40)/.48);
        const double ellipse=1-deform::Smooth((radius-.88)/.18);
        return ellipse*(1-deform::Smooth((uv.y-.93)/.02));
    }
    if(frame==26) {
        // Keep the gripping paws and dangling body at their physical height;
        // only the small painted head is replaced by the full-size canonical one.
        return 1-deform::Smooth((uv.y-.415)/.030);
    }
    return 0;
}

// The head is drawn source-over the body, not added to it. Retain enough
// residual body alpha to reproduce the source through the head's opacity;
// a simple (1-coverage) mask would create a transparent neck seam.
inline std::uint32_t MaskPixel(std::uint32_t pixel,double coverage,bool head) {
    coverage=std::isfinite(coverage) ? std::clamp(coverage,0.0,1.0) : 0.0;
    if (coverage==0) return head ? 0 : pixel;
    if (coverage==1) return head ? pixel : 0;
    const unsigned alpha=pixel>>24;
    if (!alpha) return 0;
    const unsigned headAlpha=static_cast<unsigned>(std::floor(alpha*coverage+.5));
    if (head) {
        std::uint32_t result=headAlpha<<24;
        for (int channel=0;channel<3;++channel) {
            const unsigned value=(pixel>>(channel*8))&255;
            const unsigned part=static_cast<unsigned>(std::floor(value*coverage+.5));
            result|=std::min(part,headAlpha)<<(channel*8);
        }
        return result;
    }
    if (headAlpha==255) return 0;
    const unsigned denominator=255-headAlpha;
    const unsigned bodyAlpha=((alpha-headAlpha)*255+denominator/2)/denominator;
    std::uint32_t result=bodyAlpha<<24;
    for (int channel=0;channel<3;++channel) {
        const unsigned value=(pixel>>(channel*8))&255;
        // Solve the colour source-over equation too. Scaling only by
        // bodyAlpha/alpha can accumulate two rounding errors on faint fur.
        const unsigned headPart=static_cast<unsigned>(std::floor(value*coverage+.5));
        const unsigned part=((value-headPart)*255+denominator/2)/denominator;
        result|=std::min(part,bodyAlpha)<<(channel*8);
    }
    return result;
}

struct HeadBone { double x=0,y=0,angle=0; };
inline HeadBone BoneFor(int frame,double width,double height) {
    if (!std::isfinite(width) || !std::isfinite(height) || width<=0 || height<=0) return {};
    // Coordinates relative to the sprite bottom-centre, before facing/DPI.
    // Base frame4's head centre is higher than the neckline; it is the same
    // point used when positioning the sole canonical head texture.
    double x=.57,y=.40,angle=0;
    if (frame==24) { x=.77;y=.55; }
    else if (frame==25) { x=.79;y=.55;angle=12; }
    return {(x-.5)*width,(y-1)*height,angle};
}
inline HeadBone BlendBone(int first,double firstWidth,double firstHeight,
                          int second,double secondWidth,double secondHeight,double weight) {
    const auto a=BoneFor(first,firstWidth,firstHeight),b=BoneFor(second,secondWidth,secondHeight);
    const double t=std::isfinite(weight) ? std::clamp(weight,0.0,1.0) : 0.0;
    return {a.x+(b.x-a.x)*t,a.y+(b.y-a.y)*t,a.angle+(b.angle-a.angle)*t};
}

} // namespace cat::belly
