// Portable developer preview bridge, compiled twice for archived/current code.
// The Python driver provides the include directory; no production code is copied.
#ifdef PREVIEW_BASELINE
#define cat archived_cat
#endif
#include "cat_animation.hpp"
#ifndef PREVIEW_BASELINE
#include "cat_locomotion_rig.hpp"
#endif
#include <chrono>

extern "C" {
int walking_preview_pose(double distance, double* out) {
    if(!out||!std::isfinite(distance)||distance<0)return 0;
#ifdef PREVIEW_BASELINE
    // Replay the driver's actual 0.875px/40ms walking samples. Inventing
    // 40px/100ms chunks changes the speed history in adaptive gait versions.
    cat::animation::Locomotion gait;
    constexpr double speed=21.875,step=.875;
    while (distance > step) {
        if (!gait.Advance(cat::animation::Motion::Walk, step, .04)) return 0;
        distance -= step;
    }
    if (!gait.Advance(cat::animation::Motion::Walk, distance, distance/speed)) return 0;
    const auto pose = cat::animation::Evaluate(cat::animation::Motion::Walk, 0, 0, 1, gait);
    out[0] = pose.frame; out[1] = pose.blendFrame; out[2] = pose.blend;
#else
    // This original tool compares a steady ordinary walk and scalar phase
    // sheets. Dynamic speed/start/stop reviews use gait_review_step instead.
    const auto settings=cat::gait::ForSpeed(false,21.875);
    out[0]=std::fmod(distance/settings.stride,1.0);out[1]=settings.stride;
    out[2]=settings.duty;out[3]=settings.lift;
#endif
    return 1;
}
#ifndef PREVIEW_BASELINE
void* walking_preview_create(const std::uint32_t* source, int width, int height, int character) {
    auto* rig = new cat::walk::Rig;
    if (!rig->Prepare(source, width, height, width * 4,
                      cat::walk::ProfileFor(static_cast<cat::Character>(character)))) {
        delete rig;
        return nullptr;
    }
    return rig;
}
void walking_preview_destroy(void* p) { delete static_cast<cat::walk::Rig*>(p); }
int walking_preview_raster(void* p, const double* pose, double width, double height) {
    return static_cast<cat::walk::Rig*>(p)->Raster(pose[0], pose[1], width, height, pose[2], pose[3]);
}
const std::uint32_t* walking_preview_pixels(void* p) { return static_cast<cat::walk::Rig*>(p)->Pixels(); }
int walking_preview_width(void* p) { return static_cast<cat::walk::Rig*>(p)->Width(); }
int walking_preview_height(void* p) { return static_cast<cat::walk::Rig*>(p)->Height(); }
double walking_preview_ground_inset(void* p) { return static_cast<cat::walk::Rig*>(p)->GroundInsetPixels(); }
double walking_preview_body_offset(void* p) { return static_cast<cat::walk::Rig*>(p)->BodyOffset(); }
double walking_preview_benchmark(void* p, int count, double width, double height) {
    if (count <= 0) return -1;
    auto& rig = *static_cast<cat::walk::Rig*>(p);
    const auto start = std::chrono::steady_clock::now();
    for (int i = 0; i < count; ++i)
        if (!rig.Raster((i % 256) / 256., 24, width, height, .68, 3.2)) return -1;
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count() / count;
}
#endif
}
