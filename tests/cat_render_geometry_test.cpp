#include "../src/cat_render_geometry.hpp"
#include "../src/cat_animation.hpp"
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {
int checks = 0;
#define CHECK(c) do { ++checks; if (!(c)) throw std::runtime_error(std::string(__FILE__) + ":" + std::to_string(__LINE__) + " " #c); } while (false)
void RotatedSpritesStayAboveGround() {
    for (double width : {70., 110., 145.}) for (double height : {65., 110.})
    for (double angle = -180; angle <= 180; angle += 3)
    for (bool centre : {false,true}) for (bool flip : {false,true}) {
        for (double x : {0.,1.}) for (double y : {0.,1.}) {
            auto local = cat::render::LocalPoint({x,y}, width,height,centre);
            auto point = cat::render::Transform(local,130,209,width,height,angle,flip,centre);
            CHECK(point.y <= 209 + 1e-9);
            CHECK(std::isfinite(point.x) && std::isfinite(point.y));
        }
    }
}
void AccessoriesFollowTheSameMirrorRotationAndScale() {
    for (int frame = 0; frame < 24; ++frame) {
        const auto anchors = cat::render::AnchorsForFrame(frame);
        CHECK(anchors.crown.y < anchors.neck.y);
        for (auto anchor : {anchors.neck, anchors.crown}) {
            CHECK(anchor.x > 0 && anchor.x < 1 && anchor.y > 0 && anchor.y < 1);
            for (bool centre : {false,true}) {
                auto local = cat::render::LocalPoint(anchor,100,110,centre);
                auto right = cat::render::Transform(local,130,209,100,110,20,false,centre);
                auto left = cat::render::Transform(local,130,209,100,110,-20,true,centre);
                CHECK(std::abs(right.x + left.x - 260) < 1e-9);
                CHECK(std::abs(right.y-left.y) < 1e-9);
                // Changing DPI/growth or body proportions affects attachment and
                // sprite through the same geometry; no fixed screen-height anchor.
                auto big = cat::render::LocalPoint(anchor,150,220,centre);
                CHECK(std::abs(big.x-local.x*1.5) < 1e-9);
                CHECK(std::abs(big.y-local.y*2) < 1e-9);
            }
        }
    }
    const auto sit = cat::render::AnchorsForFrame(14);
    const auto crouch = cat::render::AnchorsForFrame(11);
    CHECK(crouch.neck.y > sit.neck.y);
    CHECK(crouch.crown.y > sit.crown.y);
}
void NewPosesKeepTheirIntendedBaseline() {
    using namespace cat::animation;
    CHECK(kPeekDurationMs == 3600);
    CHECK(kBellyRollDurationMs == 4300);
    CHECK(Evaluate(Motion::Peek,0,0,1).lift == 0);
    CHECK(Evaluate(Motion::Peek,3600,3600,1).lift == 0);
    CHECK(Evaluate(Motion::Peek,999999,999999,1).lift == 0);
    CHECK(Evaluate(Motion::Peek,1800,1800,1).lift < -30);
    for (unsigned t=0; t<3600; t+=16) {
        CHECK(Evaluate(Motion::Loaf,t,t,1).lift >= 0);
        CHECK(std::abs(Evaluate(Motion::Peek,t+1,t+1,1).lift - Evaluate(Motion::Peek,t,t,1).lift) < .06);
    }
    const auto rollStart = Evaluate(Motion::BellyRoll,0,0,1);
    const auto rollEnd = Evaluate(Motion::BellyRoll,kBellyRollDurationMs,0,1);
    CHECK(rollStart.frame == 14 && rollStart.blend == 0 && rollStart.angle == 0);
    CHECK(rollEnd.frame == 14 && rollEnd.blendFrame == -1 && rollEnd.angle == 0);
    CHECK(std::abs(rollStart.lift) < 1e-9 && std::abs(rollEnd.lift) < 1e-9);
    for (unsigned boundary : {240U, static_cast<unsigned>(kBellyRollDurationMs-240),
                              static_cast<unsigned>(kBellyRollDurationMs)}) {
        const auto before = Evaluate(Motion::BellyRoll,boundary-1,boundary-1,1);
        const auto after = Evaluate(Motion::BellyRoll,boundary,boundary,1);
        const auto weight = [](const Pose& p, int frame) {
            return (p.frame == frame ? 1-p.blend : 0) + (p.blendFrame == frame ? p.blend : 0);
        };
        CHECK(std::abs(weight(before,6)-weight(after,6)) < .001);
        CHECK(std::abs(weight(before,14)-weight(after,14)) < .001);
    }
    for (int index=0; index<=static_cast<int>(Motion::BoxSit); ++index) {
        for (unsigned t=0; t<8000; t+=23) {
            const auto pose = Evaluate(static_cast<Motion>(index),t,t,1);
            CHECK(std::isfinite(pose.lift) && std::isfinite(pose.angle));
            CHECK(pose.frame >= 0 && pose.frame < 24);
            CHECK(pose.scaleX > 0 && pose.scaleY > 0);
        }
    }
    for (auto motion : {Motion::Groom,Motion::Watch}) for (unsigned t=0;t<3000;t+=37)
        CHECK(std::abs(Evaluate(motion,t,t,1).angle+Evaluate(motion,t,t,-1).angle) < 1e-12);
}
}
int main() {
    try {
        RotatedSpritesStayAboveGround();
        AccessoriesFollowTheSameMirrorRotationAndScale();
        NewPosesKeepTheirIntendedBaseline();
        std::cout << "Render geometry: " << checks << " checks passed\n";
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
