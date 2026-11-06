#pragma once

#include "cat_sprite_assets.hpp"
#include <algorithm>
#include <cmath>
#include <limits>

namespace cat::render {

struct Placement {
    int screenFeetY = 0;
    int windowTop = 0;
    double localBottom = 0;
};

// Some authored props extend below the cat's support point. Reserve enough
// local canvas for every source rectangle in that clip, using the same common
// scale as the renderer. This changes drawing room, never the physical root.
inline int SpriteBottomSafetyInset(const cat::sprite_assets::Descriptor& asset,
                                   double renderedHeight, double bodyHeight=1) {
    if (!asset.frameCount || asset.frameCount>asset.frames.size() ||
            !std::isfinite(asset.referenceHeight) || asset.referenceHeight<=0 ||
            !std::isfinite(renderedHeight) || renderedHeight<=0 ||
            !std::isfinite(bodyHeight) || bodyHeight<=0) return 0;
    double sourceInset=0;
    for (unsigned index=0;index<asset.frameCount;++index) {
        const auto& frame=asset.frames[index];
        if (frame.rect.height<=0 || !std::isfinite(frame.anchor.y) ||
                frame.anchor.y<0 || frame.anchor.y>frame.rect.height) return 0;
        sourceInset=std::max(sourceInset,frame.rect.height-frame.anchor.y);
    }
    const double pixels=std::ceil(sourceInset*renderedHeight*bodyHeight/asset.referenceHeight);
    return std::isfinite(pixels) && pixels<=std::numeric_limits<int>::max() ?
        static_cast<int>(pixels):0;
}

inline Placement WithBottomSafetyInset(Placement placement, int inset) {
    inset=std::max(0,inset);
    placement.windowTop+=inset;
    placement.localBottom-=inset;
    return placement;
}

// Physical lift is measured in logical desktop pixels above the taskbar,
// including supported window ledges. It moves the entire layered window.
// Cosmetic lift is measured in body pixels and belongs only in local drawing
// coordinates. bodyScale already includes DPI and growth; physical lift must
// never depend on the cat's age or size. Keep the last local pixel as the
// contact margin used by the existing renderer.
inline Placement BuildPlacement(int workBottom, int surfaceHeight, double dpi,
                                double physicalLift, double cosmeticLiftBodyPx,
                                double bodyScale) {
    const int feet = workBottom - static_cast<int>(std::lround(physicalLift * dpi));
    // A layered window has integer screen coordinates, but its GDI+ content
    // does not. Keep the rounding remainder inside the surface so a slow
    // physical descent remains subpixel-smooth, as horizontal motion already
    // does. The one-pixel transparent contact margin stays unchanged.
    const double subpixel = workBottom - physicalLift*dpi - feet;
    return {feet, feet - surfaceHeight,
            surfaceHeight - 1.0 + subpixel - cosmeticLiftBodyPx * bodyScale};
}

} // namespace cat::render
