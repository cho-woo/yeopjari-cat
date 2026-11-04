#include "../src/cat_render_geometry.hpp"
#include "../src/cat_animation.hpp"
#include <cmath>
#include <array>
#include <vector>
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
    for (int frame = 0; frame < cat::animation::kSpriteCount; ++frame) {
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
    CHECK(rollStart.frame == 4 && rollStart.blend == 0 && rollStart.angle == 0);
    CHECK(rollEnd.frame == 4 && rollEnd.blendFrame == -1 && rollEnd.angle == 0);
    CHECK(rollStart.lift == 0 && rollEnd.lift == 0);
    for (unsigned boundary : {520U,1120U,3140U,3740U,static_cast<unsigned>(kBellyRollDurationMs)}) {
        const auto before = Evaluate(Motion::BellyRoll,boundary-1,boundary-1,1);
        const auto after = Evaluate(Motion::BellyRoll,boundary,boundary,1);
        const auto weight = [](const Pose& p, int frame) {
            return (p.frame == frame ? 1-p.blend : 0) + (p.blendFrame == frame ? p.blend : 0);
        };
        for (int frame : {4,24,25}) CHECK(std::abs(weight(before,frame)-weight(after,frame)) < .001);
    }
    for (int index=0; index<=static_cast<int>(Motion::BoxSit); ++index) {
        for (unsigned t=0; t<8000; t+=23) {
            const auto pose = Evaluate(static_cast<Motion>(index),t,t,1);
            CHECK(std::isfinite(pose.lift) && std::abs(pose.angle) <= 6);
            CHECK(pose.frame >= 0 && pose.frame < kSpriteCount);
            CHECK(pose.scaleX >= .9 && pose.scaleX <= 1.1);
            CHECK(pose.scaleY >= .75 && pose.scaleY <= 1.05);
        }
    }
    for (auto motion : {Motion::Groom,Motion::Watch}) for (unsigned t=0;t<3000;t+=37)
        CHECK(std::abs(Evaluate(motion,t,t,1).angle+Evaluate(motion,t,t,-1).angle) < 1e-12);
}

cat::deform::Shape ShapeForFrame(int frame) {
    return frame == 24 ? cat::deform::Shape::ReclineSide : frame == 25
        ? cat::deform::Shape::ReclineBack : cat::deform::Shape::Sitting;
}
cat::render::Point WorldPoint(cat::deform::Point uv, const cat::animation::Pose& pose,
                              int frame, double width, double height) {
    const auto deformed = cat::deform::DeformPoint(uv, pose.joints, ShapeForFrame(frame));
    width *= pose.scaleX; height *= pose.scaleY;
    const auto local = cat::render::LocalPoint({deformed.x,deformed.y},width,height,pose.centerPivot);
    return cat::render::Transform(local,130,209-pose.lift,width,height,pose.angle,pose.flip,pose.centerPivot);
}

void ScheduledEarMotionDoesNotMorphFaceBodyOrFeet() {
    using namespace cat::animation;
    const std::array<cat::deform::Point, 5> fixed{{{.57,.50},{.57,.76},{.43,.97},{.69,.97},{.12,.76}}};
    for (int direction : {-1,1}) for (unsigned t=0;t<=kEarFlickDurationMs;t+=11) {
        const auto pose=Evaluate(Motion::EarFlick,t,t,direction);
        CHECK(pose.frame == 4 && pose.blendFrame == -1 && pose.scaleX == 1 && pose.scaleY == 1);
        CHECK(pose.angle == 0 && pose.lift == 0 && !pose.centerPivot);
        for (const auto uv : fixed) {
            const auto point=cat::deform::SittingPoint(uv,pose.joints);
            CHECK(point.x==uv.x && point.y==uv.y);
        }
    }
    const auto first=Evaluate(Motion::EarFlick,360,360,1);
    const auto second=Evaluate(Motion::EarFlick,1170,1170,1);
    CHECK(cat::deform::SittingPoint({.23,.07},first.joints).y > .07);
    CHECK(cat::deform::SittingPoint({.82,.07},first.joints).y == .07);
    CHECK(cat::deform::SittingPoint({.23,.07},second.joints).y == .07);
    CHECK(cat::deform::SittingPoint({.82,.07},second.joints).y > .07);

    // Exercise the scheduled controls through the rasterizer, not only its
    // generic all-controls fixture. Body/face rows must stay byte-identical.
    constexpr int width=65,height=91;
    std::vector<std::uint32_t> source(width*height),output(width*height);
    for(int y=0;y<height;++y) for(int x=0;x<width;++x)
        source[y*width+x]=0xff000000U | static_cast<unsigned>((x*19+y*7)&255)
            | (static_cast<unsigned>((x*3+y*23)&255)<<8);
    for (unsigned t : {0U,235U,360U,800U,1035U,1170U,2200U}) {
        const auto pose=Evaluate(Motion::EarFlick,t,t,1);
        CHECK(cat::deform::Warp(source.data(),width,height,width*4,output.data(),width*4,pose.joints));
        bool changedEar=false;
        for(int y=0;y<height;++y) for(int x=0;x<width;++x) {
            if ((y+.5)/height>=.36) CHECK(output[y*width+x]==source[y*width+x]);
            else changedEar=changedEar || output[y*width+x]!=source[y*width+x];
        }
        CHECK(changedEar == !cat::deform::Zero(pose.joints));
    }
}

