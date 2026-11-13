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
std::uint64_t RegisteredGroundTransitionBudget() {
    const auto* walk=cat::sprite_assets::Find(cat::sprite_sequence::Clip::Walk,0,cat::sprite_assets::kCareSprites);
    return walk && walk->nominalFramesPerSecond>0?
        cat::sprite_sequence::Duration(walk->program,walk->program.intro)+
        cat::sprite_sequence::Duration(walk->program,walk->program.outro)+800:800;
}
void RequireMoving(Model& model,Context& c,double minimumSpeed) {
    const auto began=c.ticks;
    while(std::abs(model.Physical().velocityX-model.Physical().supportVelocityX)<=minimumSpeed &&
            c.ticks-began<RegisteredGroundTransitionBudget())Step(model,c,1);
    CHECK(std::abs(model.Physical().velocityX-model.Physical().supportVelocityX)>minimumSpeed);
}
void RequireCareStarts(Model& model,Context& c,cat::sprite_sequence::Clip clip) {
    const auto began=c.ticks;
    while(model.View(c.ticks).animation.spriteClip!=clip &&
            c.ticks-began<RegisteredGroundTransitionBudget())Step(model,c,1);
    const auto view=model.View(c.ticks);
    CHECK(view.animation.spriteClip==clip && view.animation.spriteTimed);
    CHECK(view.animation.spriteElapsedMs==0);
    CHECK(std::abs(view.physical.velocityX-view.physical.supportVelocityX)<cat::motion::kStoppedSpeed);
}
void AuthoredKneadDoesNotLeakLegacyJointsAcrossActions() {
    using M=cat::animation::Motion;
    for(unsigned reset=0;reset<5;++reset) {
        State s=NewState(born);s.pet.position=.2;s.nutrition.fullness=60;s.pet.hunger=60;
        Model model;model.Bind(s);model.Reset(0,1600,700);
        auto c=Frame(100,model.Physical().x+65);c.primaryMotion=M::Eat;
        CHECK(model.TrySocial(Interaction::FoodCompetition,c));model.Tick(c);Step(model,c,12);
        const auto kneading=model.View(c.ticks);
        CHECK(kneading.motion==M::Knead && kneading.animation.spriteClip==cat::sprite_sequence::Clip::Knead && kneading.animation.joints.frontLeft==0);
        CHECK(model.Pet(c.nowUtc,c.ticks));
        const auto record=Serialize(s);
        const auto start=model.View(c.ticks);
        CHECK(start.motion==M::TailRaise && start.animation.joints.frontLeft==kneading.animation.joints.frontLeft);
        CHECK(model.View(c.ticks).animation.joints.frontLeft==start.animation.joints.frontLeft);
        CHECK(Serialize(s)==record); // Rendering never changes saved care/growth data.
        if(reset==0) {
            Step(model,c,3);const auto middle=model.View(c.ticks);
            CHECK(middle.animation.joints.frontLeft==0);
            const auto physical=model.Physical();
            c.ticks+=5000;c.nowUtc+=5;c.paused=true;c.elapsedSeconds=5;model.Tick(c);
            const auto paused=model.View(c.ticks);
            CHECK(paused.animation.joints.frontLeft==middle.animation.joints.frontLeft);
            CHECK(model.Physical().x==physical.x && model.Physical().lift==physical.lift);
            c.paused=false;c.elapsedSeconds=1.0/60;Step(model,c,12);
            CHECK(model.View(c.ticks).animation.joints.frontLeft==0);
        } else {
            if(reset==1)model.Relocate(0,1600,700);
            if(reset==2)model.DragTo(model.Physical().x+50);
            if(reset==3)s.preferences.character=cat::Character::Rabbit;
            if(reset==4){model.Bind(s);model.Reset(0,1600,700);}
            CHECK(model.View(c.ticks).animation.joints.frontLeft==0);
        }
    }
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
void FishCareKeepsTheExistingMealAndBusyContracts() {
    using cat::feeding::FoodKind;
    using cat::nutrition::FeedResult;
    for(int index=0;index<cat::kCharacterCount;++index) {
        State state=NewState(born);state.preferences.character=static_cast<cat::Character>(index);
        state.nutrition.fullness=state.pet.hunger=45;
        Model model;model.Bind(state);model.Reset(40,1000,700);
        const auto before=Serialize(state);
        if(!cat::feeding::CanOfferFish(state.preferences.character)) {
            CHECK(model.FeedFood(born,100,FoodKind::Fish)==FeedResult::TooSoon);
            CHECK(Serialize(state)==before && !model.View(100).animation.fishMeal);
            CHECK(model.Feed(born,100,false)==FeedResult::Accepted);
            CHECK(!model.View(100).animation.fishMeal && state.pet.feedCount==1);
            continue;
        }
        CHECK(model.FeedFood(born,100,FoodKind::Fish)==FeedResult::Accepted);
        CHECK(model.Busy() && model.View(100).motion==cat::animation::Motion::Eat);
        CHECK(model.View(100).animation.fishMeal);
        CHECK(state.pet.feedCount==1 && state.memory.meals==1);
        CHECK(state.pet.snackCount==0 && state.memory.treats==0 && state.nutrition.lastTreatUtc==0);
        CHECK(state.nutrition.lastMealUtc==born && state.nutrition.fullness==80);
        const auto fed=Serialize(state);
        CHECK(model.Feed(born+1,1100,true)==FeedResult::TooSoon);
        CHECK(state.pet.feedCount==1 && state.pet.snackCount==0 && model.View(1100).animation.fishMeal);
        // Same Eat action kind can be fed again after the shared cooldown:
        // an ordinary meal must clear the transient fish presentation.
        CHECK(model.Feed(born+300,200,false)==FeedResult::Accepted);
        CHECK(!model.View(200).animation.fishMeal && state.pet.feedCount==2 && state.memory.meals==2);
        CHECK(model.FeedFood(born+600,300,FoodKind::Fish,0)==FeedResult::Full);
        CHECK(!model.View(300).animation.fishMeal && state.pet.feedCount==2 && state.memory.meals==2);
        CHECK(Valid(state));
        State restored;std::string error;
        CHECK(Parse(fed,restored,error));
        CHECK(restored.pet.feedCount==1 && restored.memory.meals==1);
        Model restarted;restarted.Bind(restored);restarted.Reset(40,1000,700);
        CHECK(!restarted.View(100).animation.fishMeal); // No presentation in schema1.
    }
    State state=NewState(born);state.nutrition.fullness=state.pet.hunger=45;
    Model model;model.Bind(state);model.Reset(40,1000,700);
    CHECK(model.FeedFood(born,100,FoodKind::Fish)==FeedResult::Accepted);
    auto c=Frame(100,model.Physical().x+60);
    CHECK(!model.FoodOffered(c) && !model.Pet(born,200));
    c.paused=true;Step(model,c,600);CHECK(model.View(c.ticks).animation.fishMeal);
    c.paused=false;Step(model,c,600);
    CHECK(model.View(c.ticks).motion!=cat::animation::Motion::Eat);
    CHECK(!model.View(c.ticks).animation.fishMeal && state.pet.feedCount==1 && state.memory.meals==1);
}
void AcceptedFoodRestartsItsClockAfterLongMenuPauses() {
    using cat::feeding::FoodKind;
    using cat::nutrition::FeedResult;
    using cat::sprite_sequence::Clip;
    for(const auto& kinds:{std::pair{FoodKind::Meal,FoodKind::Fish},std::pair{FoodKind::Treat,FoodKind::Treat}}) {
        State state=NewState(born);state.preferences.character=cat::Character::Cream;
        state.nutrition.fullness=state.pet.hunger=45;
        Model model;model.Bind(state);model.Reset(40,1000,700);
        CHECK(model.FeedFood(born,100,kinds.first,1)==FeedResult::Accepted);
        auto c=Frame(100,model.Physical().x+60);c.primaryVisible=false;
        model.Tick(c);
        // A front-facing meal first plays the shared real turn. Measure the
        // food clock after that entrance, so this fixture still tests the
        // accepted serving across a long menu pause rather than the bridge.
        const auto firstClip=kinds.first==FoodKind::Meal?Clip::Eat:Clip::Treat;
        for(unsigned frame=0;frame<100 && model.View(c.ticks).animation.spriteClip!=firstClip;++frame) {
            c.ticks+=16;model.Tick(c);
        }
        CHECK(model.View(c.ticks).animation.spriteClip==firstClip);
        c.ticks+=3000;model.Tick(c);
        CHECK(model.View(c.ticks).animation.spriteElapsedMs==3000);
        // The real menu freezes the action clock while UTC still advances
        // nutrition. Five minutes later another serving can be accepted.
        c.paused=true;c.ticks+=300000;c.nowUtc=born+300;model.Tick(c);
        CHECK(model.View(c.ticks).animation.spriteElapsedMs==3000);
        CHECK(model.FeedFood(c.nowUtc,c.ticks,kinds.second,1)==FeedResult::Accepted);
        const bool treat=kinds.second==FoodKind::Treat;
        const auto freshClip=treat?Clip::Treat:Clip::Fish;
        c.paused=false;
        for(unsigned frame=0;frame<100 && model.View(c.ticks).animation.spriteClip!=freshClip;++frame) {
            c.ticks+=16;model.Tick(c);
        }
        const auto fresh=model.View(c.ticks);
        CHECK(fresh.motion==(treat?cat::animation::Motion::Treat:cat::animation::Motion::Eat));
        CHECK(fresh.animation.spriteClip==(treat?Clip::Treat:Clip::Fish));
        CHECK(fresh.animation.spriteElapsedMs==0 && fresh.animation.spriteFrame==0);
        CHECK(fresh.animation.fishMeal==!treat);
        CHECK(state.pet.feedCount==(treat?0u:2u) && state.pet.snackCount==(treat?2u:0u));
        CHECK(state.memory.meals==state.pet.feedCount && state.memory.treats==state.pet.snackCount);
        // A rejected repeat is not a new serving or action. Check its clock
        // and transient variant after77ms, where a reset would be observable.
        const auto visible=model.View(c.ticks+77);const auto saved=Serialize(state);
        CHECK(model.FeedFood(c.nowUtc,c.ticks+77,kinds.first,1)==FeedResult::TooSoon);
        const auto rejected=model.View(c.ticks+77);
        CHECK(rejected.animation.spriteElapsedMs==visible.animation.spriteElapsedMs);
        CHECK(rejected.animation.spriteFrame==visible.animation.spriteFrame);
        CHECK(rejected.animation.fishMeal==visible.animation.fishMeal && Serialize(state)==saved);
        if(!treat) {
            c.paused=false;c.ticks+=500;model.Tick(c);
            c.paused=true;c.ticks+=300000;c.nowUtc=born+600;model.Tick(c);
            const auto fullBefore=model.View(c.ticks);const auto fullSave=Serialize(state);
            CHECK(fullBefore.animation.spriteElapsedMs==500 && fullBefore.animation.fishMeal);
            CHECK(model.FeedFood(c.nowUtc,c.ticks,FoodKind::Meal,0)==FeedResult::Full);
            const auto refused=model.View(c.ticks);
            CHECK(refused.animation.spriteElapsedMs==500 && refused.animation.fishMeal);
            CHECK(refused.animation.spriteFrame==fullBefore.animation.spriteFrame && Serialize(state)==fullSave);
        }
        CHECK(Valid(state));
    }
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
    CHECK(m.Pet(c.nowUtc,c.ticks)); // Stable canonical sitting rig during support movement.
    const auto oldX=m.Physical().x;
    // The second cat keeps its own ledge even if the first cat leaves it.
    c.primary.onPlatform=false;c.primaryPlatform={};c.primary.lift=0;
    for(unsigned i=0;i<60;++i) { c.supportPlatform.left+=.5;c.supportPlatform.right+=.5;Step(m,c,1); }
    CHECK(m.Physical().onPlatform); CHECK(m.Physical().x>oldX+20);
    for(int direction:{1,-1}) {
        if(direction<0)for(unsigned i=0;i<60;++i) {c.supportPlatform.left-=.5;c.supportPlatform.right-=.5;Step(m,c,1);}
        // A support's acceleration can first require physical braking and
        // the complete registered side-to-front exit. Keep carrying the cat
        // while waiting, then check its unchanged stable balance pose.
        const auto began=c.ticks;
        while(m.View(c.ticks).motion!=cat::animation::Motion::TailRaise &&
                c.ticks-began<RegisteredGroundTransitionBudget()) {
            c.supportPlatform.left+=direction*.5;c.supportPlatform.right+=direction*.5;Step(m,c,1);
        }
        const auto view=m.View(c.ticks);
        CHECK(view.animation.frame==4 && view.motion==cat::animation::Motion::TailRaise);
        CHECK(view.physical.onPlatform && std::abs(view.physical.supportVelocityX-direction*30)<.1);
        CHECK(view.animation.angle==0 && view.animation.x==0 && view.animation.lift==0);
        CHECK(view.animation.scaleX==1 && view.animation.scaleY==1);
        CHECK(direction*view.animation.joints.headNod<-.01 && std::abs(view.animation.joints.headNod)<=.2);
        CHECK(view.animation.joints.crouch>.01 && view.animation.joints.crouch<=.16);
    }
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
    CHECK(m.TrySocial(Interaction::Follow,c));RequireMoving(m,c,10);
    CHECK(m.Physical().velocityX>10);
    CHECK(m.Feed(c.nowUtc,c.ticks,false)==cat::nutrition::FeedResult::Accepted);
    CHECK(cat::animation::Travels(m.View(c.ticks).motion)); // A seated eating frame cannot slide during braking.
    const auto* walk=cat::sprite_assets::Find(cat::sprite_sequence::Clip::Walk,0,cat::sprite_assets::kCareSprites);
    CHECK(walk);
    const auto stopDeadline=c.ticks+1000+cat::sprite_sequence::Duration(walk->program,walk->program.outro);
    // Finish braking and the complete side-to-front recovery before the
    // self-contained meal movie takes ownership of the stationary cat.
    for(;c.ticks<stopDeadline && m.View(c.ticks).motion!=cat::animation::Motion::Eat;) {
        Step(m,c,1);
        if(std::abs(m.Physical().velocityX)>cat::motion::kStoppedSpeed)
            CHECK(cat::animation::Walking(m.View(c.ticks).motion));
    }
    CHECK(std::abs(m.Physical().velocityX)<cat::motion::kStoppedSpeed);
    CHECK(m.View(c.ticks).motion==cat::animation::Motion::Eat);
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
        m.Tick(c);const auto accepted=m.View(c.ticks);
        CHECK(accepted.motion==(treat?cat::animation::Motion::Treat:cat::animation::Motion::Eat));
        const auto actionStart=c.ticks-accepted.animation.spriteElapsedMs;
        // A menu freezes the complete accepted meal, including its social gate.
        c.paused=true;c.ticks+=10000;c.nowUtc+=10;m.Tick(c);
        c.paused=false;c.ticks+=100;c.nowUtc+=1;
        CHECK(!m.FoodOffered(c));CHECK(!m.Pet(c.nowUtc,c.ticks));CHECK(!m.Play(c.nowUtc,c.ticks));
        const auto clip=treat?cat::sprite_sequence::Clip::Treat:cat::sprite_sequence::Clip::Eat;
        const auto* art=cat::sprite_assets::Find(clip,0,cat::sprite_assets::kCareSprites);
        CHECK(art);c.ticks=actionStart+10000+cat::sprite_sequence::ProgramDuration(art->program);
        c.nowUtc=born+c.ticks/1000;
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
    for(const auto character:{cat::Character::SilverTabby,cat::Character::Cream}) {
    State s=NewState(birthday);s.pet.position=.5;s.preferences.character=character;
    Model m;m.Bind(s);m.Reset(0,1600,700);
    Context c;c.primaryVisible=false;c.elapsedSeconds=1.0/60;
    bool wasGrounded=true,badSliding=false,badFacing=false;
    unsigned takeoffs=0,movingTakeoffs=0;std::uint64_t crouchStarted=0;
    double previousVelocity=0,flightVelocity=0;bool travelling=false;
    // Longer quiet intervals require a longer deterministic day sample.
    for(unsigned frame=1;frame<144000;++frame) {
        c.ticks=frame*17;c.nowUtc=birthday+static_cast<std::int64_t>(c.ticks/1000);m.Tick(c);
        const auto view=m.View(c.ticks);
        const auto physical=m.Physical();
        const double speed=view.physical.velocityX-view.physical.supportVelocityX;
        const bool travellingPreparation=view.travellingJump&&view.animation.spriteTimed&&
            view.animation.spriteClip==cat::sprite_sequence::Clip::Jump&&
            view.animation.spriteOptions.jump==cat::sprite_sequence::JumpPhase::Crouch;
        if(view.physical.grounded && std::abs(speed)>2) {
            badSliding=badSliding || (!travellingPreparation&&
                (!cat::animation::Walking(view.motion) || view.animation.frame<0 || view.animation.frame>3));
            // The authored pivot keeps its original source orientation while
            // the final drawings already face and push in the other direction.
            if(view.animation.spriteClip!=cat::sprite_sequence::Clip::RunningTurn)
                badFacing=badFacing || view.animation.flip!=(speed<0);
        }
        if(view.motion!=M::Jump && view.motion!=M::Pounce)crouchStarted=0;
        if(view.physical.grounded && !travellingPreparation && (view.motion==M::Jump || view.motion==M::Pounce) && view.animation.frame==4) {
            if(!crouchStarted)crouchStarted=c.ticks;
            CHECK(std::abs(speed)<=2);
            CHECK(view.animation.scaleX==1 && view.animation.scaleY==1 && view.animation.angle==0);
        }
        if(wasGrounded && !physical.grounded) {
            ++takeoffs;
            const auto* jump=cat::sprite_assets::Find(cat::sprite_sequence::Clip::Jump,
                static_cast<unsigned>(character),cat::sprite_assets::kCareSprites);
            const auto preparation=jump && jump->program.stepCount?
                cat::sprite_sequence::Duration(jump->program,jump->program.intro):
                (view.motion==M::Jump?220U:240U);
            if(view.travellingJump) {
                ++movingTakeoffs;travelling=true;flightVelocity=physical.velocityX;
                CHECK(std::abs(flightVelocity)>=8);
                CHECK(std::abs(flightVelocity-previousVelocity)<1);
            } else CHECK(crouchStarted>0 && c.ticks-crouchStarted+34>=preparation);
            CHECK(!view.animation.groundContact);
            crouchStarted=0;
        }
        if(travelling && !physical.grounded)CHECK(std::abs(physical.velocityX-flightVelocity)<1);
        if(physical.landed)travelling=false;
        previousVelocity=physical.velocityX;
        wasGrounded=physical.grounded;
    }
    CHECK(takeoffs>0);CHECK(movingTakeoffs>0);CHECK(!badSliding);CHECK(!badFacing);CHECK(Valid(s));
    }
}
void SocialGesturesWaitForPlantedFeet() {
    using M=cat::animation::Motion;
    for(const auto kind:{Interaction::Sniff,Interaction::Paw,Interaction::SharedSleep,Interaction::FoodCompetition}) {
        State s=NewState(born);s.pet.position=.2;s.nutrition.fullness=60;s.pet.hunger=60;
        Model m;m.Bind(s);m.Reset(0,1600,700);
        auto c=Frame(100,m.Physical().x+200);c.primaryMotion=M::Zoomies;
        CHECK(m.TrySocial(Interaction::RunTogether,c));RequireMoving(m,c,40);CHECK(m.Physical().velocityX>40);
        c.primary.x=m.Physical().x+(kind==Interaction::Paw?48:65);
        c.primaryMotion=kind==Interaction::SharedSleep?M::Sleep:kind==Interaction::FoodCompetition?M::Eat:M::Idle;
        CHECK(m.TrySocial(kind,c));m.Tick(c);
        CHECK(m.TakeEvent()==Interaction::None);CHECK(s.sniffs+s.mutualPlay+s.sharedSleeps+s.foodRaces==0);
        CHECK(cat::animation::Travels(m.View(c.ticks).motion));
        bool contacted=false;
        for(unsigned frame=0;frame<240;++frame) {
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
    CHECK(m.TrySocial(Interaction::RunTogether,c));RequireMoving(m,c,40);CHECK(m.Physical().velocityX>40);
    const auto acceptedAt=c.ticks;
    CHECK(m.Feed(c.nowUtc,c.ticks,false)==cat::nutrition::FeedResult::Accepted);
    c.primaryVisible=false;
    const auto* meal=cat::sprite_assets::Find(cat::sprite_sequence::Clip::Eat,0,cat::sprite_assets::kCareSprites);
    CHECK(meal);
    while(c.ticks<acceptedAt+cat::sprite_sequence::ProgramDuration(meal->program)+50)Step(m,c,1);
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
        const auto animation=m.View(c.ticks).animation;
        CHECK(animation.angle==0 && animation.x==0 && animation.lift==0);
        CHECK(animation.scaleX==1 && animation.scaleY==1);
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
void HighRefreshGaitFollowsRenderedTravel() {
    // Box2D steps at60Hz while the layered window may render at120Hz. Both
    // root displacement and footfalls must use the same interpolated sample.
    State s=NewState(born);s.pet.position=.2;
    Model m;m.Bind(s);m.Reset(0,2400,700);
    auto c=Frame(100,m.Physical().x+60);c.elapsedSeconds=1.0/120;
    c.primaryMotion=cat::animation::Motion::Zoomies;c.primary.velocityX=90;
    CHECK(m.TrySocial(Interaction::RunTogether,c));
    cat::animation::Locomotion expected;
    double priorTravel=m.View(c.ticks).physical.relativeTravelX;
    unsigned renderedBetweenSteps=0;
    for(unsigned frame=1;frame<=600;++frame) {
        const double priorPhysicalX=m.Physical().x;
        c.ticks=100+frame*1000/120;c.nowUtc=born+static_cast<std::int64_t>(c.ticks/1000);
        c.primary.x=m.Physical().x+60;m.Tick(c);
        const auto view=m.View(c.ticks);
        const double stride=view.physical.relativeTravelX-priorTravel;
        expected.Advance(view.motion,stride/view.growthScale,c.elapsedSeconds);
        priorTravel=view.physical.relativeTravelX;
        if(frame>120) {
            CHECK(view.motion==cat::animation::Motion::Zoomies);
            const auto pose=cat::animation::Evaluate(view.motion,0,c.ticks,1,expected);
            CHECK(view.animation.frame==pose.frame);
            CHECK(view.animation.blendFrame==pose.blendFrame);
            CHECK(std::abs(view.animation.blend-pose.blend)<1e-10);
            CHECK(std::abs(view.animation.joints.breath-pose.joints.breath)<1e-10);
            if(m.Physical().x==priorPhysicalX && stride>.01)++renderedBetweenSteps;
        }
    }
    CHECK(renderedBetweenSteps>100);CHECK(Valid(s));
}

void AuthoredCompanionWalkRunUsesItsRealRegisteredTravel() {
    using Clip=cat::sprite_sequence::Clip;
    const auto* walk=cat::sprite_assets::Find(Clip::Walk,0,cat::sprite_assets::kCareSprites);
    const auto* run=cat::sprite_assets::Find(Clip::Run,0,cat::sprite_assets::kCareSprites);
    CHECK(walk && run && walk->stridePixels>0 &&
        cat::sprite_assets::NominalLocomotionSpeed(*run)>cat::sprite_assets::NominalLocomotionSpeed(*walk));
    const double videoPace=cat::sprite_assets::NominalLocomotionSpeed(*walk);
    const double normalPace=videoPace>0?videoPace:22;
    const double runningPace=std::max(150.0,normalPace*1.8);
    for(const auto character:{cat::Character::Cream,cat::Character::SilverTabby})
        for(const double pace:{12.,normalPace,runningPace}) {
            State state=NewState(born);state.preferences.character=character;state.pet.position=.2;
            Model model;model.Bind(state);model.Reset(0,10000,700);
            auto c=Frame(100,model.Physical().x+60);c.elapsedSeconds=1./120;
            c.primaryMotion=cat::animation::Motion::Zoomies;c.primary.velocityX=pace;
            CHECK(model.TrySocial(Interaction::RunTogether,c));
            double previousTravel=model.View(c.ticks).physical.relativeTravelX,expectedPhase=0;
            unsigned authored=0,entrance=0;bool loopStarted=false;
            for(unsigned frame=1;frame<=300;++frame) {
                c.ticks=100+frame*1000/120;c.nowUtc=born+static_cast<std::int64_t>(c.ticks/1000);
                c.primary.x=model.Physical().x+60;model.Tick(c);
                const auto view=model.View(c.ticks);
                const double travel=view.physical.relativeTravelX-previousTravel;
                previousTravel=view.physical.relativeTravelX;
                if(character==cat::Character::Cream) {
                    CHECK(view.animation.spriteClip==Clip::Walk || view.animation.spriteClip==Clip::Run);
                    const auto* layout=view.animation.spriteClip==Clip::Run?run:walk;
                    if(!view.animation.spriteTimed) {
                        CHECK(!loopStarted && layout==walk);
                        const auto first=walk->program.intro.first,last=first+walk->program.intro.count;
                        CHECK(std::any_of(walk->program.steps.begin()+first,walk->program.steps.begin()+last,
                            [&](const auto& step){return step.frame==view.animation.spriteFrame;}));
                        ++entrance;
                    } else {
                        // The authored first steps finish before the real
                        // distance-driven cycle is seeded at its seam.
                        if(!loopStarted){expectedPhase=walk->frameCount==229?cat::video_actions::WalkBoundaryPhase(static_cast<unsigned>(character)):0;loopStarted=true;}
                        else expectedPhase=std::fmod(expectedPhase+std::abs(travel)/
                            (view.growthScale*layout->stridePixels*view.body.bodyWidth),1.0);
                        const double sourcePhase=layout==run?cat::video_actions::RunSourcePhase(static_cast<unsigned>(character),expectedPhase):expectedPhase;
                        CHECK(std::abs(view.animation.spriteOptions.cyclePhase-sourcePhase)<1e-10);
                    }
                    CHECK(!view.animation.originalWalk && !view.animation.jointWalk && !view.animation.walkingRig);
                    CHECK(view.animation.x==0 && view.animation.lift==0 && view.animation.angle==0);
                    CHECK(view.animation.scaleX==1 && view.animation.scaleY==1 && view.animation.blendFrame==-1);
                    // Native video cadence is still walking. A distinctly
                    // faster shared sprint selects the separate run cycle.
                    // The 1.5 s planted entrance finishes before the motor
                    // accelerates through the separate walk/run threshold.
                    if(loopStarted && frame>240)CHECK(view.animation.spriteClip==(pace==runningPace?Clip::Run:Clip::Walk));
                    ++authored;
                } else {
                    CHECK(view.animation.spriteClip==Clip::None && view.animation.originalWalk);
                    CHECK(view.animation.frame>=0 && view.animation.frame<4);
                }
            }
            if(character==cat::Character::Cream) {
                CHECK(authored==300);
                CHECK(loopStarted && (!videoPace || entrance>0));
                const auto before=model.View(c.ticks);
                c.paused=true;c.ticks+=60000;c.nowUtc+=60;model.Tick(c);
                const auto paused=model.View(c.ticks);
                CHECK(paused.animation.spriteClip==before.animation.spriteClip);
                CHECK(paused.animation.spriteOptions.cyclePhase==before.animation.spriteOptions.cyclePhase);
                CHECK(paused.physical.x==before.physical.x);
            }
            CHECK(Valid(state));
        }
}

void OptionalWalkReviewKeepsBothCoatsOnTheRegisteredStride() {
    using Clip=cat::sprite_sequence::Clip;
    const auto* approved=cat::sprite_assets::Find(Clip::Walk,0,cat::sprite_assets::kCareSprites);
    CHECK(approved);
    auto authored=*approved;authored.stridePixels=30;
    CHECK(cat::sprite_assets::Valid(authored));
    for(auto character:{cat::Character::Cream,cat::Character::Tuxedo}) {
        State state=NewState(born);state.preferences.character=character;state.pet.position=.2;
        Model model;model.SetWalkRegistration(&authored);model.Bind(state);model.Reset(0,10000,700);
        auto c=Frame(100,model.Physical().x+60);c.elapsedSeconds=1./120;
        c.primaryMotion=cat::animation::Motion::Zoomies;c.primary.velocityX=12;
        CHECK(model.TrySocial(Interaction::RunTogether,c));
        double previousTravel=model.View(c.ticks).physical.relativeTravelX,expectedPhase=0;
        bool loopStarted=false;
        for(unsigned frame=1;frame<=300;++frame) {
            c.ticks=100+frame*1000/120;c.nowUtc=born+static_cast<std::int64_t>(c.ticks/1000);
            c.primary.x=model.Physical().x+60;model.Tick(c);
            const auto view=model.View(c.ticks);
            const double travel=view.physical.relativeTravelX-previousTravel;
            previousTravel=view.physical.relativeTravelX;
            CHECK(view.animation.spriteClip==Clip::Walk);
            if(view.animation.spriteTimed) {
                if(!loopStarted){expectedPhase=authored.frameCount==229?cat::video_actions::WalkBoundaryPhase(static_cast<unsigned>(character)):0;loopStarted=true;}
                else expectedPhase=std::fmod(expectedPhase+std::abs(travel)/
                    (view.growthScale*authored.stridePixels*view.body.bodyWidth),1.);
                CHECK(std::abs(view.animation.spriteOptions.cyclePhase-expectedPhase)<1e-10);
            } else CHECK(!loopStarted);
        }
        CHECK(loopStarted);
        const double x=model.Physical().x;
        model.SetWalkRegistration(nullptr);CHECK(model.Physical().x==x);
        c.ticks+=9;c.primary.x=model.Physical().x+60;model.Tick(c);
        const auto after=model.View(c.ticks);
        expectedPhase=std::fmod(expectedPhase+std::abs(after.physical.relativeTravelX-previousTravel)/
            (after.growthScale*approved->stridePixels*after.body.bodyWidth),1.);
        CHECK(std::abs(after.animation.spriteOptions.cyclePhase-expectedPhase)<1e-10);
    }
}

void RegisteredCompanionCareFinishesItsWholeProgram() {
    using Clip=cat::sprite_sequence::Clip;
    using Food=cat::feeding::FoodKind;
    for(const auto character:{cat::Character::Cream,cat::Character::Tuxedo})
    for(const auto food:{Food::Meal,Food::Treat,Food::Fish}) {
        State state=NewState(born);state.preferences.character=character;
        state.nutrition.fullness=state.pet.hunger=45;
        Model model;model.Bind(state);model.Reset(0,1600,700);
        auto c=Frame(100,model.Physical().x+60);c.primaryVisible=false;
        const auto clip=food==Food::Treat?Clip::Treat:food==Food::Fish?Clip::Fish:Clip::Eat;
        const auto* asset=cat::sprite_assets::Find(clip,static_cast<unsigned>(character),cat::sprite_assets::kCareSprites);
        CHECK(asset && asset->program.stepCount);
        const auto duration=cat::sprite_sequence::ProgramDuration(asset->program);
        const auto intro=cat::sprite_sequence::Duration(asset->program,asset->program.intro);
        const auto loop=cat::sprite_sequence::Duration(asset->program,asset->program.loop);
        const auto outro=cat::sprite_sequence::Duration(asset->program,asset->program.outro);
        // Whole-program completion follows each action's registered length,
        // not a former five-second minimum. A default cyclic meal must still
        // show its complete entrance, at least one bite/lick, and its ending.
        CHECK(duration>0 && duration>=intro+outro);
        CHECK(!loop || duration-intro-outro>=loop);
        CHECK(model.FeedFood(born,c.ticks,food,1)==cat::nutrition::FeedResult::Accepted);
        model.Tick(c);
        RequireCareStarts(model,c,clip);
        const auto start=c.ticks;
        const auto end=c.ticks+duration;
        std::set<unsigned> introSeen,outroSeen;
        bool sawLoop=false;
        while(c.ticks+17<end) {
            const auto view=model.View(c.ticks);
            CHECK(view.animation.spriteClip==clip && view.animation.spriteOptions.durationMs==duration);
            CHECK(view.motion==(food==Food::Treat?cat::animation::Motion::Treat:cat::animation::Motion::Eat));
            CHECK(view.animation.spriteElapsedMs==c.ticks-start);
            auto options=view.animation.spriteOptions;options.sheetFrames=asset->frameCount;
            const auto source=cat::sprite_sequence::SampleProgram(clip,asset->program,
                view.animation.spriteElapsedMs,options);
            CHECK(!source.finished && source.step<asset->program.stepCount);
            if(source.step<asset->program.loop.first)introSeen.insert(source.step);
            else if(source.step<asset->program.outro.first)sawLoop=true;
            else outroSeen.insert(source.step);
            Step(model,c,1);
        }
        CHECK(introSeen.size()==asset->program.intro.count);
        CHECK(sawLoop==bool(asset->program.loop.count));
        CHECK(outroSeen.size()==asset->program.outro.count);
        const auto last=model.View(end-1);
        CHECK(last.animation.spriteClip==clip && last.animation.spriteElapsedMs==duration-1);
        auto options=last.animation.spriteOptions;options.sheetFrames=asset->frameCount;
        const auto source=cat::sprite_sequence::SampleProgram(clip,asset->program,last.animation.spriteElapsedMs,options);
        CHECK(source.frame==asset->program.steps[asset->program.stepCount-1].frame);
        c.ticks=end;c.nowUtc=born+static_cast<std::int64_t>(end/1000);model.Tick(c);
        RequireCareStarts(model,c,Clip::Groom);
        const auto grooming=model.View(c.ticks);
        CHECK(grooming.motion==cat::animation::Motion::Groom);
        const auto* groom=cat::sprite_assets::Find(Clip::Groom,0,cat::sprite_assets::kCareSprites);
        const auto groomDuration=cat::sprite_sequence::ProgramDuration(groom->program);
        CHECK(grooming.animation.spriteClip==Clip::Groom && grooming.animation.spriteTimed);
        c.ticks+=groomDuration;c.nowUtc=born+static_cast<std::int64_t>(c.ticks/1000);model.Tick(c);
        const auto neutral=model.View(c.ticks);
        CHECK(neutral.motion==cat::animation::Motion::Idle);
        CHECK(neutral.animation.spriteClip==Clip::None&&neutral.animation.frame==4&&!neutral.animation.spriteTimed);
        CHECK(cat::deform::Zero(neutral.animation.joints)&&neutral.animation.scaleX==1&&neutral.animation.scaleY==1);
        CHECK(Valid(state));
    }
}

void ContactRecoveryFinishesBeforeAcceptedFishStarts() {
    using Clip=cat::sprite_sequence::Clip;
    const auto* jump=cat::sprite_assets::Find(Clip::Jump,0,cat::sprite_assets::kCareSprites);
    CHECK(jump && jump->program.stepCount);
    const auto recovery=cat::sprite_sequence::Duration(jump->program,jump->program.outro);
    CHECK(recovery>280);
    State state=NewState(born);state.preferences.character=cat::Character::Cream;state.pet.position=.2;
    state.nutrition.fullness=state.pet.hunger=45;
    Model model;model.Bind(state);model.Reset(0,2400,700);
    auto c=Frame(100,700,120);c.primary.onPlatform=true;
    c.primaryPlatform={912,400,2100,120,true};c.supportPlatform=c.primaryPlatform;
    CHECK(model.TrySocial(Interaction::WindowFollow,c));
    bool foundRecovery=false;
    for(unsigned frame=0;frame<300;++frame) {
        Step(model,c,1);const auto view=model.View(c.ticks);
        if(view.animation.spriteClip==Clip::Jump && view.animation.spriteOptions.grounded &&
                view.animation.spriteOptions.jump==cat::sprite_sequence::JumpPhase::Landing &&
                view.animation.spriteOptions.contactElapsedMs>300) {
            CHECK(view.animation.spriteOptions.contactElapsedMs<recovery);
            foundRecovery=true;break;
        }
    }
    CHECK(foundRecovery && model.Physical().grounded && model.Physical().onPlatform);
    CHECK(model.FeedFood(c.nowUtc,c.ticks,cat::feeding::FoodKind::Fish,1)==cat::nutrition::FeedResult::Accepted);
    auto landing=model.View(c.ticks);
    CHECK(landing.animation.spriteClip==Clip::Jump && !landing.animation.fishMeal);
    for(unsigned frame=0;frame<100 && model.View(c.ticks).animation.spriteClip==Clip::Jump;++frame)Step(model,c,1);
    const auto meal=model.View(c.ticks);
    CHECK(meal.animation.spriteClip==Clip::Fish && meal.animation.fishMeal);
    CHECK(meal.animation.spriteElapsedMs<=34);
    CHECK(state.pet.feedCount==1 && state.memory.meals==1 && Valid(state));
}

void CompanionJumpReturnOwnsTheCompletedRecoveryBoundary() {
    using Clip=cat::sprite_sequence::Clip;
    const auto* jump=cat::sprite_assets::Find(Clip::Jump,0,cat::sprite_assets::kCareSprites);
    const auto* walk=cat::sprite_assets::Find(Clip::Walk,0,cat::sprite_assets::kCareSprites);
    CHECK(jump&&walk);
    const auto profile=cat::video_ground_bridge::JumpTurns(*walk);CHECK(profile.valid);
    const auto recovery=cat::sprite_sequence::Duration(jump->program,jump->program.outro);
    const bool canonical=std::string_view(walk->sourcePath).find("assets/video-actions/")==0;
    const auto turn=canonical?cat::sprite_sequence::Duration(walk->program,walk->program.outro):profile.entryMs;
    const unsigned first=canonical?walk->program.steps[walk->program.outro.first].frame:20;
    const unsigned last=canonical?walk->program.steps[walk->program.outro.first+walk->program.outro.count-1].frame:29;
    for(const auto character:{cat::Character::Cream,cat::Character::Tuxedo}) {
        State state=NewState(born);state.preferences.character=character;state.pet.position=.2;
        Model model;model.Bind(state);model.Reset(0,2400,700);
        auto c=Frame(100,700,120);c.primary.onPlatform=true;
        c.primaryPlatform={912,400,2100,120,true};c.supportPlatform=c.primaryPlatform;
        CHECK(model.TrySocial(Interaction::WindowFollow,c));
        std::uint64_t landed=0,returning=0;
        for(unsigned frame=0;frame<400;++frame) {
            Step(model,c,1);const auto physical=model.Physical();const auto view=model.View(c.ticks);
            if(physical.landed)landed=c.ticks;
            if(landed && c.ticks-landed>=recovery && view.animation.spriteClip==Clip::Walk &&
                    view.animation.spriteFrame>=first && view.animation.spriteFrame<=last) {
                returning=c.ticks;break;
            }
        }
        CHECK(landed&&returning&&returning-landed>=recovery&&returning-landed<=recovery+34);
        const auto origin=model.View(c.ticks).physical;
        while(c.ticks-returning<turn+34) {
            const auto view=model.View(c.ticks);
            CHECK(view.physical.grounded&&view.physical.onPlatform);
            CHECK(std::abs(view.physical.x-origin.x)<.01&&std::abs(view.physical.lift-origin.lift)<.01);
            // The old waitingForStop recovery extension must not resurrect
            // Jump or reset this just-started real side-to-front turn.
            if(c.ticks-returning<turn)
                CHECK(view.animation.spriteClip==Clip::Walk&&view.animation.spriteFrame>=first&&view.animation.spriteFrame<=last);
            else CHECK(view.animation.spriteClip==Clip::None&&view.animation.frame==4);
            Step(model,c,1);
        }
        const auto settled=model.View(c.ticks);
        CHECK(settled.motion==cat::animation::Motion::Idle&&settled.animation.spriteClip==Clip::None&&settled.animation.frame==4);
        CHECK(cat::deform::Zero(settled.animation.joints)&&settled.animation.scaleX==1&&settled.animation.scaleY==1);
        CHECK(state.pet.feedCount==0&&state.pet.snackCount==0&&Valid(state));
    }
}

void ExplicitCompanionJumpReturnsWhenTheRecoveryFinishes() {
    using Clip=cat::sprite_sequence::Clip;
    const auto* jump=cat::sprite_assets::Find(Clip::Jump,0,cat::sprite_assets::kCareSprites);
    CHECK(jump);
    const auto recovery=cat::sprite_sequence::Duration(jump->program,jump->program.outro);
    for(const auto character:{cat::Character::Cream,cat::Character::Tuxedo,cat::Character::OddEye})for(bool paused:{false,true}) {
        State state=NewState(born);state.preferences.character=character;state.pet.position=.2;state.pet.roamingPaused=paused;
        Model model;model.Bind(state);model.Reset(0,2400,700);
        auto c=Frame(100,1700);CHECK(model.Perform(Clip::Jump,c.ticks));
        std::uint64_t landed=0,returned=0;double previousProgress=-1;
        for(unsigned frame=0;frame<550;++frame) {
            Step(model,c,1);const auto physical=model.Physical();const auto view=model.View(c.ticks);
            if(!physical.grounded&&view.animation.spriteClip==Clip::Jump) {
                const auto progress=view.animation.spriteOptions.flightProgress;
                CHECK(progress>=0&&progress<=1&&progress>=previousProgress);previousProgress=progress;
            }
            if(physical.landed&&!landed)landed=c.ticks;
            if(landed&&c.ticks-landed<recovery)CHECK(view.animation.spriteClip==Clip::Jump);
            if(landed&&c.ticks-landed>=recovery&&view.animation.spriteClip==Clip::Walk){returned=c.ticks;break;}
        }
        CHECK(landed&&returned&&returned-landed>=recovery&&returned-landed<=recovery+34);
        CHECK(previousProgress>.9&&state.pet.roamingPaused==paused&&state.pet.feedCount==0&&state.pet.snackCount==0);
    }
}

void AuthoredSleepKeepsItsIdentityIncludingWindowRestAndWake() {
    using Clip=cat::sprite_sequence::Clip;
    const auto* sleep=cat::sprite_assets::Find(Clip::Sleep,0,cat::sprite_assets::kCareSprites);
    CHECK(sleep);
    const auto intro=cat::animation::ReferenceSleepIntroMs(&sleep->program,sleep->frameCount);
    for(unsigned seed=0;seed<20;++seed) {
        State state=NewState(born+seed);state.preferences.character=cat::Character::Cream;
        Model model;model.Bind(state);model.Reset(0,1600,700);
        auto c=Frame(100,model.Physical().x+65);c.nowUtc=born+seed;
        c.primaryMotion=cat::animation::Motion::Sleep;
        CHECK(model.TrySocial(Interaction::SharedSleep,c));model.Tick(c);
        for(unsigned seconds:{0u,2u,5u,15u}) {
            c.ticks=100+seconds*1000;c.nowUtc=born+seed+seconds;model.Tick(c);
            const auto view=model.View(c.ticks);
            CHECK(view.motion==cat::animation::Motion::Sleep);
            if(seconds*1000<intro)CHECK(view.animation.spriteClip==Clip::Sleep&&view.animation.spriteTimed);
            else CHECK(view.animation.spriteClip==Clip::None&&!view.animation.spriteTimed&&view.animation.frame==6);
        }
    }
    State state=NewState(born);state.preferences.character=cat::Character::Cream;state.pet.position=.2;
    Model model;model.Bind(state);model.Reset(0,2400,700);
    auto c=Frame(100,700,120);c.primary.onPlatform=true;
    c.primaryPlatform={912,400,2100,120,true};c.supportPlatform=c.primaryPlatform;
    CHECK(model.TrySocial(Interaction::WindowFollow,c));
    for(unsigned frame=0;frame<240 && !model.Physical().onPlatform;++frame)Step(model,c,1);
    CHECK(model.Physical().onPlatform);
    const auto* jump=cat::sprite_assets::Find(Clip::Jump,0,cat::sprite_assets::kCareSprites);
    CHECK(jump && jump->program.stepCount);
    const auto recovery=cat::sprite_sequence::Duration(jump->program,jump->program.outro);
    Step(model,c,static_cast<unsigned>((recovery+16)/17+1));
    c.primary.x=model.Physical().x+65;c.primaryMotion=cat::animation::Motion::Sleep;
    CHECK(model.TrySocial(Interaction::SharedSleep,c));model.Tick(c);
    RequireCareStarts(model,c,Clip::Sleep); // Finish an in-progress, contact-owned jump return before the nap clock begins.
    CHECK(model.View(c.ticks).physical.onPlatform);
    CHECK(model.View(c.ticks).animation.spriteClip==Clip::Sleep);
    const auto started=c.ticks;
    while(c.ticks-started<30000)Step(model,c,1);
    const auto waking=model.View(c.ticks);
    CHECK(waking.motion==cat::animation::Motion::Stretch && waking.physical.onPlatform);
    CHECK(waking.animation.spriteClip==Clip::Sleep);
    CHECK(waking.animation.spriteOptions.sleep==cat::sprite_sequence::SleepStage::Waking);
    CHECK(Valid(state));
}
}
void CompanionPointerAndAuthoredActions() {
    using C=cat::sprite_sequence::Clip;using A=cat::environment::CursorAction;
    for(const auto coat:cat::kAvailableCharacters) {
        State s=NewState(born-365*86400);s.preferences.character=coat;s.pet.position=.3;
        Model model;model.Bind(s);model.Reset(0,1600,700);
        Context c;c.nowUtc=born;c.ticks=1000;c.elapsedSeconds=.016;c.primaryVisible=false;
        c.cursorEnabled=c.cursorAvailable=true;
        const double root=model.Physical().x;unsigned swats=0,turns=0;
        for(unsigned frame=0;frame<1250;++frame) {
            c.ticks+=16;c.cursorX=root+48*std::sin(frame*.016*6*cat::animation::kPi);c.cursorWorldHeight=65;
            model.Tick(c);const auto view=model.View(c.ticks);
            swats+=view.animation.spriteClip==C::Punch;turns+=view.animation.spriteClip==C::RunningTurn;
            CHECK(std::abs(view.physical.x-root)<.05);CHECK(view.physical.grounded);
        }
        CHECK(swats>900);CHECK(turns==0);
        // An independent low pointer crosses the actual source swat arc;
        // the higher stress pattern above deliberately may miss the paw.
        for(unsigned frame=0;frame<1250;++frame) {
            c.ticks+=16;c.cursorX=root+35+8*std::sin(frame*.016*6*cat::animation::kPi);c.cursorWorldHeight=35;
            model.Tick(c);
        }
        CHECK(model.CursorContacts()>0);
        c.cursorX=1250;c.cursorWorldHeight=95;bool chased=false,launched=false;
        for(unsigned frame=0;frame<750;++frame) {
            c.ticks+=16;model.Tick(c);
            chased=chased||model.CursorAction()==A::Chase;
            launched=launched||!model.Physical().grounded;
        }
        CHECK(chased);CHECK(model.Physical().x>root+250);CHECK(launched);
        c.cursorEnabled=c.cursorAvailable=false;
        for(unsigned frame=0;frame<400;++frame){c.ticks+=16;model.Tick(c);}
        for(const auto clip:{C::FlyHunt,C::ToyPlay,C::BallPlay,C::Box,C::Groom,C::Knead,C::Punch}) {
            CHECK(model.Perform(clip,c.ticks));
            bool seen=false;unsigned last=0;bool progressed=false;double ballSpeed=0;
            const auto* art=cat::sprite_assets::Find(clip,static_cast<unsigned>(coat),cat::sprite_assets::kCareSprites);
            CHECK(art);const auto duration=cat::sprite_sequence::ProgramDuration(art->program);
            for(unsigned frame=0;frame<(duration+2500)/16;++frame) {
                c.ticks+=16;model.Tick(c);const auto view=model.View(c.ticks);const auto pose=view.animation;
                if(view.ballVisible)ballSpeed=std::max(ballSpeed,std::abs(view.ball.velocity));
                if(pose.spriteClip==clip) {
                    auto options=pose.spriteOptions;options.sheetFrames=art->frameCount;
                    const auto source=pose.spriteTimed?cat::sprite_sequence::SampleProgram(clip,art->program,pose.spriteElapsedMs,options).frame:pose.spriteFrame;
                    CHECK(source<art->frameCount);progressed=progressed||source>last;last=source;seen=true;
                }
            }
            CHECK(seen&&progressed);CHECK(s.pet.feedCount==0&&s.memory.meals==0);
            if(clip==C::BallPlay)CHECK(ballSpeed>10);
        }
        CHECK(Valid(s));
    }
}
void CompanionKeyboardAndNinePairings() {
    using C=cat::sprite_sequence::Clip;
    for(const auto coat:cat::kAvailableCharacters) {
        State state=NewState(born-366*86400);state.preferences.character=coat;state.pet.position=.3;
        Model model;model.Bind(state);model.Reset(0,3300,700);
        Context c;c.nowUtc=born;c.ticks=1000;c.elapsedSeconds=.016;c.primaryVisible=false;
        CHECK(model.BeginKeyboard(c.ticks));int direction=1;bool run=false,air=false;unsigned turns=0;
        for(unsigned frame=0;frame<1250;++frame) {
            if(frame==188||frame==414||frame==850)run=true;
            if(frame==397||frame==650)run=false;
            if(frame==313||frame==700)direction=-1;
            if(frame==450||frame==650)direction=0;
            if(frame==470||frame==1000)direction=1;
            model.KeyboardDrive(direction,run,frame==1030);c.ticks+=16;model.Tick(c);
            const auto view=model.View(c.ticks);air=air||!view.physical.grounded;turns+=view.animation.spriteClip==C::RunningTurn;
        }
        CHECK(air);CHECK(turns>40);model.EndKeyboard(c.ticks);CHECK(Valid(state));
        model.DragTo(3290);CHECK(model.Perform(C::Climb,c.ticks));double climb=0;bool film=false;
        for(unsigned frame=0;frame<1500;++frame) {
            c.ticks+=16;model.Tick(c);const auto view=model.View(c.ticks);
            climb=std::max(climb,view.physical.lift);film=film||view.animation.spriteClip==C::Climb;
        }
        CHECK(film&&climb>30);CHECK(model.Physical().grounded);
        CHECK(model.Perform(C::Sleep,c.ticks));bool woke=false;
        const auto* sleep=cat::sprite_assets::Find(C::Sleep,static_cast<unsigned>(coat),cat::sprite_assets::kCareSprites);
        const auto sleepDuration=cat::sprite_sequence::ProgramDuration(sleep->program);
        for(unsigned frame=0;frame<(sleepDuration+6000)/16;++frame) {
            c.ticks+=16;model.Tick(c);const auto view=model.View(c.ticks);
            if(view.motion==cat::animation::Motion::Stretch) {
                CHECK(view.animation.spriteClip==C::Sleep);
                CHECK(view.animation.spriteOptions.sleep==cat::sprite_sequence::SleepStage::Waking);woke=true;
            }
        }
        CHECK(woke);
    }
    for(const auto first:cat::kAvailableCharacters)for(const auto second:cat::kAvailableCharacters) {
        State a=NewState(born-366*86400),b=a;a.preferences.character=first;b.preferences.character=second;
        a.pet.position=.5;b.pet.position=.418;Model primary,companion;
        primary.Bind(a);companion.Bind(b);primary.Reset(0,1000,700);companion.Reset(0,1000,700);
        Context c;c.nowUtc=born;c.ticks=1000;c.elapsedSeconds=.016;c.primary=primary.Physical();
        Context p=c;p.primaryVisible=false;CHECK(companion.TrySocial(Interaction::Paw,c));
        unsigned events=0,completeFirst=0,completeSecond=0;
        for(unsigned frame=0;frame<1100;++frame) {
            c.ticks+=16;p.ticks=c.ticks;c.primary=primary.Physical();companion.Tick(c);
            if(companion.TakeEvent()==Interaction::Paw){++events;CHECK(primary.Perform(C::Punch,c.ticks));}
            primary.Tick(p);
            for(const auto which:{0,1}) {
                const auto pose=(which?companion:primary).View(c.ticks).animation;
                const auto* art=cat::sprite_assets::Find(C::Punch,static_cast<unsigned>(which?second:first),cat::sprite_assets::kCareSprites);
                if(pose.spriteClip==C::Punch) {
                    auto options=pose.spriteOptions;options.sheetFrames=art->frameCount;
                    const auto source=pose.spriteTimed?cat::sprite_sequence::SampleProgram(C::Punch,art->program,pose.spriteElapsedMs,options).frame:pose.spriteFrame;
                    if(source==art->frameCount-1){if(which)++completeSecond;else ++completeFirst;}
                }
            }
        }
        CHECK(events==2);CHECK(b.mutualPlay==1);CHECK(completeFirst>0&&completeSecond>0);
        CHECK(a.pet.feedCount==0&&b.pet.feedCount==0&&a.memory.plays==0&&b.memory.plays==0);
        CHECK(!companion.SocialActive());
    }
}
void ManualRequestsWaitForTheCompleteWake() {
    using C=cat::sprite_sequence::Clip;using M=cat::animation::Motion;
    for(auto coat:cat::kAvailableCharacters) {
        State state=NewState(born);state.preferences.character=coat;state.pet.roamingPaused=true;
        Model model;model.Bind(state);model.Reset(0,2400,700);auto c=Frame(100,1600);
        CHECK(model.Perform(C::Sleep,c.ticks));Step(model,c,300);
        const auto x=model.Physical().x;
        CHECK(model.Perform(C::Walk,c.ticks));
        auto view=model.View(c.ticks);
        CHECK(view.motion==M::Stretch&&view.animation.spriteClip==C::Sleep&&
            view.animation.spriteOptions.sleep==cat::sprite_sequence::SleepStage::Waking);
        unsigned frames=0;
        while(model.View(c.ticks).motion==M::Stretch&&frames++<400) {
            CHECK(std::abs(model.Physical().x-x)<.5);Step(model,c,1);
        }
        CHECK(model.View(c.ticks).motion==M::Walk);
        CHECK(state.pet.bornUtc==born&&state.pet.feedCount==0&&state.pet.snackCount==0);
    }
}
int main() {
    try { SeparateAdoptionAndRestart();IndividualStableTemperaments();StrictBundleParsing();RecoveryAndFaults();IndependentCareAndAge();FishCareKeepsTheExistingMealAndBusyContracts();AcceptedFoodRestartsItsClockAfterLongMenuPauses();SocialContactAndCompetition();RealWindowFollowingAndClosure();SpeciesLimitsAndBraking();ManualCareIsNotInterrupted();NaturalTakeoffAndLandingUsePhysicalMotion();SocialGesturesWaitForPlantedFeet();BrakingPreservesTheCompleteMeal();PairedRunningUsesOneSupportAndBodyMultiplier();HighWindowFollowingRespectsTheRealCeiling();
        HighRefreshGaitFollowsRenderedTravel();AuthoredKneadDoesNotLeakLegacyJointsAcrossActions();
        AuthoredCompanionWalkRunUsesItsRealRegisteredTravel();RegisteredCompanionCareFinishesItsWholeProgram();
        OptionalWalkReviewKeepsBothCoatsOnTheRegisteredStride();
        ContactRecoveryFinishesBeforeAcceptedFishStarts();
        CompanionJumpReturnOwnsTheCompletedRecoveryBoundary();
        ExplicitCompanionJumpReturnsWhenTheRecoveryFinishes();
        AuthoredSleepKeepsItsIdentityIncludingWindowRestAndWake();
        CompanionPointerAndAuthoredActions();
        CompanionKeyboardAndNinePairings();
        ManualRequestsWaitForTheCompleteWake();
        std::cout<<"companion checks passed: "<<checks<<'\n';return 0;
    } catch(const std::exception& e) { std::cerr<<e.what()<<'\n';return 1; }
}
