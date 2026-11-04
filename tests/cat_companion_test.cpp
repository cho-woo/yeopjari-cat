#include "../src/cat_companion.hpp"
#include <iostream>
#include <map>
#include <set>
#include <stdexcept>

namespace {
using namespace cat::companion;
constexpr std::int64_t born=1700000000;
int checks=0;
#define CHECK(condition) do { ++checks; if(!(condition)) throw std::runtime_error(std::string(__FILE__)+":"+std::to_string(__LINE__)+" " #condition); } while(false)
struct IO {
    std::map<std::string,std::string> files,preserved;
    std::set<std::string> unreadable,replaceDenied;
    bool badWrite=false,preserveDenied=false;
    std::string concurrent;
    bool Exists(const std::string& name) { return files.count(name)!=0; }
    bool Read(const std::string& name,std::string& out) { if(!Exists(name)||unreadable.count(name)) return false; out=files.at(name); return true; }
    bool Write(const std::string& name,const std::string& bytes) {
        if(Exists(name)) return false;
        files[name]=badWrite?"truncated":bytes;
        if(!concurrent.empty()) { files["companion.ini"]=concurrent; concurrent.clear(); }
        return true;
    }
    bool Replace(const std::string& name,const std::string& destination) {
        if(replaceDenied.count(destination)||!Exists(name)) return false;
        files[destination]=files.at(name); files.erase(name); return true;
    }
    bool Preserve(const std::string& name) { if(preserveDenied||!Exists(name)) return false; preserved[name+"-"+std::to_string(preserved.size())]=files.at(name); return true; }
    bool Remove(const std::string& name) { return files.erase(name)!=0; }
};
std::string Rehash(std::string text) {
    text=text.substr(0,text.rfind("checksum="));
    std::ostringstream out; out<<text<<"checksum="<<std::hex<<std::setw(16)<<std::setfill('0')<<cat::Checksum(text)<<'\n';return out.str();
}
void NoTemps(const IO& io) { for(const auto& entry:io.files) CHECK(entry.first.find(".tmp-")==std::string::npos); }
void SeparateAdoptionAndRestart() {
    IO io;
    for(const auto name:{"pet.ini","pet.bak","memory.ini","memory.bak","nutrition.ini","preferences.ini","sound.ini"}) io.files[name]=std::string("first cat ")+name;
    const auto original=io.files;
    Store store; store.Load(io,born);
    CHECK(store.outcome==Outcome::Missing); CHECK(!store.available()); CHECK(!store.enabled());
    CHECK(!store.Save(io)); CHECK(io.files==original); CHECK(store.Adopt(born)); CHECK(!store.Adopt(born+1));
    CHECK(store.state.pet.bornUtc==born); CHECK(store.state.life.temperament!=cat::life::Temperament::Legacy);
    CHECK(store.Save(io)); const auto first=io.files.at("companion.ini");
    store.state.enabled=false; CHECK(store.Save(io));
    CHECK(io.files.at("companion.bak")==first);
    Store restart; restart.Load(io,born+86400);
    CHECK(restart.available()); CHECK(!restart.enabled()); CHECK(restart.state.pet.bornUtc==born);
    restart.state.enabled=true; CHECK(restart.Save(io));
    for(const auto& entry:original) CHECK(io.files.at(entry.first)==entry.second);
    NoTemps(io);
}
void IndividualStableTemperaments() {
    std::set<cat::life::Temperament> seen;
    for(int day=0;day<96;++day) {
        const auto birthday=born+day*86400;
        const auto original=NewState(birthday);
        const auto again=NewState(birthday);
        CHECK(original.life.temperament==again.life.temperament);
        seen.insert(original.life.temperament);
        State restored;std::string error;
        CHECK(Parse(Serialize(original),restored,error));
        CHECK(restored.life.temperament==original.life.temperament);
    }
    CHECK(seen.size()==6);
    IO io;auto existing=NewState(born);existing.life.temperament=cat::life::Temperament::Sleepy;
    io.files["companion.ini"]=Serialize(existing);
    Store loaded;loaded.Load(io,born+365*86400);
    CHECK(loaded.state.life.temperament==cat::life::Temperament::Sleepy); // Loading never rerolls existing personality.
}
void StrictBundleParsing() {
    State s=NewState(born), parsed=NewState(born+1); std::string error;
    const auto valid=Serialize(s), before=Serialize(parsed);
    CHECK(!valid.empty()); CHECK(Parse(valid,parsed,error)); CHECK(Serialize(parsed)==valid);
    for(auto malformed:{valid.substr(0,100),valid+"unknown=1\n",std::string("garbage")}) {
        parsed=NewState(born+1); CHECK(!Parse(malformed,parsed,error)); CHECK(Serialize(parsed)==before);
    }
    for(const auto field:{"schema=99\n","enabled=2\n","sniffs=NaN\n","unknown=1\n"}) {
        auto malformed=valid; malformed.insert(malformed.find("checksum="),field); malformed=Rehash(malformed);
        parsed=NewState(born+1); CHECK(!Parse(malformed,parsed,error)); CHECK(Serialize(parsed)==before);
    }
    s.memory.bornUtc=born-1; CHECK(Serialize(s).empty());
    s=NewState(born); s.pet.feedCount=1; CHECK(Serialize(s).empty());
    s=NewState(born); s.life.bornUtc=born-1; CHECK(Serialize(s).empty());
    s=NewState(born); s.nutrition.fullness=std::numeric_limits<double>::quiet_NaN(); CHECK(Serialize(s).empty());
    s=NewState(born); s.sniffs=cat::memory::kMaxCount+1; CHECK(Serialize(s).empty());
}
void RecoveryAndFaults() {
    const auto healthy=Serialize(NewState(born));
    IO io; io.files={{"companion.ini","damaged"},{"companion.bak",healthy},{"pet.ini","first original"}};
    Store recovered; recovered.Load(io,born+60); CHECK(recovered.outcome==Outcome::Recovered);
    io.preserveDenied=true; CHECK(!recovered.Save(io)); CHECK(io.files.at("companion.ini")=="damaged"); CHECK(io.files.at("companion.bak")==healthy); NoTemps(io);
    io.preserveDenied=false; CHECK(recovered.Save(io)); CHECK(io.files.at("companion.bak")==healthy); CHECK(io.preserved.size()==1);
    CHECK(io.preserved.begin()->second=="damaged"); CHECK(io.files.at("pet.ini")=="first original");
    io.files["companion.ini"]="broken";io.files["companion.bak"]="broken backup";
    Store blocked; blocked.Load(io,born+60); CHECK(blocked.outcome==Outcome::Blocked); CHECK(!blocked.Adopt(born+60)); CHECK(!blocked.Save(io)); CHECK(io.files.at("companion.ini")=="broken");
    io={}; Store fresh; fresh.Load(io,born); CHECK(fresh.Adopt(born)); io.badWrite=true; CHECK(!fresh.Save(io)); CHECK(io.files.empty());
    io.badWrite=false; CHECK(fresh.Save(io)); fresh.state.enabled=false;
    io.replaceDenied.insert("companion.ini"); CHECK(!fresh.Save(io)); CHECK(io.files.at("companion.ini")==healthy); CHECK(io.files.at("companion.bak")==healthy); NoTemps(io);
    io.replaceDenied.clear(); CHECK(fresh.Save(io));
    auto external=NewState(born); external.sniffs=3;
    io.concurrent=Serialize(external); CHECK(!fresh.Save(io)); CHECK(io.files.at("companion.ini")==Serialize(external)); NoTemps(io);
    Store unread; io.unreadable.insert("companion.ini"); unread.Load(io,born+100); CHECK(unread.outcome==Outcome::Recovered); CHECK(!unread.Save(io));
    io.unreadable.clear(); Store loaded; loaded.Load(io,born+100); loaded.state.pet.bornUtc=born+1; CHECK(!loaded.Save(io));
}
Context Frame(std::uint64_t ticks,double x,double lift=0) {
    Context c; c.nowUtc=born+static_cast<std::int64_t>(ticks/1000); c.ticks=ticks; c.elapsedSeconds=1.0/60;
    c.primary.x=x;c.primary.lift=lift;c.primary.grounded=true;return c;
}
void Step(Model& model,Context& c,unsigned frames) {
    for(unsigned i=0;i<frames;++i) { c.ticks+=17;c.nowUtc=born+static_cast<std::int64_t>(c.ticks/1000); model.Tick(c); }
}
void IndependentCareAndAge() {
    auto first=cat::NewPet(born-365*86400); cat::Feed(first,born,false); const auto original=cat::Serialize(first);
    State second=NewState(born); second.nutrition.fullness=50; second.pet.hunger=50;
    Model model; model.Bind(second); model.Reset(40,1000,700);
    CHECK(model.Feed(born,100,false)==cat::nutrition::FeedResult::Accepted);
    CHECK(model.Busy());
    CHECK(second.pet.feedCount==1 && second.memory.meals==1 && second.pet.snackCount==0);
    CHECK(second.nutrition.fullness==85); CHECK(cat::Serialize(first)==original);
    CHECK(model.Feed(born+1,1100,true)==cat::nutrition::FeedResult::TooSoon); CHECK(second.pet.snackCount==0);
    CHECK(model.Pet(born+600,600100)); CHECK(!model.Pet(born+601,601100)); CHECK(second.memory.pets==1);
    CHECK(second.pet.bornUtc==born); CHECK(second.memory.bornUtc==born); CHECK(second.life.bornUtc==born);
    CHECK(model.View(601100).growthScale<cat::GrowthScale(first.bornUtc,born));
    auto c=Frame(602000,800); c.primaryVisible=false;c.paused=true;
    const auto pose=model.Physical(); Step(model,c,600);
    CHECK(std::abs(model.Physical().x-pose.x)<.001); CHECK(second.memory.pets==1); CHECK(cat::Serialize(first)==original);
    const auto after=second.pet.updatedUtc;c.nowUtc=born-3600;model.Tick(c);CHECK(second.pet.updatedUtc==after);
    CHECK(Valid(second));
}
void SocialContactAndCompetition() {
    for(const auto kind:{Interaction::Sniff,Interaction::Paw,Interaction::SharedSleep,Interaction::RunTogether,Interaction::FoodCompetition}) {
        State s=NewState(born);s.nutrition.fullness=60;s.pet.hunger=60;
        Model m;m.Bind(s);m.Reset(40,1000,700);
        auto c=Frame(100,m.Physical().x+(kind==Interaction::Paw?48:60));
        if(kind==Interaction::SharedSleep)c.primaryMotion=cat::animation::Motion::Sleep;
        if(kind==Interaction::RunTogether){c.primaryMotion=cat::animation::Motion::Zoomies;c.primary.velocityX=60;}
        if(kind==Interaction::FoodCompetition){c.primaryMotion=cat::animation::Motion::Eat;c.primaryBusy=true;}
        const auto first=c.primary;
        CHECK(m.TrySocial(kind,c)); m.Tick(c); CHECK(m.TakeEvent()==kind); CHECK(m.TakeEvent()==Interaction::None);
        CHECK(s.pet.feedCount==0 && s.memory.meals==0 && s.memory.pets==0 && s.memory.plays==0);
        CHECK(c.primary.x==first.x && c.primary.lift==first.lift);
        if(kind==Interaction::Sniff){CHECK(s.sniffs==1);CHECK(m.View(c.ticks).motion==cat::animation::Motion::Sniff);}
        if(kind==Interaction::Paw || kind==Interaction::RunTogether)CHECK(s.mutualPlay==1);
        if(kind==Interaction::SharedSleep){CHECK(s.sharedSleeps==1);CHECK(m.View(c.ticks).motion==cat::animation::Motion::Sleep);}
        if(kind==Interaction::FoodCompetition)CHECK(s.foodRaces==1);
        Step(m,c,30); CHECK(s.sniffs+s.mutualPlay+s.sharedSleeps+s.foodRaces==1);
        CHECK(Valid(s));
    }
    State s=NewState(born); Model m;m.Bind(s);m.Reset(40,1000,700);
    auto c=Frame(100,m.Physical().x+60); CHECK(!m.FoodOffered(c)); // Full cat does not race for the bowl.
    c.primary.lift=200;CHECK(!m.TrySocial(Interaction::Sniff,c));
    c.primary.lift=0;c.paused=true;CHECK(!m.TrySocial(Interaction::Paw,c));
    c.paused=false;s.pet.roamingPaused=true;CHECK(!m.TrySocial(Interaction::Follow,c));
}
void RealWindowFollowingAndClosure() {
    State s=NewState(born);s.pet.position=.2;
    Model m;m.Bind(s);m.Reset(40,1000,700);
    auto c=Frame(100,460,180);c.primary.onPlatform=true;
    c.primaryPlatform={73,300,760,180,true};c.supportPlatform=c.primaryPlatform;
    CHECK(m.TrySocial(Interaction::WindowFollow,c)); CHECK(m.SupportId()==73); CHECK(!m.Physical().grounded);
    CHECK(m.Busy());
    const auto food=s.pet.feedCount; CHECK(m.Feed(born,100,false)==cat::nutrition::FeedResult::TooSoon);CHECK(!m.Pet(born,100));CHECK(s.pet.feedCount==food);
    bool landed=false,movingLanding=false; double apex=0;
    for(unsigned i=0;i<240;++i) {
        Step(m,c,1);apex=std::max(apex,m.Physical().lift);
        const auto view=m.View(c.ticks);
        if(!landed && m.Physical().onPlatform && std::abs(view.physical.velocityX-view.physical.supportVelocityX)>2) {
            movingLanding=true;CHECK(cat::animation::Walking(view.motion));CHECK(view.animation.frame>=0 && view.animation.frame<=3);
        }
        landed=landed||m.Physical().onPlatform;
    }
    CHECK(movingLanding);
    CHECK(apex>180); CHECK(landed); CHECK(m.Physical().onPlatform); CHECK(std::abs(m.Physical().lift-180)<1);
    CHECK(m.Physical().x>=c.primaryPlatform.left && m.Physical().x<=c.primaryPlatform.right);
    const auto oldX=m.Physical().x;
    // The second cat keeps its own ledge even if the first cat leaves it.
    c.primary.onPlatform=false;c.primaryPlatform={};c.primary.lift=0;
    for(unsigned i=0;i<60;++i) { c.supportPlatform.left+=.5;c.supportPlatform.right+=.5;Step(m,c,1); }
    CHECK(m.Physical().onPlatform); CHECK(m.Physical().x>oldX+20);
    c.supportPlatform={};Step(m,c,1);CHECK(m.TakeEvent()==Interaction::Startled);CHECK(m.SupportId()==0);
    Step(m,c,180);CHECK(m.Physical().grounded);CHECK(!m.Physical().onPlatform);CHECK(m.Physical().lift<1);
    c.primary.onPlatform=true;c.primary.lift=510;c.primaryPlatform={99,2000,2300,510,true};CHECK(!m.TrySocial(Interaction::WindowFollow,c));
}
void SpeciesLimitsAndBraking() {
    State s=NewState(born);s.pet.position=.25;s.preferences.character=cat::Character::Munchkin;
    Model m;m.Bind(s);m.Reset(40,1000,800);
    auto c=Frame(100,500,650);c.primary.onPlatform=true;c.primaryPlatform={20,200,850,650,true};
    CHECK(!m.TrySocial(Interaction::WindowFollow,c));
    s.preferences.character=cat::Character::Cream;
    CHECK(m.TrySocial(Interaction::WindowFollow,c));
    c.supportPlatform=c.primaryPlatform;
    Step(m,c,180);CHECK(m.Physical().onPlatform);
    m.DragTo(400);c=Frame(4000,600);s.nutrition.fullness=50;s.pet.hunger=50;
    CHECK(m.TrySocial(Interaction::Follow,c));Step(m,c,30);
    CHECK(m.Physical().velocityX>10);
    CHECK(m.Feed(c.nowUtc,c.ticks,false)==cat::nutrition::FeedResult::Accepted);
    CHECK(cat::animation::Travels(m.View(c.ticks).motion)); // A seated eating frame cannot slide during braking.
    Step(m,c,30);CHECK(std::abs(m.Physical().velocityX)<2);CHECK(m.View(c.ticks).motion==cat::animation::Motion::Eat);
    CHECK(Valid(s));
    m.DragTo(700);
    State saved;std::string error;
    CHECK(Parse(Serialize(s),saved,error));
    CHECK(std::abs(saved.pet.position-(700.0-40)/960)<.000001);
    Model ceiling;ceiling.Bind(s);ceiling.Reset(40,1000,120);ceiling.DragTo(400);
    auto high=Frame(10000,520,100);high.primary.onPlatform=true;high.primaryPlatform={88,300,850,100,true};
    CHECK(!ceiling.TrySocial(Interaction::WindowFollow,high)); // Drag retains the real work-area roof.
}
void ManualCareIsNotInterrupted() {
    for(bool treat:{false,true}) {
        State s=NewState(born);s.nutrition.fullness=45;s.pet.hunger=45;
        Model m;m.Bind(s);m.Reset(40,1000,700);
        CHECK(m.Feed(born,100,treat)==cat::nutrition::FeedResult::Accepted);
        const auto prior=Serialize(s);
        CHECK(!m.Pet(born+1,1100));CHECK(!m.Play(born+1,1100));CHECK(Serialize(s)==prior);
        auto c=Frame(1100,m.Physical().x+180);c.primaryMotion=cat::animation::Motion::Eat;c.primaryBusy=true;
        CHECK(!m.FoodOffered(c));
        c.primaryBusy=false;CHECK(!m.TrySocial(Interaction::Follow,c));
        m.Tick(c);CHECK(m.View(c.ticks).motion==(treat?cat::animation::Motion::Treat:cat::animation::Motion::Eat));
        // A menu freezes the complete accepted meal, including its social gate.
        c.paused=true;c.ticks+=10000;c.nowUtc+=10;m.Tick(c);
        c.paused=false;c.ticks+=100;c.nowUtc+=1;
        CHECK(!m.FoodOffered(c));CHECK(!m.Pet(c.nowUtc,c.ticks));CHECK(!m.Play(c.nowUtc,c.ticks));
        c.ticks=14301;c.nowUtc=born+15;
        CHECK(m.FoodOffered(c)); // Ordinary interaction may resume after the preserved meal ends.
        CHECK(s.pet.feedCount==(treat?0U:1U));CHECK(s.pet.snackCount==(treat?1U:0U));CHECK(Valid(s));
        m.Pet(born+301,301100); // A manual pet also takes priority over an automatic proposal.
        c.ticks=301200;c.nowUtc=born+301;CHECK(!m.TrySocial(Interaction::Sniff,c));
    }
}
void NaturalTakeoffAndLandingUsePhysicalMotion() {
    using M=cat::animation::Motion;
    // This actual birthday/brain sequence previously showed a seated frame 14
    // after a jump while Box2D still moved at more than 100 logical px/second.
    const auto birthday=born+7*86400;
    State s=NewState(birthday);s.pet.position=.5;
    Model m;m.Bind(s);m.Reset(0,1600,700);
    Context c;c.primaryVisible=false;c.elapsedSeconds=1.0/60;
    bool wasGrounded=true,badSliding=false,badFacing=false;
    unsigned takeoffs=0;std::uint64_t crouchStarted=0;
    for(unsigned frame=1;frame<24000;++frame) {
        c.ticks=frame*17;c.nowUtc=birthday+static_cast<std::int64_t>(c.ticks/1000);m.Tick(c);
        const auto view=m.View(c.ticks);
        const auto physical=m.Physical();
        const double speed=view.physical.velocityX-view.physical.supportVelocityX;
        if(view.physical.grounded && std::abs(speed)>2) {
            badSliding=badSliding || !cat::animation::Walking(view.motion) || view.animation.frame<0 || view.animation.frame>3;
            badFacing=badFacing || view.animation.flip!=(speed<0);
        }
        if(view.motion!=M::Jump && view.motion!=M::Pounce)crouchStarted=0;
        if(view.physical.grounded && (view.motion==M::Jump || view.motion==M::Pounce) && view.animation.frame==4) {
            if(!crouchStarted)crouchStarted=c.ticks;
            CHECK(std::abs(speed)<=2);
            CHECK(view.animation.scaleX==1 && view.animation.scaleY==1 && view.animation.angle==0);
        }
        if(wasGrounded && !physical.grounded) {
            ++takeoffs;
            CHECK(crouchStarted>0 && c.ticks-crouchStarted>=204);
            CHECK(!view.animation.groundContact);
            crouchStarted=0;
        }
        wasGrounded=physical.grounded;
    }
    CHECK(takeoffs>0);CHECK(!badSliding);CHECK(!badFacing);CHECK(Valid(s));
}
void SocialGesturesWaitForPlantedFeet() {
    using M=cat::animation::Motion;
    for(const auto kind:{Interaction::Sniff,Interaction::Paw,Interaction::SharedSleep,Interaction::FoodCompetition}) {
        State s=NewState(born);s.pet.position=.2;s.nutrition.fullness=60;s.pet.hunger=60;
        Model m;m.Bind(s);m.Reset(0,1600,700);
        auto c=Frame(100,m.Physical().x+200);c.primaryMotion=M::Zoomies;
        CHECK(m.TrySocial(Interaction::RunTogether,c));Step(m,c,30);CHECK(m.Physical().velocityX>40);
        c.primary.x=m.Physical().x+(kind==Interaction::Paw?48:65);
        c.primaryMotion=kind==Interaction::SharedSleep?M::Sleep:kind==Interaction::FoodCompetition?M::Eat:M::Idle;
        CHECK(m.TrySocial(kind,c));m.Tick(c);
        CHECK(m.TakeEvent()==Interaction::None);CHECK(s.sniffs+s.mutualPlay+s.sharedSleeps+s.foodRaces==0);
        CHECK(cat::animation::Travels(m.View(c.ticks).motion));
        bool contacted=false;
        for(unsigned frame=0;frame<90;++frame) {
            Step(m,c,1);
            if(m.TakeEvent()==kind) {
                contacted=true;
                CHECK(std::abs(m.Physical().velocityX-m.Physical().supportVelocityX)<=2);
                CHECK(!cat::animation::Travels(m.View(c.ticks).motion));
            }
        }
        CHECK(contacted);CHECK(s.sniffs+s.mutualPlay+s.sharedSleeps+s.foodRaces==1);CHECK(Valid(s));
    }
}
void BrakingPreservesTheCompleteMeal() {
    State s=NewState(born);s.pet.position=.2;s.nutrition.fullness=45;s.pet.hunger=45;
    Model m;m.Bind(s);m.Reset(0,1600,700);
    auto c=Frame(100,m.Physical().x+200);c.primaryMotion=cat::animation::Motion::Zoomies;
    CHECK(m.TrySocial(Interaction::RunTogether,c));Step(m,c,30);CHECK(m.Physical().velocityX>40);
    const auto acceptedAt=c.ticks;
    CHECK(m.Feed(c.nowUtc,c.ticks,false)==cat::nutrition::FeedResult::Accepted);
    c.primaryVisible=false;
    while(c.ticks<acceptedAt+4250)Step(m,c,1);
    CHECK(m.View(c.ticks).motion==cat::animation::Motion::Eat);
    CHECK(!m.Pet(c.nowUtc,c.ticks)); // Braking time cannot consume the accepted meal.
    CHECK(s.pet.feedCount==1 && s.memory.meals==1 && s.memory.pets==0);
    Step(m,c,120);
    CHECK(!m.Pet(c.nowUtc,c.ticks)); // Gesture resumes, but shared care credit keeps its five-minute limit.
    CHECK(m.View(c.ticks).motion==cat::animation::Motion::TailRaise);
    CHECK(s.memory.pets==0);CHECK(Valid(s));
}
void PairedRunningUsesOneSupportAndBodyMultiplier() {
    for(bool perched:{false,true}) {
        State s=NewState(born);s.pet.position=.2;s.preferences.character=cat::Character::Munchkin;
        Model m;m.Bind(s);m.Reset(0,2400,700);
        auto c=Frame(100,700,perched?120:0);
        if(perched) {
            c.primary.onPlatform=true;c.primaryPlatform={912,400,2100,120,true};c.supportPlatform=c.primaryPlatform;
            CHECK(m.TrySocial(Interaction::WindowFollow,c));
            for(unsigned i=0;i<240 && !m.Physical().onPlatform;++i)Step(m,c,1);
            CHECK(m.Physical().onPlatform);Step(m,c,1);m.TakeEvent();
        }
        c.primary.x=m.Physical().x+60;c.primaryMotion=cat::animation::Motion::Zoomies;
        c.primary.supportVelocityX=perched?30:0;c.primary.velocityX=60+c.primary.supportVelocityX;
        CHECK(m.TrySocial(Interaction::RunTogether,c));
        for(unsigned i=0;i<120;++i) {
            // Feed the same measured 60 px/s relative pace at a steady spacing;
            // actual Box2D still supplies all secondary acceleration and travel.
            c.primary.x=m.Physical().x+60;
            if(perched) {
                c.primaryPlatform.left+=.5;c.primaryPlatform.right+=.5;c.supportPlatform=c.primaryPlatform;
            }
            Step(m,c,1);
        }
        CHECK(std::abs(m.Physical().velocityX-m.Physical().supportVelocityX-60)<1.5);
        if(perched)CHECK(m.Physical().onPlatform && std::abs(m.Physical().supportVelocityX-30)<.1);
        CHECK(Valid(s));
    }
}
void HighWindowFollowingRespectsTheRealCeiling() {
    State s=NewState(born);s.pet.position=.3;
    Model m;m.Bind(s);m.Reset(0,1800,1040);
    auto c=Frame(100,780,640);c.primary.onPlatform=true;
    c.primaryPlatform={400,450,1300,640,true};c.supportPlatform=c.primaryPlatform;
    CHECK(m.TrySocial(Interaction::WindowFollow,c));
    for(unsigned i=0;i<240 && !m.Physical().onPlatform;++i)Step(m,c,1);
    CHECK(m.Physical().onPlatform);CHECK(std::abs(m.Physical().lift-640)<1);
    Step(m,c,30);
    // A second jump starts from the first ledge, not from the taskbar. Its
    // absolute height can exceed the old520px cap without exceeding the roof.
    c.primary.lift=900;c.primary.x=980;c.primaryPlatform={401,650,1500,900,true};
    CHECK(m.TrySocial(Interaction::WindowFollow,c));c.supportPlatform=c.primaryPlatform;
    for(unsigned i=0;i<240 && !m.Physical().onPlatform;++i)Step(m,c,1);
    CHECK(m.Physical().onPlatform);CHECK(std::abs(m.Physical().lift-900)<1);CHECK(m.SupportId()==401);
    Step(m,c,30);
    c.primary.lift=1020;c.primaryPlatform={402,700,1600,1020,true};
    CHECK(!m.TrySocial(Interaction::WindowFollow,c));
    CHECK(m.Physical().onPlatform && m.SupportId()==401);CHECK(Valid(s));
}
}
int main() {
    try { SeparateAdoptionAndRestart();IndividualStableTemperaments();StrictBundleParsing();RecoveryAndFaults();IndependentCareAndAge();SocialContactAndCompetition();RealWindowFollowingAndClosure();SpeciesLimitsAndBraking();ManualCareIsNotInterrupted();NaturalTakeoffAndLandingUsePhysicalMotion();SocialGesturesWaitForPlantedFeet();BrakingPreservesTheCompleteMeal();PairedRunningUsesOneSupportAndBodyMultiplier();HighWindowFollowingRespectsTheRealCeiling();
        std::cout<<"companion checks passed: "<<checks<<'\n';return 0;
    } catch(const std::exception& e) { std::cerr<<e.what()<<'\n';return 1; }
}
