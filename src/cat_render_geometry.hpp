#pragma once

#include <algorithm>
#include <array>
#include <cmath>

namespace cat::render {

struct Point { double x = 0, y = 0; };

// Coordinates are fractions of the alpha-cropped sprite, before mirroring and
// rotation. Accessories belong to each sprite layer, including crossfades.
// Crown is its bottom-centre attachment; neck is the ribbon/scarf centre.
struct OutfitAnchors { Point neck, crown; double headWidth = .65; };
inline OutfitAnchors AnchorsForFrame(int frame) {
    static constexpr std::array<OutfitAnchors, 24> anchors{{
        {{.67,.70},{.66,.16},.60}, {{.67,.70},{.66,.16},.60},
        {{.67,.70},{.66,.16},.60}, {{.67,.70},{.66,.16},.60},
        {{.57,.68},{.57,.14},.82}, {{.57,.68},{.57,.14},.82},
        {{.73,.85},{.68,.38},.57}, // curled sleeping head is low and to the right
        {{.65,.79},{.63,.38},.67}, // eating head is lowered toward the bowl
        {{.73,.85},{.72,.43},.62}, // stretching: tail is above the lowered head
        {{.61,.68},{.60,.16},.80}, {{.62,.68},{.60,.16},.80},
        {{.74,.83},{.72,.43},.62}, // stalking/crouching
        {{.57,.68},{.56,.16},.76}, {{.52,.70},{.52,.16},.72},
        {{.61,.68},{.61,.16},.80}, {{.54,.69},{.57,.17},.76},
        {{.66,.65},{.66,.17},.65}, {{.65,.66},{.65,.16},.72},
        {{.64,.68},{.61,.16},.74}, {{.64,.68},{.61,.16},.74},
        {{.56,.67},{.56,.16},.82}, {{.56,.67},{.56,.16},.82},
        {{.65,.67},{.65,.14},.72}, {{.65,.67},{.65,.14},.72},
    }};
    return anchors[static_cast<std::size_t>(std::clamp(frame, 0, 23))];
}

inline Point LocalPoint(Point anchor, double width, double height, bool centerPivot) {
    return {(anchor.x - .5) * width, (anchor.y - (centerPivot ? .5 : 1.0)) * height};
}

// The rotated rectangle must remain above the ground, including a belly roll
// around its centre. Previously only foot-pivot poses received this clearance.
inline double GroundClearance(double width, double height, double angle, bool centerPivot) {
    const double radians = angle * 3.14159265358979323846 / 180.0;
    const double s = std::abs(std::sin(radians)), c = std::cos(radians);
    if (centerPivot)
        return std::max(0.0, (width * s + height * std::abs(c) - height) / 2);
    return width * s / 2 + std::max(0.0, -height * c);
}

// Used by portable tests to mirror the renderer's actual transform, including
// foot clearance. width/height already include pose and nutrition scaling.
inline Point Transform(Point local, double center, double bottom, double width,
                       double height, double angle, bool flip, bool centerPivot) {
    const double radians = angle * 3.14159265358979323846 / 180.0;
    const double x = flip ? -local.x : local.x;
    return {center + x * std::cos(radians) - local.y * std::sin(radians),
            bottom - GroundClearance(width, height, angle, centerPivot)
                - (centerPivot ? height / 2 : 0)
                + x * std::sin(radians) + local.y * std::cos(radians)};
}

} // namespace cat::render
