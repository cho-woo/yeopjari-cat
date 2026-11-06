#pragma once
#include "cat_sprite_assets.hpp"

// Shared by the portable fixture regression and the real Windows PNG/cache
// test. Keeping descriptor construction here makes malformed test metadata
// fail on every platform before the Windows renderer is exercised.
namespace cat::native_care_test {
constexpr unsigned CellWidth=24,CellHeight=32,ExposureMs=37;
inline cat::sprite_assets::Descriptor CapacityLayout(unsigned count) {
    cat::sprite_assets::Descriptor layout;
    if(!count||count>cat::sprite_assets::kMaxFrames||count%8)return layout;
    using namespace cat::sprite_sequence;
    layout.clip=Clip::Groom;layout.character=0;
    layout.resourceId=cat::sprite_assets::ResourceId(Clip::Groom,0);
    layout.columns=8;layout.rows=count/8;layout.frameCount=count;
    layout.imageWidth=8*CellWidth;layout.imageHeight=layout.rows*CellHeight;
    layout.referenceHeight=CellHeight;
    layout.program.stepCount=count;layout.program.intro={0,0};
    layout.program.loop={0,count};layout.program.outro={count,0};
    layout.program.finish=Finish::Hold;
    for(unsigned frame=0;frame<count;++frame) {
        layout.frames[frame]={{static_cast<int>(frame%8*CellWidth),static_cast<int>(frame/8*CellHeight),
            static_cast<int>(CellWidth),static_cast<int>(CellHeight)},{12,30}};
        layout.program.steps[frame]={frame,ExposureMs};
    }
    return layout;
}
constexpr std::uint32_t FrameColor(unsigned frame) {
    return 0xff000000u|(((frame*73+41)&255)<<16)|(((frame*31+109)&255)<<8)|((frame*17+67)&255);
}
} // namespace cat::native_care_test
