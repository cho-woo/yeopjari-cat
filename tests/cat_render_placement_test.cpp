#include "../src/cat_render_placement.hpp"
#include "../src/cat_animation.hpp"
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {
int checks = 0;
#define CHECK(c) do { ++checks; if (!(c)) throw std::runtime_error( \
    std::string(__FILE__) + ":" + std::to_string(__LINE__) + " " #c); } while (false)

void PhysicalAltitudeMovesTheSurfaceExactlyOnce() {
    // Negative origins cover a monitor positioned above the primary monitor.
    for (int workBottom : {-1080, -60, 0, 1040, 1440, 3240})
    for (double dpi : {1., 1.25, 1.5, 2., 3.})
    for (double growth : {.55, .8, 1., 1.1}) {
        const int surfaceHeight = static_cast<int>(std::lround(300 * dpi));
        const auto floor = cat::render::BuildPlacement(workBottom, surfaceHeight, dpi, 0, 0, dpi * growth);
        CHECK(floor.windowTop + surfaceHeight == workBottom);
        CHECK(floor.windowTop + floor.localBottom == workBottom - 1);
        for (int quarterPixel = 0; quarterPixel <= 2400; ++quarterPixel) {
            const double lift = quarterPixel / 4.;
            const auto placed = cat::render::BuildPlacement(workBottom, surfaceHeight, dpi, lift, 0, dpi * growth);
            const double screenContact = placed.windowTop + placed.localBottom;
            CHECK(placed.localBottom == floor.localBottom);
            CHECK(placed.windowTop + surfaceHeight == placed.screenFeetY);
            CHECK(screenContact == placed.screenFeetY - 1);
            // The entire world-space altitude is applied once, with at most
            // half a physical pixel of layered-window position quantization.
            CHECK(std::abs(screenContact - (workBottom - lift * dpi - 1)) <= .5);
            // A high jump still has the same drawing room as a floor pose.
            // The former fixed window / local physical lift implementation
            // clipped even a 110px cat completely at a 350px jump height.
            CHECK(placed.localBottom >= 150 * dpi * growth);
            CHECK(placed.localBottom < surfaceHeight);
        }
    }
}

void GrowthAndCosmeticMotionCannotRescalePhysicalHeight() {
    for (double dpi : {1., 1.25, 1.5, 2., 3.})
    for (double lift : {0., 180., 350., 520., 600.}) {
        const int surfaceHeight = static_cast<int>(std::lround(300 * dpi));
        const auto base = cat::render::BuildPlacement(-80, surfaceHeight, dpi, lift, 0, dpi);
        for (double growth : {.55, 1., 1.1})
        for (double cosmeticLift : {-38., -12., 0., 2., 20.}) {
            const auto placed = cat::render::BuildPlacement(-80, surfaceHeight, dpi, lift, cosmeticLift, dpi * growth);
            CHECK(placed.windowTop == base.windowTop && placed.screenFeetY == base.screenFeetY);
            CHECK(std::abs((placed.localBottom - base.localBottom) + cosmeticLift * dpi * growth) < 1e-9);
            CHECK(std::abs((placed.windowTop + placed.localBottom) -
                (base.screenFeetY - 1 - cosmeticLift * dpi * growth)) < 1e-9);
        }
        // Resizing the offscreen surface changes only local coordinates.
        const auto taller = cat::render::BuildPlacement(-80, surfaceHeight + 173, dpi, lift, 0, dpi);
        CHECK(taller.screenFeetY == base.screenFeetY);
        CHECK(taller.windowTop + taller.localBottom == base.windowTop + base.localBottom);
    }
}

void EveryScheduledMotionUsesTheSamePhysicalSupportCoordinate() {
    using cat::animation::Motion;
    for (double dpi : {1., 1.25, 1.5, 2., 3.})
    for (int index = 0; index <= static_cast<int>(Motion::BoxSit); ++index)
    for (unsigned elapsed = 0; elapsed <= 8000; elapsed += 17) {
        const auto pose = cat::animation::Evaluate(static_cast<Motion>(index), elapsed, elapsed, 1);
        const int surfaceHeight = static_cast<int>(std::lround(300 * dpi));
        const auto ground = cat::render::BuildPlacement(1040, surfaceHeight, dpi, 0, pose.lift, dpi);
        const auto ledge = cat::render::BuildPlacement(1040, surfaceHeight, dpi, 180, pose.lift, dpi);
        const auto high = cat::render::BuildPlacement(1040, surfaceHeight, dpi, 600, pose.lift, dpi);
        CHECK(std::isfinite(ground.localBottom));
        CHECK(ground.localBottom == ledge.localBottom && ground.localBottom == high.localBottom);
        CHECK(std::abs((ground.windowTop - ledge.windowTop) - 180 * dpi) < 1e-9);
        CHECK(std::abs((ground.windowTop - high.windowTop) - 600 * dpi) < 1e-9);
        // Positive physical height does not turn a resting ledge action into
        // an airborne/cosmetically raised pose. The same art and support point
        // apply to floor and window contact.
        if (static_cast<Motion>(index) == Motion::Loaf ||
            static_cast<Motion>(index) == Motion::BellyRoll ||
            static_cast<Motion>(index) == Motion::Knead) {
            CHECK(pose.groundContact && pose.lift == 0);
            CHECK(ledge.windowTop + ledge.localBottom == ledge.screenFeetY - 1);
        }
    }
}
}

int main() {
    try {
        PhysicalAltitudeMovesTheSurfaceExactlyOnce();
        GrowthAndCosmeticMotionCannotRescalePhysicalHeight();
        EveryScheduledMotionUsesTheSamePhysicalSupportCoordinate();
        std::cout << "Render placement: " << checks << " checks passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
