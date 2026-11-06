#include "../src/cat_sprite_sequence.hpp"
#include <cstdlib>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>

namespace {
int checks=0;
#define CHECK(condition) do{++checks;if(!(condition))throw std::runtime_error(std::string(__FILE__)+":"+std::to_string(__LINE__)+" " #condition);}while(false)
using namespace cat::sprite_sequence;

void SheetMetadataAndEveryFrame() {
    constexpr unsigned expected[]={16,12,8,8,12,12,16,24};
    for(unsigned index=0;index<kClips.size();++index) {
        const auto clip=kClips[index];
        const auto grid=GridFor(clip);
        CHECK(grid.count==expected[index]);CHECK(grid.count==grid.columns*grid.rows);
        for(std::uint64_t elapsed=0;elapsed<20000;elapsed+=17) {
            const auto pose=Sample(clip,elapsed);
            CHECK(pose.frame<grid.count);
            CHECK(pose.groundContact);
            CHECK(pose.holdMs==0 || pose.localMs<=pose.holdMs);
        }
        CHECK(Sample(clip,std::numeric_limits<std::uint64_t>::max()).frame<grid.count);
    }
    CHECK(GridFor(static_cast<Clip>(100)).count==0);
    CHECK(Sample(static_cast<Clip>(100),100).finished);
    CHECK(static_cast<unsigned>(Clip::None)==8);
    CHECK(GridFor(Clip::None).count==0);
    CHECK(Sample(Clip::None,0).finished);
}
void SleepDoesNotRestartYawnOrWake() {
    CHECK(Sample(Clip::Sleep,0).frame==0);
    CHECK(Sample(Clip::Sleep,499).frame==0);
    CHECK(Sample(Clip::Sleep,500).frame==1);
    CHECK(Sample(Clip::Sleep,1199).frame==1);
    for(std::uint64_t elapsed=1200;elapsed<86400000;elapsed+=997) {
        const auto pose=Sample(Clip::Sleep,elapsed);
        CHECK(pose.frame>=2&&pose.frame<=7);
        CHECK(pose.looping&&!pose.finished);
    }
    Player player;player.Start(Clip::Sleep);player.Advance(3600000);
    CHECK(player.Current().frame>=2&&player.Current().frame<=7);
    player.Wake();CHECK(player.Current().frame==8);
    player.Advance(220);CHECK(player.Current().frame==9);
    player.Wake();CHECK(player.Current().frame==9); // no repeated wake restart
    player.Advance(660);CHECK(player.Current().frame==11&&player.Current().finished);
    Options rest;rest.sleep=SleepStage::Resting;
    CHECK(Sample(Clip::Sleep,0,rest).frame==2);
}
void PunchCuesDoNotRepeatOrReplay() {
    CHECK(Sample(Clip::Punch,299).frame==3);
    CHECK(Sample(Clip::Punch,300).frame==kPunchImpactFrame);
    CHECK(Sample(Clip::Punch,599).frame==3);
    CHECK(Sample(Clip::Punch,600).frame==kPunchSecondImpactFrame);
    for(std::uint64_t step:{1ULL,17ULL,40ULL,120ULL,10000ULL}) {
        Player player;player.Start(Clip::Punch);unsigned hits=0;
        for(unsigned sample=0;sample<1500;++sample) {
            const auto tick=player.Advance(step);
            hits+=tick.impactCount;
            CHECK(tick.impact==(tick.impactCount!=0));
            CHECK(tick.pose.frame==Sample(Clip::Punch,player.Elapsed()).frame);
            if(player.Elapsed()>10000)break;
        }
        CHECK(hits==2);
        CHECK(!player.Advance(0).impact);
        CHECK(!player.Advance(std::numeric_limits<std::uint64_t>::max()).impact);
    }
    Player paused;paused.Start(Clip::Punch);paused.Advance(290);paused.Pause(true);
    CHECK(!paused.Advance(100000).impact&&paused.Elapsed()==290);
    paused.Pause(false);CHECK(paused.Advance(10).impact);
    paused.Pause(true);CHECK(!paused.Advance(100000).impact&&paused.Elapsed()==300);
    paused.Pause(false);CHECK(!paused.Advance(10).impact);
    CHECK(paused.Advance(290).impactCount==1);
    CHECK(!paused.Advance(10).impact);
    paused.Start(Clip::Punch);const auto skipped=paused.Advance(100000);
    CHECK(skipped.impactCount==2&&skipped.pose.finished&&skipped.pose.frame==11);
    CHECK(!paused.Advance(1).impact&&paused.Current().finished);
}
void JumpTracksPhysicalState() {
    Options input;input.grounded=false;input.jump=JumpPhase::Airborne;
    for(const auto& sample:{std::pair<double,unsigned>{200,4},{100,5},{40,6},{15,7},{0,8},{-20,9},{-70,10},{-200,11}}) {
        input.velocityY=sample.first;
        const auto early=Sample(Clip::Jump,0,input),late=Sample(Clip::Jump,100000,input);
        CHECK(early.frame==sample.second&&late.frame==sample.second);
        CHECK(!early.groundContact&&!late.finished);
    }
    input.grounded=true;input.jump=JumpPhase::Landing;
    for(unsigned frame=0;frame<4;++frame) {
        input.contactElapsedMs=frame*70;
        const auto pose=Sample(Clip::Jump,100000,input);
        CHECK(pose.frame==12+frame&&pose.groundContact);
    }
    input.contactElapsedMs=280;CHECK(Sample(Clip::Jump,0,input).finished);
    input.jump=JumpPhase::Crouch;
    CHECK(Sample(Clip::Jump,100000,input).frame==3);
    CHECK(!Sample(Clip::Jump,100000,input).finished);
    input.grounded=false;input.jump=JumpPhase::Landing;input.velocityY=-200;
    CHECK(Sample(Clip::Jump,100000,input).frame==11); // missed ledge keeps falling
    input.velocityY=std::numeric_limits<double>::quiet_NaN();
    CHECK(Sample(Clip::Jump,100000,input).frame==8);
}
void GroundedVisitsHoldAndGroomBounds() {
    CHECK(GroomDuration(0)==6000&&GroomDuration(1)==4000&&GroomDuration(100000)==8000);
    CHECK(Sample(Clip::Groom,5999).looping);
    CHECK(Sample(Clip::Groom,6000).finished);
    CHECK(Sample(Clip::Box,0).frame==0);
    CHECK(Sample(Clip::Box,100000).frame==7&&Sample(Clip::Box,100000).holding);
    CHECK(Sample(Clip::Fish,100000).frame==7&&Sample(Clip::Fish,100000).finished);
}
void MealsHaveOneEntranceAndOneEnding() {
    CHECK(Sample(Clip::Eat,0).frame==0);
    CHECK(Sample(Clip::Eat,200).frame==1);
    CHECK(Sample(Clip::Eat,400).frame==2);
    CHECK(Sample(Clip::Eat,4200).frame==9);
    CHECK(Sample(Clip::Eat,4460).frame==10);
    CHECK(Sample(Clip::Eat,4720).frame==11);
    CHECK(Sample(Clip::Treat,0).frame==0);
    CHECK(Sample(Clip::Treat,450).frame==5);
    CHECK(Sample(Clip::Treat,540).frame==6);
    CHECK(Sample(Clip::Treat,3400).frame==15);
    CHECK(Sample(Clip::Treat,4120).frame==23);
    for(const auto clip:{Clip::Eat,Clip::Treat}) {
        const unsigned firstLoop=clip==Clip::Eat?2:6,lastLoop=clip==Clip::Eat?8:14;
        const unsigned firstOutro=lastLoop+1,last=GridFor(clip).count-1;
        const std::uint64_t introMs=clip==Clip::Eat?400:540;
        for(const std::uint64_t requested:{0ULL,1ULL,2500ULL,3300ULL,4200ULL,5000ULL,12000ULL,100000ULL}) {
            Options options;options.durationMs=requested;
            const auto duration=CareDuration(clip,requested),outroAt=duration-800;
            bool seenOutro=false;
            for(std::uint64_t elapsed=0;elapsed<duration+1000;elapsed+=7) {
                const auto pose=Sample(clip,elapsed,options);
                CHECK(pose.frame<=last);
                if(elapsed<introMs)CHECK(pose.frame<firstLoop&&!pose.looping&&!pose.finished);
                else if(elapsed<outroAt)CHECK(pose.frame>=firstLoop&&pose.frame<=lastLoop&&pose.looping);
                else {seenOutro=true;CHECK(pose.frame>=firstOutro&&!pose.looping);}
                if(seenOutro)CHECK(pose.frame>=firstOutro); // no emptied bowl -> chewing reset
                CHECK(pose.finished==(elapsed>=duration));
            }
            CHECK(seenOutro);
            CHECK(Sample(clip,duration,options).frame==last&&Sample(clip,duration,options).holding);
        }
        Player paused;paused.Start(clip);paused.Advance(introMs+1000);paused.Pause(true);
        const auto before=paused.Current();
        paused.Advance(100000);
        CHECK(paused.Elapsed()==introMs+1000&&paused.Current().frame==before.frame);
        paused.Pause(false);paused.Advance(CareDuration(clip,0)-800-paused.Elapsed());
        CHECK(paused.Current().frame==firstOutro);
        paused.Pause(true);paused.Advance(100000);CHECK(paused.Current().frame==firstOutro);
        paused.Pause(false);paused.Advance(800);CHECK(paused.Current().frame==last&&paused.Current().finished);
        paused.Advance(100000);CHECK(paused.Current().frame==last&&paused.Current().finished);
    }
    CHECK(CareDuration(Clip::Eat,0)==5000&&CareDuration(Clip::Treat,0)==4200);
    CHECK(CareDuration(Clip::Eat,1)==2500&&CareDuration(Clip::Treat,std::numeric_limits<std::uint64_t>::max())==12000);
}
}
int main(){try{SheetMetadataAndEveryFrame();SleepDoesNotRestartYawnOrWake();PunchCuesDoNotRepeatOrReplay();JumpTracksPhysicalState();GroundedVisitsHoldAndGroomBounds();MealsHaveOneEntranceAndOneEnding();
std::cout<<"Sprite sequences: "<<checks<<" checks passed\n";return EXIT_SUCCESS;}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return EXIT_FAILURE;}}
