// Portable proof adapter for the production cursor pose and local reach rig.
// The Python driver only transports pixels and lays out review images.
#include "../src/cat_cursor_animation.hpp"
#include "../src/cat_cursor_reach_rig.hpp"

#include <algorithm>
#include <cstdint>
#include <exception>

namespace { const std::uint32_t* leftPart=nullptr;const std::uint32_t* rightPart=nullptr;const std::uint32_t* bodyPart=nullptr;int partWidth=0,partHeight=0; }
extern "C" void cursor_reach_set_arms(const std::uint32_t* left,const std::uint32_t* right,int width,int height) {
    leftPart=left;rightPart=right;partWidth=width;partHeight=height;
}
extern "C" void cursor_reach_set_body(const std::uint32_t* body) {bodyPart=body;}
extern "C" int cursor_reach_output_height(int sourceHeight) {
    return cat::cursor_rig::OutputHeight(sourceHeight);
}

extern "C" int cursor_reach_frame(const std::uint32_t* source, int width, int height,
                                  std::uint32_t* destination, int outWidth, int outHeight,
                                  int action, int elapsedMs, double targetX, double targetHeight,
                                  double* metadata) {
    if (!source || !destination || width < 1 || height < 1 || outWidth < 1 || outHeight < 1 ||
        elapsedMs < 0 || action < 0 || action > 1) return 0;
    try {
        const auto kind = action == 0 ? cat::cursor::AttackKind::Tiptoe : cat::cursor::AttackKind::BothPaws;
        const cat::cursor::Geometry geometry{static_cast<double>(width), static_cast<double>(height), .69};
        const auto pose = cat::cursor::AttackPose(kind, static_cast<std::uint64_t>(elapsedMs), 1,
                                                targetX, targetHeight, geometry);
        cat::cursor_rig::Source parts;
        if(!cat::cursor_rig::Prepare(parts,source,width,height))return 0;
        if(leftPart&&rightPart)cat::cursor_rig::SetArms(parts,leftPart,partWidth,partHeight,rightPart,partWidth,partHeight);
        if(bodyPart)cat::cursor_rig::SetBody(parts,bodyPart,partWidth,partHeight);
        if (!cat::cursor_rig::Render(parts, destination, outWidth, outHeight, pose.cursorReach)) return 0;
        if (metadata) {
            const auto left = cat::cursor_rig::PawUV(pose.cursorReach, 0);
            const auto right = cat::cursor_rig::PawUV(pose.cursorReach, 1);
            const auto leftHind = cat::cursor_rig::BodyPoint({.35, .97}, pose.cursorReach);
            const auto rightHind = cat::cursor_rig::BodyPoint({.79, .97}, pose.cursorReach);
            metadata[0] = pose.cursorReach.rise;
            metadata[1] = pose.cursorReach.leftLift;
            metadata[2] = pose.cursorReach.rightLift;
            metadata[3] = pose.cursorReach.aimX;
            metadata[4] = left.x; metadata[5] = left.y;
            metadata[6] = right.x; metadata[7] = right.y;
            metadata[8] = leftHind.x; metadata[9] = leftHind.y;
            metadata[10] = rightHind.x; metadata[11] = rightHind.y;
            metadata[12] = static_cast<double>(cat::cursor::ContactIndex(kind, elapsedMs));
            metadata[13] = static_cast<double>(cat::cursor::DurationMs(kind));
        }
        return 1;
    } catch (const std::exception&) {
        return 0;
    }
}
