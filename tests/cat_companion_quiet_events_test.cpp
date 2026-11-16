#include "../src/cat_companion.hpp"
#include <cstdlib>
#include <iostream>
namespace cat::companion {
struct LiveMotionAuditAccess {
    static std::uint64_t OverdueFly(Model& m,std::uint64_t ticks){
        for(unsigned i=0;i<12;++i)m.physics_.Update(1./60);
        m.landedAt_=0;m.BeginQuiet(ticks);m.nextFly_=1;return m.nextDecision_;
    }
    static void LongSleep(Model& m,std::uint64_t ticks) {
        for(unsigned i=0;i<12;++i)m.physics_.Update(1./60);
        m.landedAt_=0;m.ResetControls();m.brain_->Interrupt(ticks);
        m.storyActive_=true;m.quietActive_=false;
        m.SetMotion(cat::animation::Motion::Sleep,ticks,600000,0);
    }
    static std::uint64_t Decision(const Model& m){return m.nextDecision_;}
};
}
void Check(bool value,const char* message){if(!value){std::cerr<<message<<'\n';std::exit(1);}}
int main(){
    using namespace cat::companion;using C=cat::sprite_sequence::Clip;
    constexpr std::int64_t now=1800000000;unsigned checked=0;
    for(auto coat:cat::kAvailableCharacters){
        auto state=NewState(now-366*86400);state.enabled=true;state.preferences.character=coat;
        state.pet.updatedUtc=state.nutrition.updatedUtc=state.memory.updatedUtc=state.life.updatedUtc=now;
        Model model;model.Bind(state);model.Reset(96,904,600);
        const auto until=LiveMotionAuditAccess::OverdueFly(model,1000);
        Context c;c.nowUtc=now;c.elapsedSeconds=.016;c.primaryVisible=false;
        for(c.ticks=1016;c.ticks<until;c.ticks+=16){
            model.Tick(c);Check(model.View(c.ticks).animation.spriteClip!=C::FlyHunt,"automatic fly hunt stole an unfinished neutral rest");
        }
        bool hunted=false;
        for(unsigned i=0;i<100&&!hunted;++i,c.ticks+=16){model.Tick(c);hunted=model.View(c.ticks).animation.spriteClip==C::FlyHunt;}
        Check(hunted,"overdue fly event was lost instead of waiting for the safe boundary");
        // Manual furniture remains responsive inside a quiet interval.
        auto second=state;second.pet.roamingPaused=true;Model manual;manual.Bind(second);manual.Reset(96,904,600);
        LiveMotionAuditAccess::OverdueFly(manual,1000);Check(manual.RequestFurniture(cat::furniture::Kind::Box),"manual furniture rejected");
        c.supportsFurniture=c.furnitureReady=true;
        for(c.ticks=1016;c.ticks<2400&&!manual.furniture.Owned();c.ticks+=16)manual.Tick(c);
        Check(manual.furniture.Owned(),"manual command was incorrectly delayed until the random-event rest deadline");++checked;
        for(auto kind:{cat::furniture::Kind::Tower,cat::furniture::Kind::Scratcher,cat::furniture::Kind::Box}) {
            auto sleepingState=state;sleepingState.pet.roamingPaused=true;
            Model sleeping;sleeping.Bind(sleepingState);sleeping.Reset(96,904,600);
            LiveMotionAuditAccess::LongSleep(sleeping,1000);
            Check(sleeping.RequestFurniture(kind),"sleeping furniture command rejected");
            const auto* sleep=cat::sprite_assets::Find(C::Sleep,static_cast<unsigned>(coat),cat::sprite_assets::kCareSprites);
            Check(sleep!=nullptr,"registered sleep source missing");
            const auto intro=cat::animation::ReferenceSleepIntroMs(&sleep->program,sleep->frameCount);
            bool woke=false;std::uint64_t wakeUntil=0;
            for(c.ticks=1016;c.ticks<16000&&!sleeping.furniture.Owned();c.ticks+=16) {
                sleeping.Tick(c);const auto view=sleeping.View(c.ticks);
                if(view.motion==cat::animation::Motion::Stretch&&!woke) {
                    woke=true;wakeUntil=LiveMotionAuditAccess::Decision(sleeping);
                    Check(c.ticks>=1000+intro,"furniture cut the authored falling-asleep entrance");
                    Check(view.animation.spriteClip==C::Sleep,"furniture skipped the authored waking source");
                }
                if(sleeping.furniture.Owned())Check(woke&&c.ticks>=wakeUntil,"furniture began before wake recovery finished");
            }
            Check(sleeping.furniture.Owned(),"manual furniture starved behind sleep instead of waking naturally");
        }
    }
    std::cout<<"quiet events: "<<checked<<" coats preserve rest;9sleep-to-furniture commands retain sleep entrance/wake recovery and start passed\n";
}
