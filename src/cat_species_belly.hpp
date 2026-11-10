#pragma once

#include "cat_belly_rig.hpp"
#include "cat_preferences.hpp"

namespace cat::belly {

inline bool SpeciesRig(Character character) {
    return character!=Character::OddEye && IsValid(character) && static_cast<int>(character) >= 2;
}
inline deform::Point HeadAnchor(Character character) {
    return SpeciesRig(character) ? deform::Point{.70,.42} : deform::Point{.57,.40};
}
inline double Neckline(Character character) {
    constexpr double neck[]{.61,.64,.56,.67,.58,.70,.59,.56};
    return SpeciesRig(character) ? neck[static_cast<int>(character)-2] : .70;
}
inline double HeadEllipse(deform::Point point,double x,double y,double rx,double ry) {
    return 1-deform::Smooth((std::hypot((point.x-x)/rx,(point.y-y)/ry)-.90)/.14);
}

inline double HeadCoverage(Character character,int frame,deform::Point uv) {
    if (frame==26) {
        const double coverage=HeadCoverage(frame,uv);
        if (character!=Character::Rabbit || !std::isfinite(uv.x) || !std::isfinite(uv.y) ||
            uv.x<0 || uv.x>1 || uv.y<0 || uv.y>1) return coverage;
        // The rabbit's long original cheek extends right of the replacement
        // head. Remove its short shoulder-height wedge above the curved paw
        // outline: the outer right paw starts lower than its central knuckle.
        const double pawTop=.453+.25*std::max(0.0,uv.x-.80);
        const double cheek=deform::Smooth((uv.x-.70)/.04)*
            (1-deform::Smooth((uv.y-pawTop)/.002));
        return std::max(coverage,cheek);
    }
    if (!SpeciesRig(character)) return HeadCoverage(frame,uv);
    if (!std::isfinite(uv.x) || !std::isfinite(uv.y) || uv.x<0 || uv.x>1 || uv.y<0 || uv.y>1) return 0;
    if (frame==4) {
        // The new sitting sheets put a raised tail left of the cheek. A full
        // horizontal head slice carried that tail into the moving head layer.
        // Keep every ear tip above the cheek while excluding the lower tail.
        const bool broadHead=character==Character::Dog || character==Character::Rabbit;
        const double left=broadHead ? .04 : .18+.13*deform::Smooth((uv.y-.30)/.16);
        return deform::Smooth((uv.x-left)/.035)*
            (1-deform::Smooth((uv.y-(Neckline(character)-.025))/.05));
    }
    if (frame==24) {
        double coverage=HeadEllipse(uv,.76,.53,.36,.75);
        // Side-pose ears extend left/up beyond the face ellipse. Removing only
        // the face left a third ear behind the sole canonical head.
        coverage=std::max(coverage,HeadEllipse(uv,.52,.18,.18,.26));
        if (character==Character::Rabbit)
            coverage=std::max(coverage,HeadEllipse(uv,.48,.22,.25,.30));
        return coverage;
    }
    if (frame==25) {
        double coverage=HeadEllipse(uv,.79,.43,.32,.65);
        // The old floor exclusion retained a detached chin/ear strip on the
        // puppy. These pixels belong to the replaced head, including its base.
        return std::max(coverage,deform::Smooth((uv.x-.60)/.06)*deform::Smooth((uv.y-.68)/.12));
    }
    return 0;
}

inline HeadBone BoneFor(Character character,int frame,double width,double height) {
    if (!SpeciesRig(character) || (frame!=4 && frame!=24 && frame!=25)) return BoneFor(frame,width,height);
    if (!std::isfinite(width) || !std::isfinite(height) || width<=0 || height<=0) return {};
    // Measured with the alpha-cropped canonical/side/back artwork, at the
    // existing 110px reference size. Include the 12-degree back-head rotation;
    // the masked head's lower support stays about two logical pixels above
    // the floor. A shared .55 Y detached short heads and sank rabbit chins.
    constexpr double side[]{.64514,.60188,.72812,.57148,.69584,.47410,.67810,.73601};
    constexpr double back[]{.67337,.62355,.75820,.58081,.70510,.59401,.71900,.76128};
    const auto index=static_cast<int>(character)-2;
    const double x=frame==4 ? .70 : .72;
    const double y=frame==4 ? .42 : frame==24 ? side[index] : back[index];
    return {(x-.5)*width,(y-1)*height,frame==25 ? 12.0 : 0.0};
}

inline HeadBone BlendBone(Character character,int first,double firstWidth,double firstHeight,
                          int second,double secondWidth,double secondHeight,double weight) {
    const auto a=BoneFor(character,first,firstWidth,firstHeight);
    const auto b=BoneFor(character,second,secondWidth,secondHeight);
    const double t=std::isfinite(weight) ? std::clamp(weight,0.0,1.0) : 0.0;
    return {a.x+(b.x-a.x)*t,a.y+(b.y-a.y)*t,a.angle+(b.angle-a.angle)*t};
}

} // namespace cat::belly
