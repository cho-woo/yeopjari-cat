#include "../src/cat_companion.hpp"
#include <cstdlib>
#include <iostream>
namespace cat::companion {struct LiveMotionAuditAccess {
    static void Dump(const Model& m,std::uint64_t ticks){std::cerr<<"ticks="<<ticks<<" start="<<m.motionStart_<<" end="<<m.nextDecision_<<
        " clip="<<unsigned(m.actionClip_)<<" stop="<<m.waitingForStop_<<" travel="<<m.explicitTravel_<<" wall="<<m.wallApproaching_<<" motion="<<unsigned(m.motion_)<<'\n';}
};}

void Check(bool value,const char* message){if(!value){std::cerr<<message<<'\n';std::exit(1);}}
int main(){
    using namespace cat::companion;using C=cat::sprite_sequence::Clip;
    unsigned cases=0;
    for(auto coat:cat::kAvailableCharacters)for(bool paused:{false,true}){
        constexpr std::int64_t now=1800000000;
        auto state=NewState(now-366*86400);state.enabled=true;state.preferences.character=coat;
        state.pet.roamingPaused=paused;state.pet.position=.8;
        state.pet.updatedUtc=state.nutrition.updatedUtc=state.memory.updatedUtc=state.life.updatedUtc=now;
        Model model;model.Bind(state);model.Reset(96,904,600);
        Check(model.Perform(C::Climb,1000),"climb request rejected");
        std::uint64_t began=0;bool airborne=false,returned=false;double peak=0;
        const auto* art=cat::sprite_assets::Find(C::Climb,static_cast<unsigned>(coat),cat::sprite_assets::kCareSprites);
        Check(art!=nullptr,"missing climb registration");
        const auto duration=cat::sprite_sequence::ProgramDuration(art->program);
        for(unsigned frame=0;frame<3000;++frame){
            Context c;c.nowUtc=now;c.ticks=1000+(frame+1)*1000ULL/60;c.elapsedSeconds=1./60;c.primaryVisible=false;
            model.Tick(c);const auto view=model.View(c.ticks);peak=std::max(peak,view.physical.lift);
            if(!began&&view.animation.spriteClip==C::Climb)began=c.ticks;
            airborne=airborne||!view.physical.grounded;
            if(began&&c.ticks>=began+duration+1000){
                if(view.animation.spriteClip==C::Climb)LiveMotionAuditAccess::Dump(model,c.ticks);
                Check(view.physical.grounded,"climb did not land");
                Check(view.animation.spriteClip!=C::Climb,"terminal raised-paw climb frame stranded in quiet interval");
                Check(!model.Busy(),"walking owner retained the completed wall action");returned=true;break;
            }
        }
        Check(began&&airborne&&returned&&peak>80,"incomplete real climb/drop/recovery");++cases;
    }
    std::cout<<"companion climb: "<<cases<<" coats/pause cases complete source, land and release owner passed\n";
}