void BellySupportAndHeadKeepTheirWorldGeometry() {
    using namespace cat::animation;
    for (int frame : {24,25}) {
        const auto anchors=cat::render::AnchorsForFrame(frame);
        // Reclining heads occupy the rightmost third of the wide artwork.
        // Reusing frame23's sitting/tail anchors puts accessories on the torso.
        CHECK(anchors.neck.x > .70 && anchors.crown.x > .70);
        CHECK(anchors.headWidth < .65);
    }
    for(int direction : {-1,1}) for(unsigned t=0;t<=kBellyRollDurationMs;t+=17) {
        const auto pose=Evaluate(Motion::BellyRoll,t,t,direction);
        CHECK(pose.angle==0 && pose.scaleX==1 && pose.scaleY==1 && !pose.centerPivot && pose.lift==0);
        for (int frame : {pose.frame,pose.blendFrame}) {
            if(frame<0 || frame==4) continue;
            CHECK(frame==24 || frame==25);
            for(double scale : {.56,1.,1.5,2.}) {
                const double width=140*scale,height=72*scale;
                for(double x : {.20,.42,.65}) {
                    const auto support=WorldPoint({x,1},pose,frame,width,height);
                    CHECK(std::abs(support.y-209)<1e-9);
                }
                const auto head=WorldPoint({.79,.47},pose,frame,width,height);
                CHECK(head.y<209-30*scale);
                const auto opposite=Evaluate(Motion::BellyRoll,t,t,-direction);
                const auto mirror=WorldPoint({.79,.47},opposite,frame,width,height);
                CHECK(std::abs(head.x+mirror.x-260)<1e-9 && std::abs(head.y-mirror.y)<1e-9);
            }
        }
    }
}

void ScheduledJointsKeepHeadShapeAndContactLegs() {
    using namespace cat::animation;
    for(int index=0;index<=static_cast<int>(Motion::BoxSit);++index) for(unsigned t=0;t<=8000;t+=47) {
        const auto pose=Evaluate(static_cast<Motion>(index),t,t,1);
        for(int frame : {pose.frame,pose.blendFrame}) {
            if(frame<0) continue;
            const bool reclining=frame>=24;
            const auto shape=ShapeForFrame(frame);
            const double x0=reclining ? .69 : .35, x1=reclining ? .89 : .78;
            const double y0=reclining ? .30 : .38, y1=reclining ? .65 : .60;
            const auto a=cat::deform::DeformPoint({x0,y0},pose.joints,shape);
            const auto b=cat::deform::DeformPoint({x1,y0},pose.joints,shape);
            const auto c=cat::deform::DeformPoint({x0,y1},pose.joints,shape);
            const double area=(b.x-a.x)*(c.y-a.y)-(b.y-a.y)*(c.x-a.x);
            const double original=(x1-x0)*(y1-y0);
            CHECK(area>original*.9 && area<original*1.1);
            const double headWidth=std::hypot(b.x-a.x,b.y-a.y);
            CHECK(headWidth>(x1-x0)*.95 && headWidth<(x1-x0)*1.05);
        }
    }
    for(unsigned t=0;t<1800;t+=13) {
        const auto pose=Evaluate(Motion::Knead,t,t,1);
        const auto left=cat::deform::SittingPoint({.43,.97},pose.joints);
        const auto right=cat::deform::SittingPoint({.69,.97},pose.joints);
        CHECK(std::max(left.y,right.y)==.97);
        CHECK(std::min(left.y,right.y)>=.947);
    }
}
}
int main() {
    try {
        RotatedSpritesStayAboveGround();
        AccessoriesFollowTheSameMirrorRotationAndScale();
        NewPosesKeepTheirIntendedBaseline();
        ScheduledEarMotionDoesNotMorphFaceBodyOrFeet();
        BellySupportAndHeadKeepTheirWorldGeometry();
        ScheduledJointsKeepHeadShapeAndContactLegs();
        std::cout << "Render geometry: " << checks << " checks passed\n";
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
