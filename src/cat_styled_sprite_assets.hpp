#pragma once

#include "cat_sprite_assets.hpp"
#include <array>
#include <cstddef>

namespace cat::styled_assets {
// Authored pixels for an existing atlas. Animation registration is owned by the
// base descriptor: a style never supplies a second program, stride or anchor.
// This also applies to the canonical base/action/gesture/fallback PNG atlases.
struct Atlas {
    unsigned character=0;
    int baseResourceId=0,resourceId=0;
    unsigned imageWidth=0,imageHeight=0;
    const char* sha256="";
    const char* sourcePath="";
    const sprite_assets::Anchor* anchors=nullptr;
    unsigned anchorCount=0;
    double referenceHeight=0;
};
constexpr bool Valid(const Atlas& atlas) {
    if(atlas.character==0||atlas.character>=sprite_assets::kCharacters||
       atlas.baseResourceId<=0||atlas.resourceId<500||atlas.resourceId==atlas.baseResourceId||
       atlas.imageWidth==0||atlas.imageHeight==0||
       atlas.imageWidth>sprite_assets::kMaxSide||atlas.imageHeight>sprite_assets::kMaxSide||
       static_cast<std::uint64_t>(atlas.imageWidth)*atlas.imageHeight>sprite_assets::kMaxImagePixels||
       atlas.anchorCount>sprite_assets::kMaxFrames||(atlas.anchorCount>0&&!atlas.anchors)||
       !(atlas.referenceHeight>=0&&atlas.referenceHeight<=sprite_assets::kMaxSide))
        return false;
    return true;
}
constexpr bool Fits(const Atlas& atlas,const sprite_assets::Descriptor& base) {
    if(!Valid(atlas)||!sprite_assets::Valid(base)||atlas.baseResourceId!=base.resourceId||
            atlas.imageWidth!=base.imageWidth||atlas.imageHeight!=base.imageHeight||
            (atlas.anchorCount&&atlas.anchorCount!=base.frameCount))return false;
    for(unsigned frame=0;frame<atlas.anchorCount;++frame) {
        const auto anchor=atlas.anchors[frame];const auto& rect=base.frames[frame].rect;
        if(!(anchor.x>=0&&anchor.x<=rect.width&&anchor.y>=0&&anchor.y<=rect.height))return false;
    }
    return true;
}
template<std::size_t N>
constexpr bool ValidSet(const std::array<Atlas,N>& atlases) {
    for(std::size_t i=0;i<N;++i) {
        if(!Valid(atlases[i]))return false;
        for(std::size_t j=0;j<i;++j)
            if(atlases[i].resourceId==atlases[j].resourceId||
               (atlases[i].character==atlases[j].character&&
                atlases[i].baseResourceId==atlases[j].baseResourceId))return false;
    }
    return true;
}
template<std::size_t N>
constexpr const Atlas* Find(unsigned character,int baseResourceId,
                            const std::array<Atlas,N>& atlases) {
    for(const auto& atlas:atlases)
        if(atlas.character==character&&atlas.baseResourceId==baseResourceId&&Valid(atlas))return &atlas;
    return nullptr;
}
} // namespace cat::styled_assets
