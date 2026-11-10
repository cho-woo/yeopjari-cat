#pragma once
#include "native_posture_tests.hpp"
#include "native_walking_tests.hpp"
#include "native_recline_tests.hpp"
#include "native_care_sprite_tests.hpp"
// Runs only under --render-self-test, after loading resources. No save paths,
// audio device, updater or user interaction is involved.
int NativeExpandedRenderTest(std::string& context) {
    // Preserve the original canonical-pixel assertions for the available character;
    // real authored sheets have their own complete native regression below.
    DisabledCareSpriteScope canonicalArtwork;
    Surface canvas;
    if (!canvas.Create(420,320)) return 120;
    const auto originalPreferences = cat::SerializePreferences(g.preferences);
    const auto originalNutrition = cat::nutrition::Serialize(g.nutrition);
    const auto body = cat::appearance::VisualModifiers(g.nutrition,g.preferences.character);
    auto draw = [&](cat::Character character, cat::Outfit outfit, const cat::animation::Pose& pose) {
        Graphics graphics(canvas.image.get());
        graphics.SetCompositingMode(CompositingModeSourceCopy); graphics.Clear(Color(0,0,0,0));
        graphics.SetCompositingMode(CompositingModeSourceOver);
        graphics.SetSmoothingMode(SmoothingModeAntiAlias);
        graphics.SetInterpolationMode(InterpolationModeHighQualityBicubic);
        graphics.SetPixelOffsetMode(PixelOffsetModeHighQuality);
        DrawPetStyle(graphics,character,outfit,pose,body,210,280,110);
        graphics.Flush(FlushIntentionSync);
        return graphics.GetLastStatus()==Ok;
    };
    auto copy = [&] { return std::vector<BYTE>(canvas.pixels,canvas.pixels+420*320*4); };
    std::array<std::uint64_t,cat::kCharacterCount> identities{};
    for (int c=0;c<1;++c) {
        const auto character=cat::kAvailableCharacters[c];
        for (int index=0;index<=static_cast<int>(Motion::Pride);++index) {
            const auto motion=static_cast<Motion>(index);
            const auto pose=cat::animation::Evaluate(motion,1600,500000,1);
            if(!draw(character,cat::Outfit::None,pose)) return 121;
            const auto baseline=copy();
            std::size_t bodyPixels=0;
            for(std::size_t p=3;p<baseline.size();p+=4) if(baseline[p]>=200) ++bodyPixels;
            if(bodyPixels<500) {context="small species body c="+std::to_string(c)+" motion="+std::to_string(static_cast<int>(motion));return 122;}
            if(motion==Motion::Idle) {
                identities[c]=cat::Checksum(std::string(reinterpret_cast<const char*>(baseline.data()),baseline.size()));
                for(int prior=0;prior<c;++prior) if(identities[prior]==identities[c]) {context="species shares identical rendered art";return 123;}
            }
            for(int o=0;o<cat::kOutfitCount;++o) {
                context="expanded c="+std::to_string(c)+" outfit="+std::to_string(o)+" motion="+std::to_string(static_cast<int>(motion));
                if(!draw(character,static_cast<cat::Outfit>(o),pose)) return 124;
                // Retired wardrobe keys still round-trip, but never add overlays.
                if(copy()!=baseline)return 124;
                std::size_t covered=0;
                for(int y=0;y<320;++y) for(int x=0;x<420;++x) {
                    const auto p=static_cast<std::size_t>(y*420+x)*4;
                    const auto* px=canvas.pixels+p;
                    if(px[0]>px[3]+1 || px[1]>px[3]+1 || px[2]>px[3]+1) return 125;
                    if(baseline[p+3]>=200 && px[3]>=190) ++covered;
                    if((x==0||x==419||y==0||y==319) && px[3]>=32) return 126;
                }
                if(covered*100<bodyPixels*95) return 127;
            }
        }
        for(unsigned variant=0;variant<6;++variant) for(bool perched:{false,true}) {
            context="sleep c="+std::to_string(c)+" variant="+std::to_string(variant)+" perched="+std::to_string(perched);
            const auto pose=cat::animation::EvaluateSleep(variant,1600,500000,1,perched);
            if(!draw(character,cat::Outfit::Scarf,pose))return 133;
            std::size_t visible=0;
            for(std::size_t p=3;p<420*320*4;p+=4)if(canvas.pixels[p]>=32)++visible;
            if(visible<700)return 134;
        }
        // Render the real targeted-paw mesh through GDI+, separately from
        // expression changes. Reaching must move a paw without moving the head
        // or replacing the body, for either facing and every character.
        const auto geometry=CursorAttackGeometry(character);
        for(int direction:{-1,1}) {
            cat::animation::Pose resting;resting.flip=direction<0;
            if(!draw(character,cat::Outfit::None,resting))return 135;
            const auto planted=copy();
            for(const auto kind:{cat::cursor::AttackKind::Gentle,cat::cursor::AttackKind::Strong}) {
                const auto peak=kind==cat::cursor::AttackKind::Gentle?270U:320U;
                for(const auto elapsed:std::array<std::uint64_t,3>{150,peak,cat::cursor::DurationMs(kind)}) {
                    auto reaching=cat::cursor::AttackPose(kind,elapsed,direction,
                        direction*((geometry.pawAnchorX-.5)*geometry.width+5),20,geometry);
                    reaching.face={}; // Compare anatomy, not intentional eye/brow reactions.
                    if(!draw(character,cat::Outfit::None,reaching))return 135;
                    std::size_t changedPaw=0;
                    const double headBottom=280-geometry.height*.40;
                    context="cursor paw c="+std::to_string(c)+" direction="+std::to_string(direction)+
                        " kind="+std::to_string(static_cast<int>(kind))+" elapsed="+std::to_string(elapsed);
                    for(int y=0;y<320;++y)for(int x=0;x<420;++x) {
                        const auto p=static_cast<std::size_t>(y*420+x)*4;
                        const bool changed=std::memcmp(planted.data()+p,canvas.pixels+p,4)!=0;
                        if(y<headBottom && changed)return 136;
                        if(y>=headBottom && changed)++changedPaw;
                        if((x==0||x==419||y==0||y==319) && canvas.pixels[p+3]>=32)return 137;
                    }
                    if(elapsed<cat::cursor::DurationMs(kind) ? changedPaw<8 : changedPaw!=0)return 138;
                    for(int o=0;o<cat::kOutfitCount;++o)
                        if(!draw(character,static_cast<cat::Outfit>(o),reaching))return 139;
                    if(!draw(character,cat::Outfit::None,resting) || copy()!=planted)return 140;
                }
            }
        }
        // Use precisely the same head/body drawing and joints: expression may
        // change facial pixels but cannot scale or bob the planted body.
        cat::animation::Pose neutral;neutral.frame=4;neutral.joints={};
        if(!draw(character,cat::Outfit::None,neutral)) return 128;
        const auto baseline=copy();auto expressive=neutral;
        expressive.face={.8,.1,.7,.7,.5};
        if(!draw(character,cat::Outfit::None,expressive)) return 128;
        std::size_t changed=0,bodyCompared=0;
        for(int y=0;y<320;++y)for(int x=0;x<420;++x) {
            const auto p=static_cast<std::size_t>(y*420+x)*4;
            if(std::memcmp(baseline.data()+p,canvas.pixels+p,4)!=0)++changed;
            if(y>=250 && baseline[p+3]>=200) {
                ++bodyCompared;
                if(std::memcmp(baseline.data()+p,canvas.pixels+p,4)!=0) {context="expression changes lower body c="+std::to_string(c);return 129;}
            }
        }
        if(changed<5||bodyCompared<40) {context="missing local expression c="+std::to_string(c)+" changes="+std::to_string(changed)+" body="+std::to_string(bodyCompared);return 130;}
    }
    if(cat::SerializePreferences(g.preferences)!=originalPreferences || cat::nutrition::Serialize(g.nutrition)!=originalNutrition) {
        context="explicit companion renderer mutated primary preferences/nutrition";return 131;
    }
    if(const int walking=NativeWalkingRenderTest(context))return walking;
    if(const int posture=NativePostureRenderTest(context))return posture;
    if(const int recline=NativeReclineRenderTest(context))return recline;
    context="actual independent companion window rendering and hit testing";
    std::string windowError;
    if(!cat::companion::NativeWindowSmokeTest(g.instance,PaintCompanion,windowError)) {context+=" "+windowError;return 132;}
    context="expanded species/outfit/expression and companion native checks passed";
    return 0;
}
