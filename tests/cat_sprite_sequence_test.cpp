#include "../src/cat_sprite_sequence.hpp"
#include "../src/cat_sprite_assets.hpp"
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
    player.Advance(kSleepWakeMs/4);CHECK(player.Current().frame==9);
    player.Wake();CHECK(player.Current().frame==9); // no repeated wake restart
    player.Advance(kSleepWakeMs*3/4);CHECK(player.Current().frame==11&&player.Current().finished);
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
    for(const auto& boundary:{std::pair<std::uint64_t,unsigned>{0,0},{899,0},{900,1},{2199,1},
            {2200,2},{3799,2},{3800,3},{15799,3},{15800,4}}) {
        const auto pose=SampleSleep(boundary.first,story);
        CHECK(pose.frame==boundary.second && pose.groundContact && !pose.finished);
        CHECK(pose.looping==(boundary.first>=kSleepStoryIntroMs));
    }
    story.sleep=SleepStage::Resting;
    CHECK(SampleSleep(0,story).frame==3);
    CHECK(SampleSleep(12000,story).frame==4);
    CHECK(SampleSleep(54000,story).frame==5);
    CHECK(SampleSleep(56000,story).frame==4);
    CHECK(SampleSleep(106000,story).frame==3);
    std::array<unsigned,8> restingSamples{};
    for(std::uint64_t elapsed=0;elapsed<kSleepStoryRestMs;elapsed+=25) {
        const auto pose=SampleSleep(elapsed,story);++restingSamples[pose.frame];
        // All normal sleeping poses are curled. The full stretch/seated
        // twitch must not interrupt sleep, however long the event lasts.
        CHECK(pose.frame>=3 && pose.frame<=5 && pose.looping && !pose.finished);
        CHECK(pose.groundContact && pose.localMs<pose.holdMs);
        CHECK(SameFrame(pose,SampleSleep(elapsed+kSleepStoryRestMs,story)));
    }
    CHECK(restingSamples[0]==0 && restingSamples[1]==0 && restingSamples[2]==0);
    CHECK(restingSamples[3]==1040 && restingSamples[4]==3680 && restingSamples[5]==80);
    CHECK(restingSamples[6]==0 && restingSamples[7]==0);
    CHECK((restingSamples[3]+restingSamples[4])*100>4800*98);
    story.sleep=SleepStage::Entering;
    CHECK(SameFrame(SampleSleep(kSleepStoryIntroMs,story),SampleSleep(kSleepStoryIntroMs+kSleepStoryRestMs,story)));
    const auto huge=SampleSleep(std::numeric_limits<std::uint64_t>::max(),story);
    CHECK(huge.frame>=3 && huge.frame<=5 && huge.looping && !huge.finished);
}
void EightFrameSleepWakePauseAndInterruptionRemainBounded() {
    Options story;story.sheetFrames=8;
    for(std::uint64_t interrupt:{0ULL,899ULL,900ULL,2199ULL,2200ULL,3799ULL,3800ULL,3600000ULL}) {
        Player player;player.Start(Clip::Sleep,story);player.Advance(interrupt);
        const auto before=player.Current();player.Pause(true);
        player.Advance(86400000);CHECK(player.Elapsed()==interrupt && SameFrame(before,player.Current()));
        player.Wake();CHECK(player.Current().frame==6 && player.Elapsed()==0 && player.Paused());
        CHECK(!player.Advance(86400000).impact && player.Elapsed()==0 && player.Current().frame==6);
        player.Pause(false);player.Advance(699);CHECK(player.Current().frame==6);
        player.Advance(1);CHECK(player.Current().frame==7);
        player.Wake();CHECK(player.Elapsed()==700 && player.Current().frame==7);
        player.Advance(650);CHECK(player.Current().frame==2);
        player.Advance(650);CHECK(player.Current().frame==1);
        player.Advance(600);CHECK(player.Current().frame==0 && !player.Current().finished);
        player.Pause(true);player.Advance(86400000);CHECK(player.Elapsed()==2600 && player.Current().frame==0);
        player.Pause(false);player.Advance(400);
        CHECK(player.Elapsed()==kSleepWakeMs && player.Current().frame==0 && player.Current().holding && player.Current().finished);
        player.Advance(std::numeric_limits<std::uint64_t>::max());
        CHECK(player.Current().frame==0 && player.Current().finished && !player.Current().looping);
        player.Start(Clip::Groom);const auto grooming=player.Current();player.Wake();
        CHECK(player.Elapsed()==0 && SameFrame(grooming,player.Current()));
        player.Advance(600);CHECK(player.Current().frame==1 && !player.Current().looping);
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
    CHECK(GroomDuration(0)==kGroomDefaultMs&&GroomDuration(1)==kGroomMinMs&&GroomDuration(100000)==kGroomMaxMs);
    CHECK(Sample(Clip::Groom,5999).frame==0&&!Sample(Clip::Groom,5999).looping);
    CHECK(Sample(Clip::Groom,6000).finished);
    for(unsigned variant=0;variant<6;++variant)
    for(std::uint64_t duration:{4000ULL,6000ULL,8000ULL}) {
        Options options;options.durationMs=duration;options.variant=variant;
        bool acted=false,settled=false;unsigned changes=0,last=0;
        for(std::uint64_t elapsed=0;elapsed<duration;elapsed+=7) {
            const auto pose=Sample(Clip::Groom,elapsed,options);
            CHECK(!pose.looping&&!pose.finished&&pose.holdMs>=300);
            if(pose.frame!=last){++changes;last=pose.frame;}
            if(pose.frame==0) {if(acted)settled=true;}
            else {
                CHECK(!settled);acted=true;
                CHECK(variant%3==0?(pose.frame>=1&&pose.frame<=8):
                    variant%3==1?(pose.frame>=9&&pose.frame<=12):(pose.frame>=13&&pose.frame<=15));
            }
        }
        CHECK(acted&&settled&&changes<=10);
        const auto done=Sample(Clip::Groom,duration,options);
        CHECK(done.frame==0&&done.finished&&done.holding&&!done.looping);
        CHECK(Sample(Clip::Groom,std::numeric_limits<std::uint64_t>::max(),options).frame==0);
    }
    for(const auto& boundary:{std::pair<std::uint64_t,unsigned>{0,2},{1199,2},{1200,3},{2199,3},
            {2200,4},{4799,4},{4800,5},{5899,5},{5900,6},{7299,6},{7300,7},{8500,7}})
        CHECK(Sample(Clip::Box,boundary.first).frame==boundary.second);
    CHECK(!Sample(Clip::Box,kBoxDurationMs-1).finished);
    CHECK(Sample(Clip::Box,kBoxDurationMs).frame==7&&Sample(Clip::Box,kBoxDurationMs).finished);
    CHECK(Sample(Clip::Box,100000).holding);
    for(std::uint64_t elapsed=0;elapsed<kBoxDurationMs;elapsed+=7)CHECK(Sample(Clip::Box,elapsed).frame>=2);
    for(const auto clip:{Clip::Box,Clip::Fish,Clip::Groom}) {
        Player player;player.Start(clip);player.Advance(1400);const auto before=player.Current();
        player.Pause(true);player.Advance(100000);CHECK(player.Elapsed()==1400&&SameFrame(before,player.Current()));
        player.Pause(false);player.Advance(100000);CHECK(player.Current().finished&&!player.Current().looping);
    }
    unsigned previous=0;
    for(std::uint64_t elapsed=0;elapsed<kFishDurationMs;elapsed+=7) {
        const auto fish=Sample(Clip::Fish,elapsed);
        CHECK(fish.frame>=previous&&fish.holdMs>=400&&!fish.looping&&!fish.finished);
        previous=fish.frame;
    }
    CHECK(Sample(Clip::Fish,4999).frame==7&&!Sample(Clip::Fish,4999).finished);
    CHECK(Sample(Clip::Fish,kFishDurationMs).frame==7&&Sample(Clip::Fish,kFishDurationMs).finished);
}
void MealsHaveOneEntranceAndOneEnding() {
    CHECK(Sample(Clip::Eat,0).frame==0);
    CHECK(Sample(Clip::Eat,600).frame==4);
    CHECK(Sample(Clip::Eat,1000).frame==1);
    CHECK(Sample(Clip::Eat,1350).frame==2);
    CHECK(Sample(Clip::Eat,3600).frame==8);
    CHECK(Sample(Clip::Eat,3900).frame==9);
    CHECK(Sample(Clip::Eat,4150).frame==10);
    CHECK(Sample(Clip::Eat,4450).frame==11);
    CHECK(Sample(Clip::Treat,0).frame==0);
    CHECK(Sample(Clip::Treat,400).frame==3);
    CHECK(Sample(Clip::Treat,600).frame==4);
    CHECK(Sample(Clip::Treat,850).frame==5);
    CHECK(Sample(Clip::Treat,1100).frame==6);
    CHECK(Sample(Clip::Treat,2800).frame==15);
    CHECK(Sample(Clip::Treat,3300).frame==20);
    CHECK(Sample(Clip::Treat,3600).frame==23);
    for(const auto clip:{Clip::Eat,Clip::Treat}) {
        const unsigned firstOutro=clip==Clip::Eat?8:15,last=GridFor(clip).count-1;
        const std::uint64_t introMs=clip==Clip::Eat?1000:1100;
        for(const std::uint64_t requested:{0ULL,1ULL,2500ULL,3300ULL,3500ULL,4200ULL,5000ULL,12000ULL,100000ULL}) {
            Options options;options.durationMs=requested;
            const auto duration=CareDuration(clip,requested),outroAt=duration-kCareOutroMs;
            bool seenOutro=false;
            for(std::uint64_t elapsed=0;elapsed<duration+1000;elapsed+=7) {
                const auto pose=Sample(clip,elapsed,options);
                CHECK(pose.frame<=last);
                if(elapsed<introMs) {
                    CHECK(!pose.looping&&!pose.finished);
                    CHECK(clip==Clip::Eat?(pose.frame==0||pose.frame==4):
                        (pose.frame==0||pose.frame==3||pose.frame==4||pose.frame==5));
                } else if(elapsed<outroAt) {
                    // A meal never cycles through upright or emptied-bowl
                    // pictures while eating, and the tube never re-enters.
                    CHECK(pose.looping&&!pose.finished&&pose.holdMs>=220);
                    CHECK(clip==Clip::Eat?(pose.frame==1||pose.frame==2):(pose.frame>=6&&pose.frame<=8));
                } else {
                    seenOutro=true;CHECK(!pose.looping);
                    CHECK(clip==Clip::Eat?(pose.frame>=8&&pose.frame<=11):
                        (pose.frame==15||pose.frame==16||pose.frame==20||pose.frame==23));
                }
                if(seenOutro)CHECK(pose.frame>=firstOutro);
                CHECK(pose.finished==(elapsed>=duration));
            }
            CHECK(seenOutro);
            CHECK(Sample(clip,duration,options).frame==last&&Sample(clip,duration,options).holding);
        }
        Player paused;paused.Start(clip);paused.Advance(introMs+1000);paused.Pause(true);
        const auto before=paused.Current();
        paused.Advance(100000);
        CHECK(paused.Elapsed()==introMs+1000&&SameFrame(before,paused.Current()));
        paused.Pause(false);paused.Advance(CareDuration(clip,0)-kCareOutroMs-paused.Elapsed());
        CHECK(paused.Current().frame==firstOutro);
        paused.Pause(true);paused.Advance(100000);CHECK(paused.Current().frame==firstOutro);
        paused.Pause(false);paused.Advance(kCareOutroMs);CHECK(paused.Current().frame==last&&paused.Current().finished);
        paused.Advance(100000);CHECK(paused.Current().frame==last&&paused.Current().finished);
    }
    CHECK(CareDuration(Clip::Eat,0)==5000&&CareDuration(Clip::Treat,0)==4200&&CareDuration(Clip::Fish,0)==5000);
    CHECK(CareDuration(Clip::Eat,1)==kCareMinDurationMs&&CareDuration(Clip::Treat,std::numeric_limits<std::uint64_t>::max())==kCareMaxDurationMs);
}

Program LinearProgram(unsigned count,unsigned exposure=17) {
    Program result;result.stepCount=count;result.intro={0,count};result.loop={count,0};result.outro={count,0};
    for(unsigned i=0;i<count;++i)result.steps[i]={i,exposure+i%3};
    return result;
}
void ExpandedProgramsVisitEveryDistinctDrawing() {
    for(const auto count:{8u,12u,16u,24u,64u,128u,256u}) {
        auto program=LinearProgram(count);
        CHECK(ValidProgram(program,count));
        Options options;options.sheetFrames=count;
        std::uint64_t clock=0;
        ProgramPlayer player;player.Start(Clip::Groom,program,options);
        for(unsigned i=0;i<count;++i) {
            CHECK(SampleProgram(Clip::Groom,program,clock,options).frame==i);
            CHECK(player.Current().frame==i&&!player.Current().finished);
            const auto exposure=program.steps[i].durationMs;
            CHECK(SampleProgram(Clip::Groom,program,clock+exposure-1,options).frame==i);
            player.Advance(exposure);clock+=exposure;
        }
        CHECK(clock==ProgramDuration(program));
        CHECK(player.Current().frame==count-1&&player.Current().finished&&player.Current().holding);
        CHECK(SampleProgram(Clip::Groom,program,std::numeric_limits<std::uint64_t>::max(),options).frame==count-1);
        player.Advance(std::numeric_limits<std::uint64_t>::max());
        CHECK(player.Elapsed()==std::numeric_limits<std::uint64_t>::max()&&player.Current().finished);
        CHECK(player.Advance(1).pose.frame==count-1);
        program.finish=Finish::Hold;
        const auto held=SampleProgram(Clip::Groom,program,clock,options);
        CHECK(held.holding&&!held.finished&&held.frame==count-1);
    }
}
Program ThreeSectionProgram() {
    auto program=LinearProgram(6,100);
    for(auto& step:program.steps)step.durationMs=100;
    program.intro={0,2};program.loop={2,2};program.outro={4,2};program.durationMs=1000;
    return program;
}
void AuthoredSectionsPreserveExposureAndPersistentRest() {
    const auto program=ThreeSectionProgram();
    CHECK(ValidProgram(program,6));
    for(const auto& point:{std::pair<std::uint64_t,unsigned>{0,0},{99,0},{100,1},{199,1},
            {200,2},{300,3},{400,2},{700,3},{800,4},{900,5},{1000,5}}) {
        const auto pose=SampleProgram(Clip::Treat,program,point.first);
        CHECK(pose.frame==point.second);
        CHECK(pose.finished==(point.first>=1000));
    }
    Options shortened;shortened.durationMs=1;
    CHECK(ProgramDuration(program,shortened)==400);
    CHECK(SampleProgram(Clip::Treat,program,200,shortened).frame==4);
    CHECK(SampleProgram(Clip::Treat,program,399,shortened).frame==5);
    CHECK(SampleProgram(Clip::Treat,program,400,shortened).finished);
    auto noLoop=program;noLoop.intro={0,4};noLoop.loop={4,0};noLoop.outro={4,2};
    CHECK(SampleProgram(Clip::Eat,noLoop,700).frame==3);
    CHECK(SampleProgram(Clip::Eat,noLoop,700).holding);
    CHECK(SampleProgram(Clip::Eat,noLoop,800).frame==4);
    for(const auto clip:{Clip::Sleep,Clip::Box}) {
        ProgramPlayer player;player.Start(clip,program);
        player.Advance(86400000);
        CHECK(player.Current().looping&&!player.Current().finished);
        CHECK(player.Current().frame>=2&&player.Current().frame<=3);
        player.Exit();CHECK(player.Elapsed()==0&&player.Current().frame==4);
        player.Advance(100);CHECK(player.Current().frame==5);
        player.Exit();CHECK(player.Elapsed()==100); // Repeated exit does not restart.
        player.Advance(100);CHECK(player.Current().finished);
    }
    Options resting;resting.sleep=SleepStage::Resting;
    CHECK(SampleProgram(Clip::Sleep,program,0,resting).frame==2);
    resting.sleep=SleepStage::Waking;
    CHECK(SampleProgram(Clip::Sleep,program,0,resting).frame==4);
    Options entering;entering.stage=Stage::Entering;
    CHECK(SampleProgram(Clip::Groom,program,100000,entering).frame==1);
    CHECK(!SampleProgram(Clip::Groom,program,100000,entering).finished);
    auto held=program;held.finish=Finish::Hold;
    for(const auto clip:{Clip::Groom,Clip::Eat,Clip::Fish,Clip::Treat,Clip::Punch}) {
        CHECK(!HoldsFinalCarePose(clip,held,999));CHECK(HoldsFinalCarePose(clip,held,1000));
        CHECK(HoldsFinalCarePose(clip,held,100000));CHECK(!HoldsFinalCarePose(clip,program,100000));
        Options airborne;airborne.grounded=false;CHECK(!HoldsFinalCarePose(clip,held,100000,airborne));
    }
    for(const auto clip:{Clip::Sleep,Clip::Box,Clip::Jump,Clip::Walk,Clip::Run})
        CHECK(!HoldsFinalCarePose(clip,held,100000));
    noLoop.finish=Finish::Hold;
    CHECK(!HoldsFinalCarePose(Clip::Eat,noLoop,700)); // Mid-action wait is not the final hold.
    CHECK(HoldsFinalCarePose(Clip::Eat,noLoop,1000));
    Options extended;extended.durationMs=3000;
    CHECK(!HoldsFinalCarePose(Clip::Eat,noLoop,2000,extended));
    CHECK(HoldsFinalCarePose(Clip::Eat,noLoop,3000,extended));
}
void AuthoredCuesAreSimulationEventsOncePerAction() {
    auto program=ThreeSectionProgram();program.cueCount=4;
    program.cues[0]={0,0,Marker};program.cues[1]={1,50,Contact};
    program.cues[2]={2,25,Contact|Marker};program.cues[3]={4,50,Landing};
    ProgramPlayer player;player.Start(Clip::Punch,program);
    CHECK(player.Advance(0).eventCount==0);
    auto tick=player.Advance(149);CHECK(tick.eventCount==1&&tick.events==Marker&&tick.contactCount==0);
    CHECK(player.Current().frame==1);
    player.Pause(true);CHECK(player.Advance(10000).eventCount==0&&player.Elapsed()==149);
    player.Pause(false);tick=player.Advance(1);
    CHECK(tick.eventCount==1&&tick.events==Contact&&tick.contactCount==1);
    tick=player.Advance(10000);
    CHECK(tick.eventCount==2&&tick.contactCount==1&&(tick.events&Landing)!=0);
    CHECK(player.Advance(10000).eventCount==0);
    player.Start(Clip::Punch,program);tick=player.Advance(std::numeric_limits<std::uint64_t>::max());
    CHECK(tick.eventCount==4&&tick.contactCount==2&&tick.events==(Contact|Marker|Landing));
    CHECK(player.Advance(1).eventCount==0);
    player.Start(Clip::Box,program);tick=player.Advance(10000);
    CHECK(tick.eventCount==3&&(tick.events&Landing)==0); // Outro cannot occur during persistent rest.
    CHECK(player.Advance(10000).eventCount==0);
    player.Exit();CHECK(player.Advance(49).eventCount==0);
    tick=player.Advance(1);CHECK(tick.eventCount==1&&tick.events==Landing);
    CHECK(player.Advance(10000).eventCount==0);
    ProgramCueCursor cursor;
    CHECK(cursor.Consume(Clip::Punch,program,1000).eventCount==4);
    CHECK(cursor.Consume(Clip::Punch,program,0).eventCount==0);
    CHECK(cursor.Consume(Clip::Punch,program,1000).eventCount==0);
    cursor.Reset();CHECK(cursor.Consume(Clip::Punch,program,1000).eventCount==4);
    // The last cue bit must be a real64-bit shift, not undefined32-bit math.
    auto many=LinearProgram(64);many.cueCount=64;
    for(unsigned i=0;i<64;++i)many.cues[i]={i,0,Marker};
    player.Start(Clip::Groom,many);CHECK(player.Advance(100000).eventCount==64);
    CHECK(player.Advance(100000).eventCount==0);
    CHECK(!ProgramContactActive(Clip::Punch,program,149));
    CHECK(ProgramContactActive(Clip::Punch,program,150));
    CHECK(ProgramContactIndex(Clip::Punch,program,150)==1);
    CHECK(!ProgramContactActive(Clip::Punch,program,200));
    CHECK(ProgramContactIndex(Clip::Punch,program,225)==2);
    CHECK(!ProgramContactActive(Clip::Punch,program,100000));
    auto repeatedDrawing=program;
    repeatedDrawing.steps[3].frame=repeatedDrawing.steps[1].frame;
    CHECK(!ProgramContactActive(Clip::Punch,repeatedDrawing,350));
    repeatedDrawing.finish=Finish::Hold;
    repeatedDrawing.cueCount=1;repeatedDrawing.cues[0]={5,0,Contact};
    CHECK(ProgramContactActive(Clip::Punch,repeatedDrawing,900));
    CHECK(!ProgramContactActive(Clip::Punch,repeatedDrawing,100000));
}
void SameStepContactsUseTheLatestVisibleOffset() {
    auto program=LinearProgram(2,1000);program.finish=Finish::Hold;program.cueCount=4;
    // Deliberately unsorted. Equal offsets select the first registered cue,
    // while independent simulation-event consumption still sees both cues.
    program.cues[0]={0,800,Contact};program.cues[1]={0,200,Contact};
    program.cues[2]={0,800,Contact|Marker};program.cues[3]={1,100,Contact};
    CHECK(ValidProgram(program,2));
    for(const auto& point:{std::pair<std::uint64_t,unsigned>{0,0},{199,0},{200,2},{799,2},
            {800,1},{999,1},{1000,0},{1099,0},{1100,4},{2000,4},{2001,0},{100000,0}})
        CHECK(ProgramContactIndex(Clip::Punch,program,point.first)==point.second);
    ProgramPlayer player;player.Start(Clip::Punch,program);
    CHECK(player.Advance(850).contactCount==3);
    CHECK(ProgramContactIndex(Clip::Punch,program,player.Elapsed())==1);
    player.Pause(true);CHECK(player.Advance(10000).contactCount==0&&player.Elapsed()==850);
    CHECK(ProgramContactIndex(Clip::Punch,program,player.Elapsed())==1);
    player.Pause(false);CHECK(player.Advance(10000).contactCount==1);
    CHECK(!ProgramContactActive(Clip::Punch,program,player.Elapsed()));
    CHECK(player.Advance(10000).contactCount==0);
    // A skipped earlier pose does not remain eligible during the next step.
    ProgramCueCursor cursor;
    CHECK(cursor.Consume(Clip::Punch,program,1000).contactCount==3);
    CHECK(!ProgramContactActive(Clip::Punch,program,1000));
}
void ExpandedJumpAndLocomotionFollowPhysics() {
    auto jump=LinearProgram(128,100);
    jump.intro={0,16};jump.loop={16,96};jump.outro={112,16};
    jump.cueCount=2;jump.cues[0]={16,0,Takeoff};jump.cues[1]={112,0,Landing};
    Options physical;physical.sheetFrames=128;
    CHECK(SampleProgram(Clip::Jump,jump,100000,physical).frame==15);
    CHECK(!SampleProgram(Clip::Jump,jump,100000,physical).finished);
    ProgramCueCursor events;
    CHECK(events.Consume(Clip::Jump,jump,100000,physical).eventCount==0);
    physical.grounded=false;physical.jump=JumpPhase::Airborne;
    unsigned previous=16;
    for(int speed=400;speed>=-400;--speed) {
        physical.velocityY=speed;
        const auto pose=SampleProgram(Clip::Jump,jump,100000,physical);
        CHECK(pose.frame>=16&&pose.frame<112&&!pose.groundContact&&!pose.finished);
        CHECK(pose.frame>=previous);previous=pose.frame;
    }
    CHECK(events.Consume(Clip::Jump,jump,0,physical).events==Takeoff);
    CHECK(events.Consume(Clip::Jump,jump,0,physical).eventCount==0);
    physical.velocityY=std::numeric_limits<double>::quiet_NaN();
    CHECK(SampleProgram(Clip::Jump,jump,0,physical).frame<112);
    auto sixteen=LinearProgram(16,100);
    for(auto& step:sixteen.steps)step.durationMs=100;
    sixteen.intro={0,3};sixteen.loop={3,8};sixteen.outro={11,5};
    Options apex;apex.grounded=false;apex.jump=JumpPhase::Airborne;apex.velocityY=0;
    CHECK(SampleProgram(Clip::Jump,sixteen,0,apex).frame==7);
    physical.grounded=true;physical.jump=JumpPhase::Landing;physical.contactElapsedMs=0;
    CHECK(SampleProgram(Clip::Jump,jump,100000,physical).frame==112);
    CHECK(events.Consume(Clip::Jump,jump,100000,physical).events==Landing);
    physical.contactElapsedMs=100000;
    CHECK(SampleProgram(Clip::Jump,jump,0,physical).frame==127);
    CHECK(SampleProgram(Clip::Jump,jump,0,physical).finished);
    CHECK(events.Consume(Clip::Jump,jump,100000,physical).eventCount==0);
    for(const auto count:{8u,64u,128u,256u})for(const auto clip:{Clip::Walk,Clip::Run}) {
        auto gait=LinearProgram(count,100);
        for(unsigned i=0;i<count;++i)gait.steps[i].durationMs=100;
        gait.intro={0,0};gait.loop={0,count};gait.outro={count,0};
        Options distance;distance.sheetFrames=count;
        for(unsigned i=0;i<count;++i) {
            distance.cyclePhase=(static_cast<double>(i)+.25)/count;
            const auto pose=SampleProgram(clip,gait,0,distance);
            CHECK(pose.frame==i&&pose.looping&&pose.groundContact&&!pose.finished);
            CHECK(SameFrame(pose,SampleProgram(clip,gait,100000,distance))); // Blocked feet do not cycle.
        }
        distance.cyclePhase=1;CHECK(SampleProgram(clip,gait,0,distance).frame==0);
        distance.cyclePhase=std::numeric_limits<double>::infinity();
        CHECK(SampleProgram(clip,gait,0,distance).frame==0);
    }
}
void ProgramAndAtlasValidationRejectUnsafeInputs() {
    const auto good=ThreeSectionProgram();
    auto bad=good;bad.stepCount=kMaxProgramSteps+1;CHECK(!ValidProgram(bad));
    bad=good;bad.cueCount=kMaxProgramCues+1;CHECK(!ValidProgram(bad));
    bad=good;bad.steps[0].frame=256;CHECK(!ValidProgram(bad));
    bad=good;bad.steps[0].durationMs=0;CHECK(!ValidProgram(bad));
    bad=good;bad.steps[0].durationMs=kMaxStepMs+1;CHECK(!ValidProgram(bad));
    bad=good;bad.loop.first=1;CHECK(!ValidProgram(bad));
    bad=good;bad.outro.count=std::numeric_limits<unsigned>::max();CHECK(!ValidProgram(bad));
    bad=good;bad.durationMs=399;CHECK(!ValidProgram(bad));
    bad=good;bad.durationMs=kMaxProgramMs+1;CHECK(!ValidProgram(bad));
    bad=good;bad.cueCount=1;bad.cues[0]={0,100,Contact};CHECK(!ValidProgram(bad));
    bad.cues[0]={6,0,Contact};CHECK(!ValidProgram(bad));
    bad.cues[0]={0,0,16};CHECK(!ValidProgram(bad));
    CHECK(SampleProgram(Clip::Groom,bad,0).finished);
    CHECK(ValidProgram(good,6)&&!ValidProgram(good,5));
    CHECK(GridAllowed(Clip::Groom,8,8,64));CHECK(GridAllowed(Clip::Groom,16,8,128));
    CHECK(GridAllowed(Clip::Groom,16,16,256));CHECK(GridAllowed(Clip::Groom,4,4,8));
    CHECK(!GridAllowed(Clip::Groom,16,16,257));CHECK(!GridAllowed(Clip::Groom,17,16,256));
    CHECK(!GridAllowed(Clip::Groom,std::numeric_limits<unsigned>::max(),4,256));
    using cat::sprite_assets::Descriptor;using cat::sprite_assets::Valid;using cat::sprite_assets::ResourceId;
    Descriptor asset;asset.clip=Clip::Groom;asset.resourceId=ResourceId(asset.clip,0);
    asset.columns=16;asset.rows=16;asset.frameCount=256;asset.imageWidth=256;asset.imageHeight=256;
    asset.referenceHeight=12;asset.program=LinearProgram(256);
    for(unsigned i=0;i<256;++i)asset.frames[i]={{static_cast<int>(i%16*16),static_cast<int>(i/16*16),16,16},{8,16}};
    CHECK(Valid(asset));asset.program={};CHECK(!Valid(asset));
    asset.program=LinearProgram(256);asset.frames[255].rect.width=17;CHECK(!Valid(asset));
    asset.frames[255].rect.width=16;asset.imageWidth=16384;asset.imageHeight=16384;CHECK(!Valid(asset));
    asset.imageWidth=256;asset.imageHeight=256;
    asset.referenceHeight=std::numeric_limits<double>::quiet_NaN();CHECK(!Valid(asset));
    asset.referenceHeight=12;asset.clip=Clip::Walk;asset.resourceId=ResourceId(Clip::Walk,0);
    CHECK(!Valid(asset));asset.stridePixels=72;CHECK(Valid(asset));
    asset.clip=Clip::Run;CHECK(Valid(asset)); // Shared combined atlas uses Walk's resource.
    asset.resourceId=ResourceId(Clip::Run,0);CHECK(Valid(asset));
    CHECK(ResourceId(Clip::Box,0)==115&&ResourceId(Clip::Treat,9)==192);
    CHECK(ResourceId(Clip::Walk,0)==400&&ResourceId(Clip::Run,0)==401);
    for(unsigned character=0;character<cat::sprite_assets::kCharacters;++character) {
        const auto walkId=ResourceId(Clip::Walk,character),runId=ResourceId(Clip::Run,character);
        CHECK(walkId==400+static_cast<int>(2*character));CHECK(runId==walkId+1);
        CHECK(!(walkId>=301&&walkId<=314));CHECK(!(runId>=301&&runId<=314));
        CHECK(walkId!=193&&runId!=193); // Existing empty-box prop remains separate.
    }
    CHECK(ResourceId(Clip::None,0)==0&&ResourceId(Clip::Walk,11)==0);
}

void ToyPawUsesTheCommonActorAndThePhysicalContactWindow() {
    for(std::uint64_t elapsed=0;elapsed<4000;++elapsed) {
        const auto pose=SampleToyPaw(elapsed);
        CHECK(pose.frame<12 && pose.groundContact && !pose.looping);
        if(elapsed<kToyPawContactMs)CHECK(pose.frame<5);
        if(elapsed>=kToyPawContactMs && elapsed<kToyPawReleaseMs)CHECK(pose.frame==5);
        if(elapsed>=kToyPawReleaseMs)CHECK(pose.frame>=6);
    }
    CHECK(SampleToyPaw(1200).frame==11 && SampleToyPaw(1200).finished);
    CHECK(SampleToyPaw(std::numeric_limits<std::uint64_t>::max()).frame==11);
}

}
int main(){try{SheetMetadataAndEveryFrame();SleepDoesNotRestartYawnOrWake();AuthoredSheetGridsPreserveTheLegacyLayout();EightFrameSleepUsesStoryPosesWithoutWakingInItsRestLoop();EightFrameSleepWakePauseAndInterruptionRemainBounded();SleepVariantSelectionPreservesLegacyAndKeepsAlternatesRare();PunchCuesDoNotRepeatOrReplay();JumpTracksPhysicalState();JumpFlightNeverReusesThePlantedPrelaunchFrame();GroundedVisitsHoldAndGroomBounds();MealsHaveOneEntranceAndOneEnding();ExpandedProgramsVisitEveryDistinctDrawing();AuthoredSectionsPreserveExposureAndPersistentRest();AuthoredCuesAreSimulationEventsOncePerAction();SameStepContactsUseTheLatestVisibleOffset();ExpandedJumpAndLocomotionFollowPhysics();ProgramAndAtlasValidationRejectUnsafeInputs();ToyPawUsesTheCommonActorAndThePhysicalContactWindow();
std::cout<<"Sprite sequences: "<<checks<<" checks passed\n";return EXIT_SUCCESS;}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return EXIT_FAILURE;}}
