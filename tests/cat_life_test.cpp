#include "../src/cat_life.hpp"
#include "../src/cat_life_storage.hpp"
#include "../src/cat_behavior.hpp"
#include <iostream>
#include <map>
#include <set>
#include <stdexcept>

namespace {
int checks=0;
#define CHECK(c) do{++checks;if(!(c))throw std::runtime_error("life check failed line "+std::to_string(__LINE__));}while(false)
using namespace cat::life;
constexpr auto Day=cat::life::Day;
std::int64_t Utc(int y,unsigned m,unsigned d,int hour=12){return cat::ToUnix({y,m,d,hour});}
State Legacy(std::int64_t born=Utc(2020,2,29),std::int64_t now=Utc(2026,11,4)){
    auto pet=cat::NewPet(born);pet.updatedUtc=now;return FromLegacy(pet,now);
}
EventContext Context(std::int64_t now=Utc(2026,11,4)){
    EventContext c;c.nowUtc=now;c.localDay=now/Day;c.busy=false;return c;
}
std::string Rehash(std::string body){
    std::ostringstream out;out<<body<<"checksum="<<std::hex<<std::setw(16)<<std::setfill('0')<<cat::Checksum(body)<<'\n';return out.str();
}
void IdentityAndParser(){
    auto pet=cat::NewPet(Utc(2026,11,4));
    const auto old=cat::Serialize(pet);
    CHECK(FromLegacy(pet,pet.updatedUtc).temperament==Temperament::Legacy);
    std::set<int> types;
    for(unsigned entropy=0;entropy<100;++entropy){
        auto state=NewPet(pet,pet.updatedUtc,entropy);types.insert(static_cast<int>(state.temperament));
        const auto encoded=Serialize(state);State parsed;std::string error;
        CHECK(Parse(encoded,parsed,error));CHECK(parsed.temperament==state.temperament);
        CHECK(NewPet(pet,pet.updatedUtc,entropy).temperament==state.temperament);
        CHECK(cat::Serialize(pet)==old);
    }
    CHECK(types.size()==6);
    auto state=Legacy(),destination=state;std::string error;
    auto text=Serialize(state);const auto original=Serialize(destination);
    for(std::size_t i=0;i+1<text.size();++i){CHECK(!Parse(text.substr(0,i),destination,error));CHECK(Serialize(destination)==original);}
    const auto body=text.substr(0,text.find("checksum="));
    for(auto field:{std::string("mood=nan\n"),std::string("newfield=0\n"),std::string("schema=2\n")}){
        CHECK(!Parse(Rehash(body+field),destination,error));CHECK(Serialize(destination)==original);
    }
    for(auto pair:{std::pair<std::string,std::string>{"mood=0.59999999999999998","mood=1.2"},
                   {"temperament=-1","temperament=6"},{"unlocked_hats=0","unlocked_hats=8"}}){
        auto bad=body;const auto pos=bad.find(pair.first);CHECK(pos!=std::string::npos);bad.replace(pos,pair.first.size(),pair.second);
        CHECK(!Parse(Rehash(bad),destination,error));CHECK(Serialize(destination)==original);
    }
}
void HiddenMood(){
    auto sleeping=Legacy(),awake=sleeping,engaged=sleeping;
    auto now=sleeping.updatedUtc;
    for(int i=1;i<=3600;++i){
        Observe(sleeping,now+i,{23,true,false,true,80});
        Observe(awake,now+i,{23,false,false,true,80});
        Observe(engaged,now+i,{12,false,true,true,80});
    }
    CHECK(sleeping.sleepiness<awake.sleepiness);
    CHECK(engaged.boredom<awake.boredom);CHECK(engaged.mood>awake.mood);
    CHECK(engaged.activity>sleeping.activity);
    CHECK(Summarize(engaged,90).shyness<Summarize(engaged,0).shyness);
    const auto before=Serialize(engaged);
    Observe(engaged,now,{12,false,true,true,80});CHECK(Serialize(engaged)==before);
    const auto oldMood=engaged.mood,oldSleep=engaged.sleepiness;
    Observe(engaged,now+10*365*Day,{3,false,false,true,20});
    CHECK(engaged.mood==oldMood&&engaged.sleepiness==oldSleep);
    auto a=Legacy(),b=a;const auto start=a.updatedUtc;
    for(int i=1;i<=3600;++i)Observe(a,start+i,{12,false,false,true,80});
    for(int i=60;i<=3600;i+=60)Observe(b,start+i,{12,false,false,true,80});
    CHECK(std::abs(a.mood-b.mood)<1e-12);CHECK(std::abs(a.sleepiness-b.sleepiness)<1e-12);
    for(int t=-1;t<=5;++t){auto s=Legacy();s.temperament=static_cast<Temperament>(t);const auto p=Summarize(s,50);
        for(double v:{p.activityScale,p.restScale,p.socialScale,p.curiosityScale,p.foodScale})CHECK(v>=.25&&v<=2);
    }
}
void RareCadence(){
    auto s=Legacy();const auto begin=(s.updatedUtc/Day+1)*Day;
    int noEventDays=0,total=0;
    for(int day=0;day<120;++day){
        unsigned today=0;
        for(int hour=0;hour<24;++hour){
            auto c=Context(begin+day*Day+hour*3600);c.trigger=Trigger::Punch;c.seasonalEnabled=false;
            const auto e=PeekEvent(s,c);
            if(e==Event::None)continue;
            CHECK(e==Event::PunchStartle);
            CHECK(CommitEvent(s,c,e));CHECK(Valid(s));++today;++total;
            State restored;std::string error;CHECK(Parse(Serialize(s),restored,error));s=restored;
            CHECK(!CommitEvent(s,c,e));
            auto rollback=c;rollback.nowUtc-=1;CHECK(PeekEvent(s,rollback)==Event::None);
            auto busy=c;busy.nowUtc+=6*3600;busy.localDay=busy.nowUtc/Day;busy.busy=true;CHECK(PeekEvent(s,busy)==Event::None);
        }
        CHECK(today<=2);if(!today)++noEventDays;
    }
    CHECK(total>40&&total<240);CHECK(noEventDays>20);
    auto c=Context(s.updatedUtc+Day);c.roamingPaused=true;c.trigger=Trigger::Punch;
    CHECK(!Eligible(s,c,Event::PunchStartle));
    c.localDay+=100;CHECK(PeekEvent(s,c)==Event::None);
}
void EventConditions(){
    auto s=Legacy();auto c=Context(s.updatedUtc);while(DailyRareQuota(s,c.localDay)==0){c.nowUtc+=Day;++c.localDay;}
    c.trigger=Trigger::Punch;CHECK(Eligible(s,c,Event::PunchStartle));CHECK(!Eligible(s,c,Event::TailStartle));
    c.trigger=Trigger::TailChase;CHECK(Eligible(s,c,Event::TailStartle));
    c.trigger=Trigger::Sneeze;CHECK(Eligible(s,c,Event::SneezeStartle));
    c.trigger=Trigger::None;c.sleeping=true;CHECK(Eligible(s,c,Event::SleepTalk));CHECK(!Eligible(s,c,Event::Yawn));
    c.sleeping=false;c.boxAvailable=true;c.crownWorn=true;CHECK(Eligible(s,c,Event::SmallBox));CHECK(Eligible(s,c,Event::CrownPride));
    c.perched=true;CHECK(!Eligible(s,c,Event::SmallBox));CHECK(!Eligible(s,c,Event::Peek));
    CHECK(!Eligible(s,c,Event::EdgeHang));c.edgeSupported=true;c.edgeDistanceLogical=18;
    CHECK(Eligible(s,c,Event::EdgeHang));c.edgeDistanceLogical=25;CHECK(!Eligible(s,c,Event::EdgeHang));
    c.screenCrossingSafe=true;CHECK(!Eligible(s,c,Event::ScreenCrossing));c.perched=false;
    CHECK(Eligible(s,c,Event::ScreenCrossing));c.roamingPaused=true;CHECK(!Eligible(s,c,Event::ScreenCrossing));
}
void Calendar(){
    const auto born=Utc(2024,2,29);auto s=Legacy(born,born);
    auto c=Context(born+7*Day);CHECK(PeekEvent(s,c)==Event::FirstWeek);CHECK(CommitEvent(s,c,Event::FirstWeek));CHECK(!Eligible(s,c,Event::FirstWeek));
    c=Context(cat::Anniversary(born,1));CHECK(PeekEvent(s,c)==Event::FirstYear);
    CHECK(CommitEvent(s,c,Event::FirstYear));CHECK(s.unlockedHats&BirthdayHat);CHECK(!Eligible(s,c,Event::FirstYear));
    c=Context(cat::Anniversary(born,5));CHECK(PeekEvent(s,c)==Event::FifthYear);
    CHECK(CommitEvent(s,c,Event::FifthYear));
    c=Context(cat::Anniversary(born,6));CHECK(PeekEvent(s,c)==Event::Birthday);
    c=Context(Utc(2030,12,24));CHECK(SeasonalUnlocks(s,c)&SantaHat);CHECK(WinterScarfSuggested(s,c));
    CHECK(UnlockSeasonal(s,c));CHECK(!UnlockSeasonal(s,c));
    c=Context(Utc(2031,10,31));CHECK(SeasonalUnlocks(s,c)&PumpkinHat);CHECK(UnlockSeasonal(s,c));
    c.seasonalEnabled=false;CHECK(SeasonalUnlocks(s,c)==0);CHECK(!WinterScarfSuggested(s,c));
    CHECK(s.unlockedHats==7);CHECK(Valid(s));
}
void LifetimeUnlocks(){
    const auto born=Utc(2024,2,29);auto s=Legacy(born,born);auto c=Context(born);c.bond=100;
    for(auto e:{Event::BondKnead,Event::BondGreeting,Event::BondLifetime})CHECK(!Eligible(s,c,e));
    for(auto e:{Event::BondKnead,Event::BondGreeting,Event::BondLifetime}){
        const auto due=e==Event::BondKnead?born+90*Day:cat::Anniversary(born,e==Event::BondGreeting?1:5);
        c=Context(due-1);c.bond=100;CHECK(!Eligible(s,c,e));
        c=Context(due);c.bond=100;
        while(!DailyRareQuota(s,c.localDay)){c.nowUtc+=Day;++c.localDay;}
        CHECK(Eligible(s,c,e));
        auto hungry=c;hungry.fullness=35;CHECK(!Eligible(s,hungry,e));
        auto shy=c;shy.bond=e==Event::BondKnead?24.999:e==Event::BondGreeting?44.999:64.999;
        CHECK(!Eligible(s,shy,e));
        CHECK(CommitEvent(s,c,e));CHECK(Valid(s));
        for(auto other:{Event::BondKnead,Event::BondGreeting,Event::BondLifetime,Event::Yawn})CHECK(!Eligible(s,c,other));
    }
}
void CelebrationsWaitForActions(){
    const auto born=Utc(2024,2,29);auto state=Legacy(born,born);
    auto context=Context(cat::Anniversary(born,1));
    CHECK(PeekEvent(state,context)==Event::FirstYear);
    const auto saved=Serialize(state);
    for(const auto& pair:{std::pair<Trigger,Event>{Trigger::Punch,Event::PunchStartle},
            {Trigger::TailChase,Event::TailStartle},{Trigger::Sneeze,Event::SneezeStartle}}){
        auto running=context;running.trigger=pair.first;
        CHECK(!Eligible(state,running,Event::FirstYear));
        CHECK(!CommitEvent(state,running,Event::FirstYear));
        CHECK(Serialize(state)==saved);
        CHECK(!Eligible(state,running,Event::Yawn));
        const auto offered=PeekEvent(state,running);
        CHECK(offered==Event::None||offered==pair.second);
        running.roamingPaused=true;
        CHECK(PeekEvent(state,running)==Event::None);
    }
    CHECK(PeekEvent(state,context)==Event::FirstYear);
    CHECK(CommitEvent(state,context,Event::FirstYear));CHECK(Valid(state));
}
void CrossingTimeline(){
    ScreenCrossing c;CHECK(!c.Active());CHECK(c.Opacity()==1);c.Start();
    CHECK(c.Active());CHECK(c.Opacity()==1);CHECK(!c.Advance(200,false));CHECK(c.elapsedMs==0);
    CHECK(!c.Advance(1000,true));CHECK(c.elapsedMs==0);CHECK(!c.Advance(NAN,true));
    int relocated=0;
    for(int i=0;i<100;++i){
        const auto old=c.elapsedMs;
        if(c.Advance(16,true)){++relocated;CHECK(c.Opacity()==0);CHECK(c.elapsedMs==450);}
        CHECK(c.elapsedMs>=old);CHECK(c.Opacity()>=0&&c.Opacity()<=1);
    }
    CHECK(relocated==1);CHECK(!c.Active());CHECK(c.Opacity()==1);
    c.Start();CHECK(c.Active()&&!c.relocated);c={};CHECK(c.Opacity()==1&&!c.Active());
}
void UpgradeCooldown(){
    auto s=Legacy();const auto stamp=s.updatedUtc-60;
    CHECK(InheritRareCooldown(s,stamp));CHECK(Valid(s));
    CHECK(s.rareUtc[0]==stamp&&s.rareCount==1);
    const auto serialized=Serialize(s);CHECK(!InheritRareCooldown(s,stamp+1));CHECK(Serialize(s)==serialized);
    State restored;std::string error;CHECK(Parse(serialized,restored,error));
    for(int second=60;second<12*3600;second+=60){
        auto c=Context(stamp+second);c.inheritedRareUtc=stamp;c.trigger=Trigger::Punch;
        CHECK(!Eligible(restored,c,Event::PunchStartle));
    }
    auto c=Context(stamp+12*3600);c.inheritedRareUtc=stamp;c.trigger=Trigger::Punch;
    while(!Eligible(restored,c,Event::PunchStartle)){c.nowUtc+=3600;c.localDay=c.nowUtc/Day;}
    CHECK(CommitEvent(restored,c,Event::PunchStartle));CHECK(Valid(restored));
    for(int hour=1;hour<24;++hour){
        c=Context(stamp+hour*3600);c.inheritedRareUtc=stamp;c.trigger=Trigger::Punch;
        CHECK(!Eligible(restored,c,Event::PunchStartle));
    }
    auto future=Legacy();CHECK(InheritRareCooldown(future,future.updatedUtc+60));CHECK(Valid(future));
    c=Context(future.updatedUtc-1);CHECK(PeekEvent(future,c)==Event::None);
}
struct IO{
    std::map<std::string,std::string> files,preserved;bool failWrite=false,failReplace=false,failPreserve=false;
    bool Exists(const std::string& n){return files.count(n);}
    bool Read(const std::string& n,std::string& b){if(!Exists(n))return false;b=files[n];return true;}
    bool Write(const std::string& n,const std::string& b){if(failWrite||Exists(n))return false;files[n]=b;return true;}
    bool Replace(const std::string& a,const std::string& b){if(failReplace||!Exists(a))return false;files[b]=files[a];files.erase(a);return true;}
    bool Remove(const std::string& n){return files.erase(n)>0;}
    bool Preserve(const std::string& n){if(failPreserve||!Exists(n))return false;preserved[n]=files[n];return true;}
};
void Persistence(){
    IO io;auto pet=cat::NewPet(Utc(2026,11,4));const auto old=cat::Serialize(pet);io.files["pet.ini"]=old;
    cat::life::storage::Store store;store.Load(io,pet,pet.updatedUtc,true,42);CHECK(store.enabled());CHECK(store.Save(io));
    const auto first=Serialize(store.state);const auto type=store.state.temperament;
    cat::life::storage::Store reloaded;reloaded.Load(io,pet,pet.updatedUtc,true,900);CHECK(reloaded.state.temperament==type);
    Observe(reloaded.state,pet.updatedUtc+60,{12,false,true,true,88});CHECK(reloaded.Save(io));CHECK(io.files["life.bak"]==first);
    io.files["life.ini"]="corrupt";cat::life::storage::Store recovered;recovered.Load(io,pet,pet.updatedUtc);
    CHECK(recovered.outcome==cat::life::storage::Outcome::Recovered);CHECK(recovered.Save(io));CHECK(io.preserved["life.ini"]=="corrupt");
    auto foreign=pet;foreign.bornUtc+=1;foreign.updatedUtc+=1;cat::life::storage::Store wrong;wrong.Load(io,foreign,foreign.updatedUtc);
    CHECK(!wrong.enabled());const auto files=io.files;CHECK(!wrong.Save(io));CHECK(io.files==files);
    CHECK(io.files["pet.ini"]==old);
    IO fresh;cat::life::storage::Store legacy;legacy.Load(fresh,pet,pet.updatedUtc);CHECK(legacy.state.temperament==Temperament::Legacy);
    fresh.failWrite=true;CHECK(!legacy.Save(fresh));CHECK(fresh.files.empty());
    fresh.failWrite=false;fresh.failReplace=true;CHECK(!legacy.Save(fresh));CHECK(fresh.files.empty());
    fresh.failReplace=false;CHECK(legacy.Save(fresh));const auto saved=fresh.files;
    Observe(legacy.state,pet.updatedUtc+60,{12,false,true,true,88});fresh.failReplace=true;
    CHECK(!legacy.Save(fresh));CHECK(fresh.files==saved);
    fresh.failReplace=false;CHECK(legacy.Save(fresh));
    fresh.files["life.ini"]="corrupt";fresh.failPreserve=true;
    cat::life::storage::Store repair;repair.Load(fresh,pet,pet.updatedUtc);
    const auto damaged=fresh.files;CHECK(!repair.Save(fresh));CHECK(fresh.files==damaged);
    CHECK(fresh.preserved.empty());
}
void BehaviorEffect(){
    int sleepySleep=0,activeSleep=0,activeMove=0,sleepyMove=0;
    for(unsigned seed=1;seed<=3000;++seed){
        cat::behavior::Brain a(Utc(2020,1,1),seed),b(Utc(2020,1,1),seed);
        const auto before=a.GetBirthPersonality();a.SetDisposition(.5,1.8,1,1,1);b.SetDisposition(1.6,.5,1,1,1);
        cat::behavior::Context c;c.ticks=100000;c.localHour=15;
        const auto x=a.Next(c).action,y=b.Next(c).action;
        sleepySleep+=x==cat::behavior::Action::Sleep;activeSleep+=y==cat::behavior::Action::Sleep;
        sleepyMove+=x==cat::behavior::Action::Walk||x==cat::behavior::Action::Zoomies;
        activeMove+=y==cat::behavior::Action::Walk||y==cat::behavior::Action::Zoomies;
        CHECK(a.GetBirthPersonality().energy==before.energy);
        c.roamingPaused=true;const auto paused=a.Next(c).action;
        CHECK(paused!=cat::behavior::Action::Walk&&paused!=cat::behavior::Action::Zoomies&&paused!=cat::behavior::Action::Jump);
    }
    CHECK(sleepySleep>activeSleep*2);CHECK(activeMove>sleepyMove*1.5);
}
}
int main(){try{IdentityAndParser();HiddenMood();RareCadence();EventConditions();Calendar();LifetimeUnlocks();CelebrationsWaitForActions();CrossingTimeline();UpgradeCooldown();Persistence();BehaviorEffect();std::cout<<"Life/mood/events: "<<checks<<" checks passed\n";return 0;}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}
