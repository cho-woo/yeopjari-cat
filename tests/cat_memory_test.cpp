#include "../src/cat_memory.hpp"
#include <iostream>
#include <stdexcept>
#include <vector>

namespace {
using namespace cat::memory;
int checks=0;
#define CHECK(x) do { ++checks; if (!(x)) throw std::runtime_error(std::string(__FILE__)+":"+std::to_string(__LINE__)+" " #x); } while(false)
constexpr auto born=1709164800LL; // 2024-02-29 UTC
std::string Seal(std::string body) {
    std::ostringstream out; out << body << "checksum=" << std::hex << std::setw(16) << std::setfill('0') << cat::Checksum(body) << '\n'; return out.str();
}
std::string Body(const State& state) { auto text=Serialize(state);return text.substr(0,text.rfind("checksum=")); }
std::string ReplaceField(const State& state,const std::string& key,const std::string& value) {
    auto body=Body(state); const auto at=body.find(key+"="); CHECK(at!=std::string::npos);
    const auto end=body.find('\n',at);body.replace(at,end-at,key+"="+value);return Seal(body);
}
void MustReject(const std::string& bytes) {
    State sentinel=FromLegacy(cat::NewPet(born),born); sentinel.bond=42;
    const auto before=Serialize(sentinel); std::string error;
    CHECK(!Parse(bytes,sentinel,error)); CHECK(!error.empty()); CHECK(Serialize(sentinel)==before);
}
void MigrationAndRoundTrip() {
    auto old=cat::NewPet(born);auto fresh=FromLegacy(old,born);
    CHECK(fresh.bond==0);CHECK(fresh.pets==0&&fresh.plays==0);
    old.feedCount=1500;old.snackCount=400;old.position=.31;old.hunger=55;old.affection=39;
    const auto before=cat::Serialize(old);
    const auto tenYears=cat::Anniversary(born,10);
    const auto migrated=FromLegacy(old,tenYears);
    CHECK(migrated.bornUtc==born);CHECK(migrated.meals==1500&&migrated.treats==400);
    CHECK(migrated.bond>20&&migrated.bond<=35);
    CHECK(migrated.pets==0&&migrated.plays==0&&migrated.learningSamples==0);
    CHECK(migrated.sociabilityShift==0&&migrated.energyShift==0);
    CHECK(cat::Serialize(old)==before);
    CHECK(cat::Age(migrated.bornUtc,tenYears).years==10);
    State parsed;std::string error="old";
    CHECK(Parse(Serialize(migrated),parsed,error));CHECK(error.empty());
    CHECK(Serialize(parsed)==Serialize(migrated));
    auto noCare=FromLegacy(cat::NewPet(born),tenYears);CHECK(noCare.bond==0);
    auto futureLegacy=old;futureLegacy.updatedUtc=tenYears;
    CHECK(FromLegacy(futureLegacy,born+100).updatedUtc==tenYears);
}
void CreditAndLifetimeCaps() {
    auto state=FromLegacy(cat::NewPet(born),born);
    CHECK(Record(state,Event::Pet,born));
    const auto first=Serialize(state);
    for(int i=0;i<300;++i) CHECK(!Record(state,Event::Pet,born+i));
    CHECK(state.pets==1);CHECK(state.bond==.07);CHECK(state.learningSamples==1);
    CHECK(!Record(state,Event::Play,born+299));
    CHECK(Record(state,Event::Play,born+600));
    for(int i=2;i<24;++i) CHECK(Record(state,Event::Pet,born+i*600));
    CHECK(state.learningSamples==24);
    const auto fullBond=state.bond; const auto fullPets=state.pets;
    CHECK(!Record(state,Event::Pet,born+24*600));
    CHECK(state.bond==fullBond&&state.pets==fullPets);
    // A successful accepted meal remains a lifetime fact even when reward cap is full.
    CHECK(!Record(state,Event::Meal,born+15000));CHECK(state.meals==1);
    CHECK(!Record(state,Event::Treat,born+15001));CHECK(state.treats==1);
    CHECK(!Record(state,Event::Meal,born+15000));CHECK(state.meals==1);
    CHECK(!Record(state,Event::Pet,born+kDaySeconds-1));
    CHECK(Record(state,Event::Play,born+kDaySeconds));
    CHECK(state.learningSamples==25);
    State restored;std::string error;CHECK(Parse(Serialize(state),restored,error));
    CHECK(!Record(restored,Event::Play,born+kDaySeconds+1));
    CHECK(restored.learningSamples==25);
    CHECK(!Record(restored,static_cast<Event>(99),born+kDaySeconds+1000));
    (void)first;
}
void RollbackAndAbsence() {
    auto state=FromLegacy(cat::NewPet(born),born);
    CHECK(Record(state,Event::Pet,born));
    Advance(state,born+kDaySeconds);
    const auto before=Serialize(state);
    Advance(state,born-10000);CHECK(Serialize(state)==before);
    CHECK(!Record(state,Event::Treat,born+300));CHECK(Serialize(state)==before);
    const auto bond=state.bond;const auto petPref=state.petPreference;
    Advance(state,cat::Anniversary(born,25));
    CHECK(state.bond==bond);CHECK(state.pets==1);CHECK(state.petPreference==petPref);
    CHECK(state.sociabilityShift==0);
    const auto profile=Summarize(state);
    CHECK(profile.bond==bond&&profile.petPreference==petPref);
    CHECK(Record(state,Event::Pet,state.updatedUtc));
    CHECK(std::abs(state.sociabilityShift)<=.0015); // one active day, never25years
    const auto late=Serialize(state);Advance(state,kMaxUtc+1);CHECK(Serialize(state)==late);
}
void GradualLearningAndPersistence() {
    auto affectionate=FromLegacy(cat::NewPet(born),born);
    auto foodOnly=affectionate;
    double previous=0;
    for(int day=0;day<180;++day) {
        const auto at=born+day*kDaySeconds;
        for(int action=0;action<12;++action) {
            CHECK(Record(affectionate,Event::Pet,at+action*600));
            CHECK(Record(foodOnly,Event::Treat,at+action*600));
        }
        CHECK(std::abs(affectionate.sociabilityShift-previous)<=.0015000001);
        previous=affectionate.sociabilityShift;
        State restart;std::string error;
        CHECK(Parse(Serialize(affectionate),restart,error));
        CHECK(Serialize(restart)==Serialize(affectionate));affectionate=restart;
        CHECK(affectionate.bond>=0&&affectionate.bond<=100);
        CHECK(Valid(affectionate));CHECK(Valid(foodOnly));
    }
    CHECK(affectionate.petPreference>.95);
    CHECK(affectionate.sociabilityShift>.10&&affectionate.sociabilityShift<=.15);
    CHECK(foodOnly.sociabilityShift<0);
    CHECK(foodOnly.treatPreference>.95);
    CHECK(foodOnly.petPreference==0);
    CHECK(affectionate.bond==100);
    const auto shift=affectionate.sociabilityShift;
    for(int i=0;i<10000;++i) Record(affectionate,Event::Treat,affectionate.updatedUtc);
    CHECK(affectionate.sociabilityShift==shift);
    for(double x:{affectionate.sociabilityShift,affectionate.energyShift,foodOnly.mischiefShift})CHECK(std::abs(x)<=.15);
}
void StrictParser() {
    auto state=FromLegacy(cat::NewPet(born),born);
    CHECK(Record(state,Event::Pet,born));
    const auto good=Serialize(state);
    for(const auto& [key,value]:std::vector<std::pair<std::string,std::string>>{
        {"schema","2"},{"born_utc","-1"},{"updated_utc",std::to_string(born-1)},
        {"bond","nan"},{"bond","inf"},{"bond","100.01"},{"bond","-0.1"},{"bond","1e309"},
        {"pets","-1"},{"pets","1000000000"},{"pet_preference","1.01"},
        {"sociability_shift","0.151"},{"last_approach_utc",std::to_string(born+1)},
        {"last_care_0",std::to_string(born-1)},{"last_learning_utc","0"},
        {"last_morning_day","-2"},{"last_lunch_day","999999999"},
        {"credit_0",std::to_string(born)},{"bond","1.0junk"}}) MustReject(ReplaceField(state,key,value));
    MustReject(Seal(Body(state)+"new_field=1\n"));
    MustReject(Seal(Body(state)+"bond=0\n"));
    auto missing=Body(state);const auto at=missing.find("plays=");missing.erase(at,missing.find('\n',at)-at+1);MustReject(Seal(missing));
    auto corrupted=good;corrupted[20]^=1;MustReject(corrupted);
    MustReject(good+"extra");MustReject("");MustReject(std::string(16385,'x'));
    for(std::size_t size=0;size<good.size();size+=17)MustReject(good.substr(0,size));
    auto invalid=state;invalid.bond=std::numeric_limits<double>::quiet_NaN();CHECK(Serialize(invalid).empty());
    invalid=state;invalid.petPreference=.7;invalid.treatPreference=.7;CHECK(Serialize(invalid).empty());
    // All scheduler cooldowns survive restart; state can only belong to one birthday.
    state.lastApproachUtc=state.lastRareUtc=state.lastTreatRequestUtc=state.lastRoutineUtc=born;
    state.lastMorningUtc=state.lastLunchUtc=state.lastDawnUtc=born;
    state.lastMorningDay=state.lastLunchDay=state.lastDawnDay=born/kDaySeconds;
    State parsed;std::string error;CHECK(Parse(Serialize(state),parsed,error));CHECK(Serialize(parsed)==Serialize(state));
}
}
int main(){try{
    MigrationAndRoundTrip();CreditAndLifetimeCaps();RollbackAndAbsence();GradualLearningAndPersistence();StrictParser();
    std::cout<<"Memory: "<<checks<<" checks passed\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
