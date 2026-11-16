#include "../src/cat_companion.hpp"
#include <cstdlib>
#include <iostream>
void Check(bool value,const char* message){if(!value){std::cerr<<message<<'\n';std::exit(1);}}
int main(){
    using namespace cat::companion;using C=cat::sprite_sequence::Clip;
    constexpr std::int64_t now=1800000000;unsigned checked=0;
    for(auto coat:cat::kAvailableCharacters){
        auto state=NewState(now-366*86400);state.enabled=true;state.preferences.character=coat;
        state.pet.updatedUtc=state.nutrition.updatedUtc=state.memory.updatedUtc=state.life.updatedUtc=now;
        state.pet.position=.76;state.nutrition.bodyCondition=50;
        Model model;model.Bind(state);model.Reset(96,904,340);
        Context c;c.nowUtc=now;c.primaryVisible=true;c.primary.x=260;c.primary.grounded=true;c.elapsedSeconds=.016;
        for(c.ticks=16;c.ticks<1000;c.ticks+=16)model.Tick(c);
        Check(model.TrySocial(Interaction::Paw,c),"distant play command rejected");
        unsigned contacts=0;bool punched=false,finished=false;double prior=model.Physical().x;
        for(;c.ticks<90000;c.ticks+=16){
            model.Tick(c);contacts+=model.TakeEvent()==Interaction::Paw;
            const auto view=model.View(c.ticks);punched|=view.animation.spriteClip==C::Punch;
            Check(std::abs(view.physical.x-prior)<3,"social approach reset the physical root");prior=view.physical.x;
            if(!model.SocialActive()){finished=true;break;}
        }
        Check(finished&&punched&&contacts==2&&state.mutualPlay==1,"distant play ended before both complete paw bouts");++checked;
    }
    std::cout<<"social approach: "<<checked<<" coats reach their friend and complete both bouts passed\n";
}
