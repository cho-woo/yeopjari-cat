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
bool SameFrame(const Frame& a,const Frame& b) {
    return a.frame==b.frame && a.localMs==b.localMs && a.holdMs==b.holdMs &&
        a.looping==b.looping && a.holding==b.holding && a.finished==b.finished && a.groundContact==b.groundContact;
}
void AuthoredSheetGridsPreserveTheLegacyLayout() {
    // Actual uploaded sheets: groom4x4, sleep4x2, box/fish4x2,
    // punch4x3, kibble3x4, jump4x4 and churu6x4.
    constexpr std::array<unsigned,8> sourceCounts{{16,8,8,8,12,12,16,24}};
    static_assert(GridFor(Clip::Sleep).count==12 && GridFor(Clip::Sleep,8).count==8);
    static_assert(GridAllowed(Clip::Sleep,4,2) && GridAllowed(Clip::Sleep,4,3));
    static_assert(!GridAllowed(Clip::Sleep,2,4) && !GridAllowed(Clip::Sleep,3,4));
    static_assert(static_cast<unsigned>(Clip::Groom)==0 && static_cast<unsigned>(Clip::Sleep)==1 &&
        static_cast<unsigned>(Clip::Box)==2 && static_cast<unsigned>(Clip::Fish)==3 &&
        static_cast<unsigned>(Clip::Punch)==4 && static_cast<unsigned>(Clip::Eat)==5 &&
        static_cast<unsigned>(Clip::Jump)==6 && static_cast<unsigned>(Clip::Treat)==7);
    for(unsigned index=0;index<kClips.size();++index) {
        const auto clip=kClips[index];const auto grid=GridFor(clip);
        Options source;source.sheetFrames=sourceCounts[index];
        const auto actual=GridFor(clip,source.sheetFrames);
        CHECK(actual.count==sourceCounts[index] && actual.count==actual.columns*actual.rows);
        for(unsigned columns=0;columns<=8;++columns)for(unsigned rows=0;rows<=6;++rows) {
            const bool expected=(columns==grid.columns && rows==grid.rows) ||
                (clip==Clip::Sleep && columns==4 && rows==2);
            CHECK(GridAllowed(clip,columns,rows)==expected);
        }
        for(std::uint64_t elapsed=0;elapsed<20000;elapsed+=31)
            CHECK(Sample(clip,elapsed,source).frame<actual.count);
        CHECK(Sample(clip,std::numeric_limits<std::uint64_t>::max(),source).frame<actual.count);
    }
    CHECK(!GridAllowed(Clip::None,0,0));
    CHECK(!GridAllowed(static_cast<Clip>(100),4,2));
    CHECK(!GridAllowed(Clip::Sleep,4,std::numeric_limits<unsigned>::max()));
    for(unsigned count:{1u,7u,9u,16u,std::numeric_limits<unsigned>::max()}) {
        Options invalid;invalid.sheetFrames=count;
        CHECK(GridFor(Clip::Sleep,count).count==0);
        CHECK(SampleSleep(0,invalid).finished && Sample(Clip::Sleep,10000,invalid).finished);
    }
    // Supplying12 explicitly must not reinterpret the previous sheet's
    // entrance, six resting frames or four wake frames as the new story.
    for(auto stage:{SleepStage::Entering,SleepStage::Resting,SleepStage::Waking}) {
        Options legacy;legacy.sleep=stage;auto explicitTwelve=legacy;explicitTwelve.sheetFrames=12;
        for(std::uint64_t elapsed=0;elapsed<20000;elapsed+=37)
            CHECK(SameFrame(Sample(Clip::Sleep,elapsed,legacy),SampleSleep(elapsed,explicitTwelve)));
    }
}
void EightFrameSleepUsesStoryPosesWithoutWakingInItsRestLoop() {
    Options story;story.sheetFrames=8;
    for(const auto& boundary:{std::pair<std::uint64_t,unsigned>{0,0},{249,0},{250,1},{899,1},
            {900,2},{1399,2},{1400,3},{6399,3},{6400,4}}) {
        const auto pose=SampleSleep(boundary.first,story);
        CHECK(pose.frame==boundary.second && pose.groundContact && !pose.finished);
        CHECK(pose.looping==(boundary.first>=kSleepStoryIntroMs));
    }
    story.sleep=SleepStage::Resting;
    CHECK(SampleSleep(0,story).frame==3);
    CHECK(SampleSleep(5000,story).frame==4);
    CHECK(SampleSleep(23000,story).frame==5);
    CHECK(SampleSleep(23800,story).frame==4);
    CHECK(SampleSleep(39800,story).frame==7);
    CHECK(SampleSleep(40400,story).frame==4);
    CHECK(SampleSleep(54400,story).frame==6);
    CHECK(SampleSleep(55300,story).frame==3);
    CHECK(SampleSleep(59300,story).frame==4);
    std::array<unsigned,8> restingSamples{};
    for(std::uint64_t elapsed=0;elapsed<kSleepStoryRestMs;elapsed+=25) {
        const auto pose=SampleSleep(elapsed,story);++restingSamples[pose.frame];
        CHECK(pose.frame>=3 && pose.frame<=7 && pose.looping && !pose.finished);
        CHECK(pose.groundContact && pose.localMs<pose.holdMs);
        CHECK(SameFrame(pose,SampleSleep(elapsed+kSleepStoryRestMs,story)));
    }
    CHECK(restingSamples[0]==0 && restingSamples[1]==0 && restingSamples[2]==0);
    CHECK(restingSamples[3]==360 && restingSamples[4]==1948);
    CHECK(restingSamples[5]==32 && restingSamples[6]==36 && restingSamples[7]==24);
    CHECK((restingSamples[3]+restingSamples[4])*100>2400*96);
    story.sleep=SleepStage::Entering;
    CHECK(SameFrame(SampleSleep(1400,story),SampleSleep(1400+kSleepStoryRestMs,story)));
    const auto huge=SampleSleep(std::numeric_limits<std::uint64_t>::max(),story);
    CHECK(huge.frame>=3 && huge.frame<=7 && huge.looping && !huge.finished);
}
void EightFrameSleepWakePauseAndInterruptionRemainBounded() {
    Options story;story.sheetFrames=8;
    for(std::uint64_t interrupt:{0ULL,249ULL,250ULL,899ULL,900ULL,1399ULL,1400ULL,3600000ULL}) {
        Player player;player.Start(Clip::Sleep,story);player.Advance(interrupt);
        const auto before=player.Current();player.Pause(true);
        player.Advance(86400000);CHECK(player.Elapsed()==interrupt && SameFrame(before,player.Current()));
        player.Wake();CHECK(player.Current().frame==2 && player.Elapsed()==0 && player.Paused());
        CHECK(!player.Advance(86400000).impact && player.Elapsed()==0 && player.Current().frame==2);
        player.Pause(false);player.Advance(219);CHECK(player.Current().frame==2);
        player.Advance(1);CHECK(player.Current().frame==1);
        player.Wake();CHECK(player.Elapsed()==220 && player.Current().frame==1);
        player.Advance(329);CHECK(player.Current().frame==1);
        player.Advance(1);CHECK(player.Current().frame==0 && !player.Current().finished);
        player.Pause(true);player.Advance(86400000);CHECK(player.Elapsed()==550 && player.Current().frame==0);
        player.Pause(false);player.Advance(330);
        CHECK(player.Elapsed()==kSleepWakeMs && player.Current().frame==0 && player.Current().holding && player.Current().finished);
        player.Advance(std::numeric_limits<std::uint64_t>::max());
        CHECK(player.Current().frame==0 && player.Current().finished && !player.Current().looping);
        player.Start(Clip::Groom);const auto grooming=player.Current();player.Wake();
        CHECK(player.Elapsed()==0 && SameFrame(grooming,player.Current()));
        player.Advance(280);CHECK(player.Current().frame==2 && player.Current().looping);
    }
}
void SleepVariantSelectionPreservesLegacyAndKeepsAlternatesRare() {
    std::array<unsigned,6> legacy{},authored{};
    for(unsigned sample=0;sample<6000;++sample) {
        const double roll=(sample+.5)/6000;
        ++legacy[SelectSleepVariant(roll,false)];++authored[SelectSleepVariant(roll,true)];
        CHECK(SelectSleepVariant(roll,false)==std::min(5u,static_cast<unsigned>(roll*6)));
    }
    for(unsigned variant=0;variant<6;++variant) {
        CHECK(legacy[variant]==1000);
        CHECK(authored[variant]==(variant==0?4500u:300u));
    }
    for(bool useAuthored:{false,true}) {
        CHECK(SelectSleepVariant(-100,useAuthored)==0);
        CHECK(SelectSleepVariant(0,useAuthored)==0);
        CHECK(SelectSleepVariant(1,useAuthored)==5);
        CHECK(SelectSleepVariant(100,useAuthored)==5);
        for(double invalid:{std::numeric_limits<double>::quiet_NaN(),
                std::numeric_limits<double>::infinity(),-std::numeric_limits<double>::infinity()})
            CHECK(SelectSleepVariant(invalid,useAuthored)==0);
        for(int sample=-100;sample<=200;++sample)
            CHECK(SelectSleepVariant(sample/100.,useAuthored)<=5);
    }
    for(const auto& boundary:{std::pair<double,unsigned>{.75,1},{.80,2},{.85,3},{.90,4},{.95,5}}) {
        CHECK(SelectSleepVariant(std::nextafter(boundary.first,0.0),true)==boundary.second-1);
        CHECK(SelectSleepVariant(boundary.first,true)==boundary.second);
    }
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
    for(const auto& sample:{std::pair<double,unsigned>{200,5},{100,6},{40,7},{15,8},{0,9},{-20,10},{-70,10},{-200,11}}) {
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
    CHECK(Sample(Clip::Jump,100000,input).frame==9);
}
void JumpFlightNeverReusesThePlantedPrelaunchFrame() {
    Options physical;physical.grounded=false;
    for(const auto& boundary:{std::pair<double,unsigned>{141,5},{140,6},{71,6},{70,7},
            {26,7},{25,8},{9,8},{8,9},{0,9},{-8,9},{-8.001,10},{-100,10},{-100.001,11}}) {
        physical.velocityY=boundary.first;
        for(auto phase:{JumpPhase::Crouch,JumpPhase::Airborne,JumpPhase::Landing}) {
            physical.jump=phase;
            CHECK(Sample(Clip::Jump,0,physical).frame==boundary.second);
            const auto pose=Sample(Clip::Jump,std::numeric_limits<std::uint64_t>::max(),physical);
            CHECK(pose.frame==boundary.second && pose.frame>=5 && pose.frame<=11 && !pose.groundContact);
        }
    }
    physical.grounded=true;
    for(auto phase:{JumpPhase::Crouch,JumpPhase::Airborne,JumpPhase::Landing})
    for(std::uint64_t elapsed:std::array<std::uint64_t,4>{{0,220,100000,std::numeric_limits<std::uint64_t>::max()}})
    for(std::uint64_t contact:{0ULL,69ULL,70ULL,210ULL,280ULL,100000ULL}) {
        physical.jump=phase;physical.contactElapsedMs=contact;
        const auto pose=Sample(Clip::Jump,elapsed,physical);
        CHECK(pose.groundContact && (pose.frame<=3 || (pose.frame>=12 && pose.frame<=15)));
    }
    physical.grounded=false;physical.velocityY=200;physical.jump=JumpPhase::Airborne;
    Player player;player.Start(Clip::Jump,physical);player.Advance(90);player.Pause(true);
    const auto held=player.Current();CHECK(held.frame==5);
    CHECK(!player.Advance(100000).impact && player.Elapsed()==90 && SameFrame(held,player.Current()));
    player.Pause(false);player.Advance(100000);CHECK(player.Current().frame==5);
    physical.velocityY=-200;player.SetOptions(physical);CHECK(player.Current().frame==11);
    // Missing a ledge keeps the falling drawing; only an actual contact can
    // start landing, regardless of how long the animation clock has run.
    physical.jump=JumpPhase::Landing;player.SetOptions(physical);CHECK(player.Current().frame==11);
    physical.grounded=true;physical.contactElapsedMs=0;player.SetOptions(physical);
    CHECK(player.Current().frame==12 && player.Current().groundContact && !player.Current().finished);
    physical.contactElapsedMs=280;player.SetOptions(physical);
    CHECK(player.Current().frame==15 && player.Current().groundContact && player.Current().finished);
}
void GroundedVisitsHoldAndGroomBounds() {
    CHECK(GroomDuration(0)==6000&&GroomDuration(1)==4000&&GroomDuration(100000)==8000);
    CHECK(Sample(Clip::Groom,5999).looping);
    CHECK(Sample(Clip::Groom,6000).finished);
    CHECK(Sample(Clip::Box,0).frame==2);
    for(std::uint64_t elapsed=0;elapsed<4000;elapsed+=7)CHECK(Sample(Clip::Box,elapsed).frame>=2);
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
int main(){try{SheetMetadataAndEveryFrame();SleepDoesNotRestartYawnOrWake();AuthoredSheetGridsPreserveTheLegacyLayout();EightFrameSleepUsesStoryPosesWithoutWakingInItsRestLoop();EightFrameSleepWakePauseAndInterruptionRemainBounded();SleepVariantSelectionPreservesLegacyAndKeepsAlternatesRare();PunchCuesDoNotRepeatOrReplay();JumpTracksPhysicalState();JumpFlightNeverReusesThePlantedPrelaunchFrame();GroundedVisitsHoldAndGroomBounds();MealsHaveOneEntranceAndOneEnding();
std::cout<<"Sprite sequences: "<<checks<<" checks passed\n";return EXIT_SUCCESS;}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return EXIT_FAILURE;}}
