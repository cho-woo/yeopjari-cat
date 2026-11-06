#pragma once

#include "cat_sprite_sequence.hpp"
#include <array>
#include <cstddef>

namespace cat::sprite_assets {
constexpr unsigned kMaxFrames=24;
constexpr unsigned kCharacters=10;
struct Rect { int x=0,y=0,width=0,height=0; };
// Anchors are local to the frame rectangle, in unscaled source pixels.
// Every clip has one common referenceHeight; frames are never normalized by
// their individual cropped heights. The caller applies DPI and real-age scale.
struct Anchor { double x=0,y=0; };
struct Frame { Rect rect;Anchor anchor; };
struct Descriptor {
    cat::sprite_sequence::Clip clip=cat::sprite_sequence::Clip::Groom;
    unsigned character=0;
    int resourceId=0;
    unsigned columns=0,rows=0,frameCount=0,imageWidth=0,imageHeight=0;
    double referenceHeight=1;
    std::array<Frame,kMaxFrames> frames{};
    const char* sha256="";
    const char* sourcePath="";
};
constexpr int ResourceId(cat::sprite_sequence::Clip clip,unsigned character) {
    return static_cast<unsigned>(clip)<8&&character<kCharacters?
        113+static_cast<int>(character*8+static_cast<unsigned>(clip)):0;
}
constexpr bool Valid(const Descriptor& asset) {
    const auto grid=cat::sprite_sequence::GridFor(asset.clip,asset.frameCount);
    if(asset.character>=kCharacters||asset.resourceId!=ResourceId(asset.clip,asset.character)||
       grid.count==0||!cat::sprite_sequence::GridAllowed(asset.clip,asset.columns,asset.rows)||asset.frameCount!=grid.count||
       asset.frameCount>kMaxFrames||asset.imageWidth==0||asset.imageHeight==0||
       asset.imageWidth>16384||asset.imageHeight>16384||
       !(asset.referenceHeight>0&&asset.referenceHeight<=16384))return false;
    for(unsigned index=0;index<asset.frameCount;++index) {
        const auto& f=asset.frames[index];
        if(f.rect.x<0||f.rect.y<0||f.rect.width<=0||f.rect.height<=0||
           f.rect.x>static_cast<int>(asset.imageWidth)||f.rect.y>static_cast<int>(asset.imageHeight)||
           f.rect.width>static_cast<int>(asset.imageWidth)-f.rect.x||
           f.rect.height>static_cast<int>(asset.imageHeight)-f.rect.y||
           !(f.anchor.x>=0&&f.anchor.x<=f.rect.width&&f.anchor.y>=0&&f.anchor.y<=f.rect.height))return false;
    }
    return true;
}
template<std::size_t N>
constexpr const Descriptor* Find(cat::sprite_sequence::Clip clip,unsigned character,
                                const std::array<Descriptor,N>& assets) {
    for(const auto& asset:assets)
        if(asset.clip==clip&&asset.character==character&&Valid(asset))return &asset;
    return nullptr;
}
} // namespace cat::sprite_assets
