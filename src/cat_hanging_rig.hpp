#pragma once
#include "cat_species_belly.hpp"
namespace cat::hanging {
inline double BodyWidthScale(Character character) {
    // Calibrated from opaque torso area, independent of tail/ear silhouette.
    // Hanging length and paw grip stay fixed; the painted narrow torso should
    // not lose roughly half its volume when the full-size head is registered.
    constexpr double widths[]{1.20,1.25,1.45,1.40,1.40,1.35,1.25,1.00,1.40,1.45};
    return IsValid(character)?widths[static_cast<int>(character)]:1.0;
}
inline double HeadCoverage(Character character,deform::Point uv) {
    const double coverage=belly::HeadCoverage(character,4,uv);
    const double distance=character==Character::Dog?.055:character==Character::Rabbit?.22:.14;
    const double taper=deform::Smooth((uv.y-(belly::Neckline(character)-distance))/distance);
    const double left=.04+(character==Character::Calico?.46:.39)*taper,right=1-.13*taper;
    return coverage*deform::Smooth((uv.x-left)/.025)*deform::Smooth((right-uv.x)/.025);
}
// The hanging artwork's neck is immediately above its gripping forepaws.
// Only the head bone moves; body height and the physical grip remain intact.
inline belly::HeadBone BoneFor(Character character,double bodyHeight,double canonicalHeight) {
    if(!std::isfinite(bodyHeight)||!std::isfinite(canonicalHeight)||bodyHeight<=0||canonicalHeight<=0)return {};
    const auto anchor=belly::HeadAnchor(character);
    return {0,-.57*bodyHeight-(belly::Neckline(character)-anchor.y)*canonicalHeight,0};
}
}
