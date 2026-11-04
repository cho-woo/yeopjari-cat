#pragma once

#include "pet_state.hpp"
#include <array>
#include <map>

namespace cat::life {

inline constexpr std::int64_t MinUtc = 946684800LL, MaxUtc = 32503680000LL, Day = 86400;
enum class Temperament { Legacy = -1, Coward, Friendly, Chic, Foodie, Playful, Sleepy };
inline const wchar_t* TemperamentName(Temperament value) {
    switch (value) {
    case Temperament::Coward: return L"겁쟁이";
    case Temperament::Friendly: return L"개냥이";
    case Temperament::Chic: return L"시크냥";
    case Temperament::Foodie: return L"먹보";
    case Temperament::Playful: return L"장난꾸러기";
    case Temperament::Sleepy: return L"잠보";
    default: return L""; // Caller retains the original birth-derived label.
    }
}
inline std::uint64_t Mix(std::uint64_t n) {
    n += 0x9e3779b97f4a7c15ULL;
    n = (n ^ (n >> 30)) * 0xbf58476d1ce4e5b9ULL;
    n = (n ^ (n >> 27)) * 0x94d049bb133111ebULL;
    return n ^ (n >> 31);
}
struct State {
    std::int64_t bornUtc = MinUtc, updatedUtc = MinUtc;
    Temperament temperament = Temperament::Legacy;
    double sleepiness = .35, boredom = .2, mood = .6, activity = .5;
    std::int64_t lastInteractionUtc = 0;
    std::array<std::int64_t, 2> rareUtc{};
    std::int64_t rareDay = -1;
    unsigned rareCount = 0;
    std::int64_t celebrationDay = -1, celebrationUtc = 0;
    unsigned unlockedHats = 0;
};
struct Observation {
    int localHour = 12;
    bool sleeping = false, interacting = false, active = true;
    double fullness = 88;
};
struct Profile {
    double sleepiness = .35, boredom = .2, mood = .6, activity = .5;
    double activityScale = 1, restScale = 1, socialScale = 1, curiosityScale = 1, foodScale = 1;
    double shyness = .6;
};
// An explicitly selected rare disappearance, separate from ordinary locomotion.
// The host relocates on the single fully transparent midpoint; pauses do not
// consume the fade and a scheduler stall cannot skip straight to the far edge.
struct ScreenCrossing {
    double elapsedMs=900;
    bool relocated=false;
    void Start() { elapsedMs=0;relocated=false; }
    bool Active() const { return elapsedMs<900; }
    bool Advance(double dtMs,bool available) {
        if(!Active()||!available||!std::isfinite(dtMs)||dtMs<=0||dtMs>250)return false;
        elapsedMs=std::min(900.0,elapsedMs+dtMs);
        if(elapsedMs>=450&&!relocated){elapsedMs=450;relocated=true;return true;}
        return false;
    }
    double Opacity() const {
        if(!Active())return 1;
        return std::clamp(std::abs(elapsedMs-450)/450.0,0.0,1.0);
    }
};
inline State FromLegacy(const cat::PetState& pet, std::int64_t now) {
    State s; s.bornUtc = pet.bornUtc; s.updatedUtc = std::max({pet.bornUtc,pet.updatedUtc,now}); return s;
}
inline State NewPet(const cat::PetState& pet, std::int64_t now, std::uint64_t entropy) {
    auto s = FromLegacy(pet,now);
    s.temperament = static_cast<Temperament>(Mix(entropy ^ static_cast<std::uint64_t>(pet.bornUtc)) % 6);
    return s;
}
inline bool Valid(const State& s) {
    if (s.bornUtc < MinUtc || s.bornUtc > MaxUtc || s.updatedUtc < s.bornUtc || s.updatedUtc > MaxUtc) return false;
    const int type = static_cast<int>(s.temperament);
    if (type < -1 || type > 5 || s.rareCount > 2 || s.unlockedHats > 7) return false;
    for (double v : {s.sleepiness,s.boredom,s.mood,s.activity})
        if (!std::isfinite(v) || v < 0 || v > 1) return false;
    for (auto stamp : {s.lastInteractionUtc,s.rareUtc[0],s.rareUtc[1],s.celebrationUtc})
        if (stamp && (stamp < s.bornUtc || stamp > s.updatedUtc)) return false;
    for (auto date : {s.rareDay,s.celebrationDay})
        if (date < -1 || date > s.updatedUtc/Day+1) return false;
    if (s.rareUtc[0] && s.rareUtc[1] && std::abs(s.rareUtc[0]-s.rareUtc[1]) < 4*3600) return false;
    return true;
}
inline bool InheritRareCooldown(State& s,std::int64_t stamp) {
    if(!Valid(s)||s.rareUtc[0]||s.rareUtc[1]||stamp<s.bornUtc||stamp>MaxUtc)return false;
    s.updatedUtc=std::max(s.updatedUtc,stamp);
    s.rareUtc[0]=stamp;s.rareDay=stamp/Day;s.rareCount=1;
    return true;
}
inline void Observe(State& s, std::int64_t now, const Observation& o) {
    if (!Valid(s) || now <= s.updatedUtc || now > MaxUtc) return;
    const auto dt = now - s.updatedUtc;
    s.updatedUtc = now;
    // Hidden/paused time and long offline gaps neither exhaust nor entertain
    // the pet. No decades of inferred emotional change are replayed on resume.
    if (!o.active || dt > 60) return;
    if (o.interacting) s.lastInteractionUtc = now;
    const int hour = (o.localHour%24+24)%24;
    const bool night = hour >=22 || hour <6;
    const double fullness = std::isfinite(o.fullness) ? std::clamp(o.fullness,20.0,100.0) : 88;
    const auto toward = [dt](double current,double target,double seconds) {
        return std::clamp(target+(current-target)*std::exp(-static_cast<double>(dt)/seconds),0.0,1.0);
    };
    s.sleepiness = toward(s.sleepiness, o.sleeping ? .03 : (night ? .95 : .72), o.sleeping ? 3600 : 6*3600);
    s.boredom = toward(s.boredom, o.interacting ? .02 : o.sleeping ? .15 : .8, o.interacting ? 180 : 3*3600);
    s.mood = toward(s.mood, o.interacting ? .95 : fullness <=35 ? .3 : .65, o.interacting ? 300 : 2*3600);
    s.activity = toward(s.activity, o.sleeping ? .12 : o.interacting ? .9 : .55, 900);
}
inline Profile Summarize(const State& s, double bond) {
    Profile p;
    p.sleepiness=s.sleepiness; p.boredom=s.boredom; p.mood=s.mood; p.activity=s.activity;
    bond=std::isfinite(bond)?std::clamp(bond,0.0,100.0):0;
    p.activityScale = .65 + .55*p.activity + .25*p.boredom - .2*p.sleepiness;
    p.restScale = .65 + 1.1*p.sleepiness;
    p.socialScale = .65 + .5*p.mood + .35*bond/100;
    p.curiosityScale = .8 + .35*p.boredom;
    p.shyness = std::clamp(.8-.65*bond/100, .1,.9);
    switch(s.temperament) {
    case Temperament::Coward: p.activityScale*=.8; p.curiosityScale*=.75; p.shyness=std::min(.95,p.shyness+.12); break;
    case Temperament::Friendly: p.socialScale*=1.3; p.shyness*=.8; break;
    case Temperament::Chic: p.socialScale*=.8; break;
    case Temperament::Foodie: p.foodScale=1.3; break;
    case Temperament::Playful: p.activityScale*=1.25; p.curiosityScale*=1.2; break;
    case Temperament::Sleepy: p.restScale*=1.35; p.activityScale*=.8; break;
    case Temperament::Legacy: break;
    }
    return p;
}

enum class Event { None, PunchStartle, TailStartle, SneezeStartle, Yawn, SleepTalk, Peek, SmallBox, CrownPride, EdgeHang, ScreenCrossing, BondKnead, BondGreeting, BondLifetime, Birthday, FirstWeek, FirstYear, FifthYear };
enum class Trigger { None, Punch, TailChase, Sneeze };
inline constexpr unsigned SantaHat=1, PumpkinHat=2, BirthdayHat=4;
struct EventContext {
    std::int64_t nowUtc=0, localDay=0;
    std::int64_t inheritedRareUtc=0;
    bool busy=true, sleeping=false, perched=false, boxAvailable=false, crownWorn=false, roamingPaused=false;
    bool seasonalEnabled=true;
    double edgeDistanceLogical=1000;
    bool edgeSupported=false, screenCrossingSafe=false;
    Trigger trigger=Trigger::None;
    double bond=0, fullness=88;
};
inline bool ContextValid(const State& s,const EventContext& c) {
    return Valid(s) && c.nowUtc>=s.updatedUtc && c.nowUtc<=MaxUtc &&
        c.localDay>=c.nowUtc/Day-1 && c.localDay<=c.nowUtc/Day+1;
}
inline bool IsCelebration(Event e) {return e==Event::Birthday||e==Event::FirstWeek||e==Event::FirstYear||e==Event::FifthYear;}
inline Event AnniversaryEvent(const State& s,const EventContext& c) {
    // Compare civil dates, retaining Feb29 -> Feb28 anniversary semantics.
    const auto local=cat::FromUnix(c.localDay*Day);
    const auto born=cat::FromUnix(s.bornUtc);
    const int years=local.year-born.year;
    if (years>=1) {
        const auto anniversary=cat::FromUnix(cat::Anniversary(s.bornUtc,years));
        if (anniversary.month==local.month && anniversary.day==local.day)
            return years==1?Event::FirstYear:years==5?Event::FifthYear:Event::Birthday;
    }
    if (c.localDay==s.bornUtc/Day+7) return Event::FirstWeek;
    return Event::None;
}
inline unsigned SeasonalUnlocks(const State& s,const EventContext& c) {
    if (!ContextValid(s,c) || !c.seasonalEnabled) return 0;
    const auto local=cat::FromUnix(c.localDay*Day);
    unsigned bits=0;
    if (local.month==12 && local.day>=20 && local.day<=26) bits|=SantaHat;
    if (local.month==10 && local.day>=25) bits|=PumpkinHat;
    const auto celebration=AnniversaryEvent(s,c);
    if (celebration==Event::Birthday||celebration==Event::FirstYear||celebration==Event::FifthYear) bits|=BirthdayHat;
    return bits;
}
inline bool WinterScarfSuggested(const State& s,const EventContext& c) {
    if (!ContextValid(s,c)||!c.seasonalEnabled) return false;
    const auto month=cat::FromUnix(c.localDay*Day).month;
    return month==12||month==1||month==2;
}
inline unsigned DailyRareQuota(const State& s,std::int64_t localDay) {
    return static_cast<unsigned>(Mix(static_cast<std::uint64_t>(s.bornUtc)^Mix(static_cast<std::uint64_t>(localDay)))%3);
}
inline bool RareReady(const State& s,const EventContext& c) {
    if (!ContextValid(s,c)||c.busy||c.localDay<s.rareDay) return false;
    if(c.inheritedRareUtc && (c.nowUtc<c.inheritedRareUtc || c.nowUtc-c.inheritedRareUtc<12*3600))return false;
    const unsigned used=c.localDay==s.rareDay?s.rareCount:0;
    if (used>=DailyRareQuota(s,c.localDay)) return false;
    unsigned rolling=0;
    for (auto t:s.rareUtc) if(t && c.nowUtc-t<Day) {
        ++rolling; if(c.nowUtc-t<4*3600) return false;
    }
    return rolling<2;
}
inline bool Eligible(const State& s,const EventContext& c,Event e) {
    if (e==Event::None||!ContextValid(s,c)||c.busy) return false;
    if (IsCelebration(e))
        return c.trigger==Trigger::None&&c.seasonalEnabled&&!c.sleeping&&c.localDay>s.celebrationDay &&
            (!s.celebrationUtc||c.nowUtc-s.celebrationUtc>=20*3600) && AnniversaryEvent(s,c)==e;
    if(!RareReady(s,c)) return false;
    // A host may open the gate during a punch/sneeze/tail motion specifically
    // for its matching startled follow-up. Other rare actions still wait.
    if(c.trigger!=Trigger::None && e!=Event::PunchStartle && e!=Event::TailStartle && e!=Event::SneezeStartle)return false;
    switch(e) {
    case Event::PunchStartle:return !c.sleeping&&!c.roamingPaused&&c.trigger==Trigger::Punch;
    case Event::TailStartle:return !c.sleeping&&!c.roamingPaused&&c.trigger==Trigger::TailChase;
    case Event::SneezeStartle:return !c.sleeping&&!c.roamingPaused&&c.trigger==Trigger::Sneeze;
    case Event::Yawn:return !c.sleeping&&s.sleepiness>=.35;
    case Event::SleepTalk:return c.sleeping;
    case Event::Peek:return !c.sleeping&&!c.perched&&!c.roamingPaused;
    case Event::SmallBox:return !c.sleeping&&!c.perched&&c.boxAvailable;
    case Event::CrownPride:return !c.sleeping&&c.crownWorn;
    case Event::EdgeHang:return !c.sleeping&&!c.roamingPaused&&c.perched&&c.edgeSupported&&
        std::isfinite(c.edgeDistanceLogical)&&c.edgeDistanceLogical>=0&&c.edgeDistanceLogical<=24;
    case Event::ScreenCrossing:return !c.sleeping&&!c.roamingPaused&&!c.perched&&c.screenCrossingSafe;
    case Event::BondKnead:
    case Event::BondGreeting:
    case Event::BondLifetime:
        if(c.sleeping||!std::isfinite(c.fullness)||c.fullness<=35||c.fullness>100||
           !std::isfinite(c.bond)||c.bond>100) return false;
        if(e==Event::BondKnead) return c.nowUtc-s.bornUtc>=90*Day&&c.bond>=25;
        if(e==Event::BondGreeting) return c.nowUtc>=cat::Anniversary(s.bornUtc,1)&&c.bond>=45;
        return c.nowUtc>=cat::Anniversary(s.bornUtc,5)&&c.bond>=65;
    default:return false;
    }
}
inline Event PeekEvent(const State& s,const EventContext& c) {
    if (!ContextValid(s,c) || c.busy) return Event::None;
    const auto celebration=AnniversaryEvent(s,c);
    if (Eligible(s,c,celebration)) return celebration;
    for(auto e:{Event::PunchStartle,Event::TailStartle,Event::SneezeStartle})
        if(Eligible(s,c,e)) return e;
    if(c.trigger!=Trigger::None)return Event::None;
    // A stable five-minute draw prevents render-rate and restart farming.
    const auto draw=Mix(static_cast<std::uint64_t>(s.bornUtc)^Mix(static_cast<std::uint64_t>(c.nowUtc/300))^0x1feda75ULL);
    if(draw%1000>=18) return Event::None;
    std::array<Event,10> choices{};unsigned count=0;
    for(auto e:{Event::Yawn,Event::SleepTalk,Event::Peek,Event::SmallBox,Event::CrownPride,Event::EdgeHang,Event::ScreenCrossing,Event::BondKnead,Event::BondGreeting,Event::BondLifetime})
        if(Eligible(s,c,e)) choices[count++]=e;
    return count?choices[(draw/1000)%count]:Event::None;
}
inline bool CommitEvent(State& s,const EventContext& c,Event e) {
    if(!Eligible(s,c,e)) return false;
    s.updatedUtc=c.nowUtc;
    if(IsCelebration(e)) {s.celebrationDay=c.localDay;s.celebrationUtc=c.nowUtc;}
    else {
        if(s.rareDay!=c.localDay) {s.rareDay=c.localDay;s.rareCount=0;}
        ++s.rareCount;
        const auto oldest=s.rareUtc[0]<=s.rareUtc[1]?0:1;s.rareUtc[oldest]=c.nowUtc;
    }
    s.unlockedHats|=SeasonalUnlocks(s,c);
    return true;
}
inline bool UnlockSeasonal(State& s,const EventContext& c) {
    const auto bits=SeasonalUnlocks(s,c);
    if((s.unlockedHats|bits)==s.unlockedHats) return false;
    s.updatedUtc=c.nowUtc;s.unlockedHats|=bits;return true;
}

inline std::string Serialize(const State& s) {
    if(!Valid(s))return {};
    std::ostringstream out;out.imbue(std::locale::classic());out<<std::setprecision(17);
    out<<"schema=1\nborn_utc="<<s.bornUtc<<"\nupdated_utc="<<s.updatedUtc
        <<"\ntemperament="<<static_cast<int>(s.temperament)<<"\nsleepiness="<<s.sleepiness
        <<"\nboredom="<<s.boredom<<"\nmood="<<s.mood<<"\nactivity="<<s.activity
        <<"\nlast_interaction_utc="<<s.lastInteractionUtc<<"\nrare_0="<<s.rareUtc[0]<<"\nrare_1="<<s.rareUtc[1]
        <<"\nrare_day="<<s.rareDay<<"\nrare_count="<<s.rareCount<<"\ncelebration_day="<<s.celebrationDay
        <<"\ncelebration_utc="<<s.celebrationUtc<<"\nunlocked_hats="<<s.unlockedHats<<'\n';
    const auto body=out.str();out<<"checksum="<<std::hex<<std::setw(16)<<std::setfill('0')<<cat::Checksum(body)<<'\n';return out.str();
}
inline bool Parse(const std::string& text,State& destination,std::string& error) {
    const auto fail=[&](const char*why){error=why;return false;};
    if(text.empty()||text.size()>8192)return fail("Empty or oversized life record");
    const auto pos=text.rfind("checksum=");
    if(pos==std::string::npos||!pos||text[pos-1]!='\n')return fail("Missing life checksum");
    const auto body=text.substr(0,pos);auto hash=text.substr(pos+9);if(!hash.empty()&&hash.back()=='\n')hash.pop_back();
    if(hash.size()!=16||hash.find_first_not_of("0123456789abcdefABCDEF")!=std::string::npos)return fail("Malformed life checksum");
    std::uint64_t expected=0;std::istringstream h(hash);h>>std::hex>>expected;
    if(!h||expected!=cat::Checksum(body))return fail("Life checksum mismatch");
    std::map<std::string,std::string> fields;std::istringstream lines(body);std::string line;
    while(std::getline(lines,line)){const auto split=line.find('=');if(split==std::string::npos||!split||!fields.emplace(line.substr(0,split),line.substr(split+1)).second)return fail("Duplicate or malformed life field");}
    if(fields["schema"]!="1")return fail("Unsupported life schema");
    fields.erase("schema");
    auto integer=[&](const char*name,auto&value,bool sign=false){auto it=fields.find(name);if(it==fields.end())return false;auto str=it->second;auto digits=sign&&!str.empty()&&str[0]=='-'?str.substr(1):str;if(digits.empty()||digits.size()>18||digits.find_first_not_of("0123456789")!=std::string::npos)return false;std::istringstream in(str);in.imbue(std::locale::classic());in>>value;if(!in||in.peek()!=std::char_traits<char>::eof())return false;fields.erase(it);return true;};
    auto real=[&](const char*name,double&value){auto it=fields.find(name);if(it==fields.end()||it->second.empty()||it->second.size()>32||it->second.find_first_not_of("0123456789.eE+-")!=std::string::npos)return false;std::istringstream in(it->second);in.imbue(std::locale::classic());in>>value;if(!in||in.peek()!=std::char_traits<char>::eof()||!std::isfinite(value))return false;fields.erase(it);return true;};
    State s;int type=-1;
    if(!integer("born_utc",s.bornUtc)||!integer("updated_utc",s.updatedUtc)||!integer("temperament",type,true)||!real("sleepiness",s.sleepiness)||!real("boredom",s.boredom)||!real("mood",s.mood)||!real("activity",s.activity)||!integer("last_interaction_utc",s.lastInteractionUtc)||!integer("rare_0",s.rareUtc[0])||!integer("rare_1",s.rareUtc[1])||!integer("rare_day",s.rareDay,true)||!integer("rare_count",s.rareCount)||!integer("celebration_day",s.celebrationDay,true)||!integer("celebration_utc",s.celebrationUtc)||!integer("unlocked_hats",s.unlockedHats)||!fields.empty())return fail("Invalid life fields");
    s.temperament=static_cast<Temperament>(type);if(!Valid(s))return fail("Invalid life values");destination=s;error.clear();return true;
}

} // namespace cat::life
