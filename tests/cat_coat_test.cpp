#include "../src/cat_coat.hpp"
#include "../src/cat_preferences.hpp"
#include "../src/cat_species.hpp"
#include "../src/care_sprites.generated.hpp"
#include <cassert>
#include <iostream>

int main() {
    using namespace cat;using namespace cat::sprite_sequence;
    static_assert(kAvailableCharacters.size()==3);
    assert(AvailableCharacter(Character::Tuxedo)==Character::Tuxedo);
    assert(AvailableCharacter(Character::OddEye)==Character::OddEye);
    assert(!species::UsesNewArt(Character::OddEye));
    assert(!CharacterAvailable(Character::SilverTabby));
    assert(!species::UsesNewArt(Character::Tuxedo));
    assert(species::ProfileFor(Character::Cream).movementSpeedMultiplier==species::ProfileFor(Character::Tuxedo).movementSpeedMultiplier);
    for(auto clip:kAuthoredClips) {
        const auto* cream=sprite_assets::Find(clip,0,sprite_assets::kCareSprites);
        const auto* tuxedo=sprite_assets::Find(clip,8,sprite_assets::kCareSprites);
        assert(cream&&cream==tuxedo&&cream==sprite_assets::Find(clip,10,sprite_assets::kCareSprites)); // Same sequence, event times, anchors and stride.
        assert(!sprite_assets::Find(clip,1,sprite_assets::kCareSprites));
    }
    for(unsigned a=0;a<=255;++a)for(unsigned c=0;c<=a;c+=7) {
        const auto p=(a<<24)|(a<<16)|(c<<8)|c;
        const auto result=coat::Pixel(p,.4,.3,Clip::Groom,0);
        assert((result>>24)==a);
        assert(((result>>16)&255)<=a&&((result>>8)&255)<=a&&(result&255)<=a);
    }
    for(auto clip:{Clip::Box,Clip::Eat,Clip::Treat,Clip::Fish})for(unsigned f=0;f<24;++f)
        for(int y=0;y<100;++y)for(int x=0;x<100;++x) {
            if(coat::Prop(clip,f,x/100.,y/100.))
                assert(coat::Pixel(0xffbc8e62,x/100.,y/100.,clip,f)==0xffbc8e62);
        }
    assert(coat::Pixel(0xfff6bb78,.4,.3,Clip::Groom,0)!=0xfff6bb78);
    // A single anatomical coat must survive the source painter's warm stripes.
    const auto red=[](std::uint32_t p){return (p>>16)&255;};
    for(auto fur:{0xfff6bb78u,0xfff6e5d2u}) {
        assert(red(coat::Pixel(fur,.40,.60,Clip::Walk,0))<85); // Solid back.
        assert(red(coat::Pixel(fur,.68,.73,Clip::Walk,0))>220); // White chest.
        assert(red(coat::Pixel(fur,.60,.87,Clip::Groom,0))>220); // White paw.
        assert(red(coat::Pixel(fur,.25,.61,Clip::Groom,0))<85); // Whole tail.
        const auto paw=coat::Pixel(fur,.60,.87,Clip::Groom,0);
        assert(((paw>>16)&255)==((paw>>8)&255)&&((paw>>8)&255)==(paw&255));
    }
    // Actual facial features survive; reflected warm fur cannot make pink holes.
    const auto h=coat::Head(Clip::Punch,0);
    assert(coat::Pixel(0xff68310f,h.cx-.12,h.cy+.12,Clip::Punch,0)==0xff68310f);
    assert(coat::Pixel(0xffef9c9a,h.cx,h.cy+.22,Clip::Punch,0)==0xffef9c9a);
    const auto ear=coat::Pixel(0xfff6d2bf,h.cx-.20,h.cy-.10,Clip::Punch,0);
    assert(red(ear)<85&&std::abs(int((ear>>16)&255)-int(ear&255))<4);
    // The turn/tilt and curled fallback use their own source-registered regions.
    assert(coat::Head(Clip::Groom,0).cx-coat::Head(Clip::Groom,11).cx>.12);
    assert(coat::Head(Clip::Sleep,4).angle<-.4);
    assert(coat::Head(Clip::None,6).cy>coat::Head(Clip::None,4).cy+.15);
    std::cout<<"PASS: shared programs, anatomical coat, white paws, facial features, alpha and props\n";
}
