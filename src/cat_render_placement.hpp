#pragma once

#include <cmath>

namespace cat::render {

struct Placement {
    int screenFeetY = 0;
    int windowTop = 0;
    double localBottom = 0;
};

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
    return {feet, feet - surfaceHeight,
            surfaceHeight - 1.0 - cosmeticLiftBodyPx * bodyScale};
}

} // namespace cat::render
