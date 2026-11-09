#include "../src/cat_coat.hpp"
#include "../src/cat_preferences.hpp"
#include "../src/cat_species.hpp"
#include "../src/care_sprites.generated.hpp"
#include <cassert>
#include <iostream>

int main() {
    using namespace cat;using namespace cat::sprite_sequence;
    static_assert(kAvailableCharacters.size()==2);
    assert(AvailableCharacter(Character::Tuxedo)==Character::Tuxedo);
    assert(!CharacterAvailable(Character::SilverTabby));
    assert(!species::UsesNewArt(Character::Tuxedo));
    assert(species::ProfileFor(Character::Cream).movementSpeedMultiplier==species::ProfileFor(Character::Tuxedo).movementSpeedMultiplier);
    for(auto clip:kAuthoredClips) {
        const auto* cream=sprite_assets::Find(clip,0,sprite_assets::kCareSprites);
        const auto* tuxedo=sprite_assets::Find(clip,8,sprite_assets::kCareSprites);
        assert(cream&&cream==tuxedo); // Same sequence, event times, anchors and stride.
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
    std::cout<<"PASS: two-character selection, shared programs, coat alpha and prop protection\n";
}
