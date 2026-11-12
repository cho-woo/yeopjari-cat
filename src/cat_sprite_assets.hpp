#pragma once

#include "cat_sprite_sequence.hpp"
#include <array>
#include <cstddef>

namespace cat::sprite_assets {
constexpr unsigned kMaxFrames=cat::sprite_sequence::kMaxAuthoredFrames;
constexpr unsigned kCharacters=11;
// Tuxedo is a coat variant of the same authored actor, not a second animation
// registration. Stable persisted ID 8 deliberately shares Cream's programs,
// anchors, stride lengths, timings and event cursor.
constexpr unsigned AuthoredCharacter(unsigned character) { return (character==8||character==10)?0:character; }
constexpr unsigned kMaxSide=16384;
constexpr std::uint64_t kMaxImagePixels=32ull*1024*1024;
constexpr std::uint64_t kMaxResidentPixels=64ull*1024*1024;
constexpr std::uint64_t kMaxImageBytes=32ull*1024*1024;
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
    cat::sprite_sequence::Program program{};
    double stridePixels=0;
    unsigned standingFrame=0; // Planted source drawing; same scale/anchor as the gait.
    double nominalFramesPerSecond=0; // Source cadence hint; physical distance still selects poses.
    unsigned movementDelayMs=0; // Stationary part of an authored locomotion entrance.
};
constexpr double NominalLocomotionSpeed(const Descriptor& asset) {
    return (asset.clip==cat::sprite_sequence::Clip::Walk||asset.clip==cat::sprite_sequence::Clip::Run) &&
        asset.nominalFramesPerSecond>0 && asset.program.loop.count>0 && asset.stridePixels>0?
        asset.stridePixels*asset.nominalFramesPerSecond/asset.program.loop.count:0;
}
constexpr int ResourceId(cat::sprite_sequence::Clip clip,unsigned character) {
    if(character>=kCharacters)return 0;
    if(static_cast<unsigned>(clip)<8)return 113+static_cast<int>(character*8+static_cast<unsigned>(clip));
    // 301..314 remain reserved for retired audio and are checked absent by
    // the silent-build verification. Locomotion must never reuse those IDs.
    if(clip==cat::sprite_sequence::Clip::Walk)return 400+static_cast<int>(character*2);
    if(clip==cat::sprite_sequence::Clip::Run)return 401+static_cast<int>(character*2);
    if(clip==cat::sprite_sequence::Clip::Knead)return 440+static_cast<int>(character*2);
    if(clip==cat::sprite_sequence::Clip::Reach)return 441+static_cast<int>(character*2);
    if(clip>=cat::sprite_sequence::Clip::BallPlay&&clip<=cat::sprite_sequence::Clip::RunningTurn)
        return 430+static_cast<int>(character*32+static_cast<unsigned>(clip)-static_cast<unsigned>(cat::sprite_sequence::Clip::BallPlay));
    return 0;
}
constexpr bool Valid(const Descriptor& asset) {
    using namespace cat::sprite_sequence;
    const auto grid=cat::sprite_sequence::GridFor(asset.clip,asset.frameCount);
    const bool aliasedRun=asset.clip==Clip::Run && asset.resourceId==ResourceId(Clip::Walk,asset.character);
    if(asset.character>=kCharacters||!KnownClip(asset.clip)||
       (asset.resourceId!=ResourceId(asset.clip,asset.character)&&!aliasedRun)||
       asset.frameCount==0||asset.standingFrame>=asset.frameCount||asset.frameCount>kMaxFrames||asset.imageWidth==0||asset.imageHeight==0||
       asset.imageWidth>kMaxSide||asset.imageHeight>kMaxSide||
       static_cast<std::uint64_t>(asset.imageWidth)*asset.imageHeight>kMaxImagePixels||
       !(asset.referenceHeight>0&&asset.referenceHeight<=kMaxSide)||
       !(asset.stridePixels>=0&&asset.stridePixels<=kMaxSide)||
       !(asset.nominalFramesPerSecond>=0&&asset.nominalFramesPerSecond<=240))return false;
    if(asset.program.stepCount) {
        if(!GridAllowed(asset.clip,asset.columns,asset.rows,asset.frameCount)||
           !ValidProgram(asset.program,asset.frameCount))return false;
    } else if(grid.count==0||!GridAllowed(asset.clip,asset.columns,asset.rows)||asset.frameCount!=grid.count||
              asset.clip==Clip::Walk||asset.clip==Clip::Run)return false;
    if((asset.clip==Clip::Walk||asset.clip==Clip::Run)&&asset.stridePixels<=0)return false;
    if(asset.movementDelayMs && (!asset.program.stepCount||
            asset.movementDelayMs>Duration(asset.program,asset.program.intro)))return false;
    std::uint64_t residentPixels=0;
    for(unsigned index=0;index<asset.frameCount;++index) {
        const auto& f=asset.frames[index];
        if(f.rect.x<0||f.rect.y<0||f.rect.width<=0||f.rect.height<=0||
           f.rect.x>static_cast<int>(asset.imageWidth)||f.rect.y>static_cast<int>(asset.imageHeight)||
           f.rect.width>static_cast<int>(asset.imageWidth)-f.rect.x||
           f.rect.height>static_cast<int>(asset.imageHeight)-f.rect.y||
           !(f.anchor.x>=0&&f.anchor.x<=f.rect.width&&f.anchor.y>=0&&f.anchor.y<=f.rect.height))return false;
        residentPixels+=static_cast<std::uint64_t>(f.rect.width)*f.rect.height;
        if(residentPixels>kMaxResidentPixels)return false;
    }
    return true;
}
template<std::size_t N>
constexpr const Descriptor* Find(cat::sprite_sequence::Clip clip,unsigned character,
                                const std::array<Descriptor,N>& assets) {
    character=AuthoredCharacter(character);
    for(const auto& asset:assets)
        if(asset.clip==clip&&asset.character==character&&Valid(asset))return &asset;
    return nullptr;
}
} // namespace cat::sprite_assets
