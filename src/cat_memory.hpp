#pragma once

#include "pet_state.hpp"
#include <array>
#include <map>

namespace cat::memory {

inline constexpr std::int64_t kMinUtc = 946684800LL;
inline constexpr std::int64_t kMaxUtc = 32503680000LL;
inline constexpr std::int64_t kDaySeconds = 86400;
inline constexpr std::uint64_t kMaxCount = 999999999;
inline constexpr std::size_t kDailyCredits = 24;
enum class Event { Meal, Treat, Pet, Play };

// Kept separately from the original pet.ini. The birthday owns this record;
// copying another cat's memories must never silently change its identity.
struct State {
    std::int64_t bornUtc = kMinUtc, updatedUtc = kMinUtc;
    double bond = 0;
    std::uint64_t meals = 0, treats = 0, pets = 0, plays = 0, learningSamples = 0;
    double petPreference = 0, treatPreference = 0, playPreference = 0;
    double sociabilityShift = 0, energyShift = 0, curiosityShift = 0, mischiefShift = 0;
    std::int64_t lastLearningUtc = kMinUtc;
    std::array<std::int64_t, 4> lastCareUtc{};
    // Exact rolling-24h credit ledger, not a resettable calendar-day counter.
    std::array<std::int64_t, kDailyCredits> creditedUtc{};
    std::int64_t lastApproachUtc = 0, lastTreatRequestUtc = 0, lastRareUtc = 0, lastRoutineUtc = 0;
    std::int64_t lastMorningUtc = 0, lastLunchUtc = 0, lastDawnUtc = 0;
    std::int64_t lastMorningDay = -1, lastLunchDay = -1, lastDawnDay = -1;
};
struct Profile {
    double bond = 0, petPreference = 0, treatPreference = 0;
    double sociabilityShift = 0, energyShift = 0, curiosityShift = 0, mischiefShift = 0;
};
inline Profile Summarize(const State& state) {
    return {state.bond, state.petPreference, state.treatPreference,
        state.sociabilityShift, state.energyShift, state.curiosityShift, state.mischiefShift};
}

inline void Advance(State& state, std::int64_t now) {
    // Absence does not erase a bond, create care, or apply a new habit to years
    // of offline time. Monotonic UTC also prevents clock rollback farming.
    if (now > state.updatedUtc && now <= kMaxUtc) state.updatedUtc = now;
}
inline State FromLegacy(const cat::PetState& pet, std::int64_t now) {
    State state;
    state.bornUtc = pet.bornUtc;
    state.updatedUtc = std::max({pet.bornUtc, pet.updatedUtc, now});
    state.lastLearningUtc = state.updatedUtc;
    state.meals = pet.feedCount; state.treats = pet.snackCount;
    const double care = static_cast<double>(state.meals) + static_cast<double>(state.treats);
    const double days = static_cast<double>(std::max<std::int64_t>(0, state.updatedUtc - state.bornUtc)) / kDaySeconds;
    // Existing age alone is not invented affection. Retain observed meal/treat
    // history and grant a modest, bounded starting bond only when care exists.
    if (care > 0) {
        state.bond = std::min(35.0, 2.5 * std::log1p(care) + std::min(12.0, days / 60.0));
        state.treatPreference = static_cast<double>(state.treats) / care;
    }
    return state;
}
inline double LearnTowards(double current, double target) {
    // At most .0015 per credited active day: even sustained care takes months
    // to change a birth-derived trait substantially. No calendar catch-up.
    return std::clamp(current + std::clamp((target - current) * .03, -.0015, .0015), -.15, .15);
}
inline bool Record(State& state, Event event, std::int64_t now) {
    const auto index = static_cast<std::size_t>(event);
    if (index >= state.lastCareUtc.size() || now < state.bornUtc || now < state.updatedUtc || now > kMaxUtc) return false;
    Advance(state, now);
    const auto last = state.lastCareUtc[index];
    // Food callers record only successfully accepted meals. Pet/play gestures
    // are deliberately rate limited so repeated clicks are not years of care.
    const std::int64_t gap = event == Event::Pet || event == Event::Play ? 300 : 1;
    if (last && now - last < gap) return false;
    state.lastCareUtc[index] = now;
    auto increment = [](std::uint64_t& value) { if (value < kMaxCount) ++value; };
    if (event == Event::Meal) increment(state.meals);
    if (event == Event::Treat) increment(state.treats);
    std::size_t slot = kDailyCredits;
    std::int64_t newestCredit = 0;
    for (std::size_t i = 0; i < state.creditedUtc.size(); ++i) {
        const auto stamp = state.creditedUtc[i];
        newestCredit = std::max(newestCredit, stamp);
        if (!stamp || now - stamp >= kDaySeconds) slot = i;
    }
    // A shared gap also prevents alternating gesture types to evade the cap.
    if (slot == kDailyCredits || (newestCredit && now - newestCredit < 300)) return false;
    if (event == Event::Pet) increment(state.pets);
    if (event == Event::Play) increment(state.plays);
    if (state.learningSamples && now - state.lastLearningUtc >= kDaySeconds) {
        // Learn from PREVIOUS credited care. The current event has no instant
        // personality effect, and is never projected back over an absence.
        state.sociabilityShift = LearnTowards(state.sociabilityShift,
            .15 * state.petPreference + .10 * state.playPreference - .04 * (1 - state.petPreference - state.playPreference));
        state.energyShift = LearnTowards(state.energyShift, .15 * state.playPreference - .03);
        state.curiosityShift = LearnTowards(state.curiosityShift, .10 * state.playPreference);
        state.mischiefShift = LearnTowards(state.mischiefShift, .06 * state.treatPreference + .06 * state.playPreference);
        state.lastLearningUtc = now;
    } else if (!state.learningSamples) state.lastLearningUtc = now;
    state.creditedUtc[slot] = now;
    increment(state.learningSamples);
    static constexpr double gains[] = {.05, .055, .07, .06};
    state.bond = std::min(100.0, state.bond + gains[index]);
    state.petPreference = state.petPreference * .98 + (event == Event::Pet ? .02 : 0);
    state.treatPreference = state.treatPreference * .98 + (event == Event::Treat ? .02 : 0);
    state.playPreference = state.playPreference * .98 + (event == Event::Play ? .02 : 0);
    return true;
}

inline bool Valid(const State& s) {
    if (s.bornUtc < kMinUtc || s.bornUtc > kMaxUtc || s.updatedUtc < s.bornUtc || s.updatedUtc > kMaxUtc) return false;
    const auto real = [](double x, double low, double high) { return std::isfinite(x) && x >= low && x <= high; };
    if (!real(s.bond, 0, 100) || !real(s.petPreference, 0, 1) || !real(s.treatPreference, 0, 1) || !real(s.playPreference, 0, 1)) return false;
    if (s.petPreference + s.treatPreference + s.playPreference > 1.000000001) return false;
    for (double shift : {s.sociabilityShift, s.energyShift, s.curiosityShift, s.mischiefShift})
        if (!real(shift, -.15, .15)) return false;
    for (auto count : {s.meals,s.treats,s.pets,s.plays,s.learningSamples}) if (count > kMaxCount) return false;
    const auto timestamp = [&](std::int64_t t) { return t == 0 || (t >= s.bornUtc && t <= s.updatedUtc); };
    if (!s.lastLearningUtc || !timestamp(s.lastLearningUtc)) return false;
    for (auto t : s.lastCareUtc) if (!timestamp(t)) return false;
    for (auto t : s.creditedUtc) if (!timestamp(t)) return false;
    for (auto t : {s.lastApproachUtc,s.lastTreatRequestUtc,s.lastRareUtc,s.lastRoutineUtc,
                  s.lastMorningUtc,s.lastLunchUtc,s.lastDawnUtc}) if (!timestamp(t)) return false;
    for (auto day : {s.lastMorningDay,s.lastLunchDay,s.lastDawnDay})
        if (day < -1 || day > s.updatedUtc/kDaySeconds + 1) return false;
    // Credited events are distinct and at least five minutes apart even after
    // a restart; reject forged ledger duplicates rather than weakening caps.
    for (std::size_t i=0;i<s.creditedUtc.size();++i)
        for (std::size_t j=i+1;j<s.creditedUtc.size();++j)
            if (s.creditedUtc[i] && s.creditedUtc[j] && std::abs(s.creditedUtc[i]-s.creditedUtc[j]) < 300) return false;
    return true;
}

inline std::string Serialize(const State& s) {
    if (!Valid(s)) return {};
    std::ostringstream out; out.imbue(std::locale::classic()); out << std::setprecision(17);
    out << "schema=1\nborn_utc=" << s.bornUtc << "\nupdated_utc=" << s.updatedUtc << "\nbond=" << s.bond
        << "\nmeals=" << s.meals << "\ntreats=" << s.treats << "\npets=" << s.pets << "\nplays=" << s.plays
        << "\nlearning_samples=" << s.learningSamples << "\npet_preference=" << s.petPreference
        << "\ntreat_preference=" << s.treatPreference << "\nplay_preference=" << s.playPreference
        << "\nsociability_shift=" << s.sociabilityShift << "\nenergy_shift=" << s.energyShift
        << "\ncuriosity_shift=" << s.curiosityShift << "\nmischief_shift=" << s.mischiefShift
        << "\nlast_learning_utc=" << s.lastLearningUtc << '\n';
    for (std::size_t i=0;i<s.lastCareUtc.size();++i) out << "last_care_" << i << '=' << s.lastCareUtc[i] << '\n';
    for (std::size_t i=0;i<s.creditedUtc.size();++i) out << "credit_" << i << '=' << s.creditedUtc[i] << '\n';
    out << "last_approach_utc=" << s.lastApproachUtc << "\nlast_treat_request_utc=" << s.lastTreatRequestUtc
        << "\nlast_rare_utc=" << s.lastRareUtc << "\nlast_routine_utc=" << s.lastRoutineUtc
        << "\nlast_morning_utc=" << s.lastMorningUtc << "\nlast_lunch_utc=" << s.lastLunchUtc
        << "\nlast_dawn_utc=" << s.lastDawnUtc << "\nlast_morning_day=" << s.lastMorningDay
        << "\nlast_lunch_day=" << s.lastLunchDay << "\nlast_dawn_day=" << s.lastDawnDay << '\n';
    const auto body = out.str();
    out << "checksum=" << std::hex << std::setw(16) << std::setfill('0') << cat::Checksum(body) << '\n';
    return out.str();
}

inline bool Parse(const std::string& text, State& destination, std::string& error) {
    const auto fail = [&](const char* why) { error = why; return false; };
    if (text.empty() || text.size() > 16384) return fail("Empty or oversized memory");
    const auto checksumAt = text.rfind("checksum=");
    if (checksumAt == std::string::npos || !checksumAt || text[checksumAt-1] != '\n') return fail("Missing memory checksum");
    const auto body = text.substr(0,checksumAt);
    auto checksum = text.substr(checksumAt+9);
    if (!checksum.empty() && checksum.back() == '\n') checksum.pop_back();
    if (checksum.size()!=16 || checksum.find_first_not_of("0123456789abcdefABCDEF") != std::string::npos) return fail("Malformed memory checksum");
    std::uint64_t expected=0; std::istringstream hash(checksum); hash.imbue(std::locale::classic()); hash >> std::hex >> expected;
    if (!hash || expected != cat::Checksum(body)) return fail("Memory checksum mismatch");
    std::map<std::string,std::string> fields;
    std::istringstream lines(body); std::string line;
    while (std::getline(lines,line)) {
        const auto split=line.find('=');
        if (split==std::string::npos || !split || !fields.emplace(line.substr(0,split),line.substr(split+1)).second)
            return fail("Malformed or duplicate memory field");
    }
    const auto schema=fields.find("schema");
    if (schema==fields.end() || schema->second!="1") return fail("Unsupported memory schema");
    fields.erase(schema);
    auto integer = [&](const std::string& key, auto& value, bool signedValue=false) {
        const auto it=fields.find(key); if (it==fields.end()) return false;
        const auto str=it->second;
        if (str.empty() || str.size()>18) return false;
        const auto digits = signedValue && str[0]=='-' ? str.substr(1) : str;
        if (digits.empty() || digits.find_first_not_of("0123456789")!=std::string::npos) return false;
        std::istringstream input(str); input.imbue(std::locale::classic()); input >> value;
        if (!input || input.peek()!=std::char_traits<char>::eof()) return false;
        fields.erase(it); return true;
    };
    auto real = [&](const std::string& key, double& value) {
        const auto it=fields.find(key); if (it==fields.end()) return false;
        const auto& str=it->second;
        if (str.empty() || str.size()>32 || str.find_first_not_of("0123456789.-+eE")!=std::string::npos) return false;
        std::istringstream input(str); input.imbue(std::locale::classic()); input >> value;
        if (!input || input.peek()!=std::char_traits<char>::eof() || !std::isfinite(value)) return false;
        fields.erase(it); return true;
    };
    State s;
    if (!integer("born_utc",s.bornUtc) || !integer("updated_utc",s.updatedUtc) || !real("bond",s.bond) ||
        !integer("meals",s.meals) || !integer("treats",s.treats) || !integer("pets",s.pets) || !integer("plays",s.plays) ||
        !integer("learning_samples",s.learningSamples) || !real("pet_preference",s.petPreference) ||
        !real("treat_preference",s.treatPreference) || !real("play_preference",s.playPreference) ||
        !real("sociability_shift",s.sociabilityShift) || !real("energy_shift",s.energyShift) ||
        !real("curiosity_shift",s.curiosityShift) || !real("mischief_shift",s.mischiefShift) ||
        !integer("last_learning_utc",s.lastLearningUtc)) return fail("Missing or invalid memory value");
    for (std::size_t i=0;i<s.lastCareUtc.size();++i)
        if (!integer("last_care_"+std::to_string(i),s.lastCareUtc[i])) return fail("Invalid care timestamp");
    for (std::size_t i=0;i<s.creditedUtc.size();++i)
        if (!integer("credit_"+std::to_string(i),s.creditedUtc[i])) return fail("Invalid credit timestamp");
    if (!integer("last_approach_utc",s.lastApproachUtc) || !integer("last_treat_request_utc",s.lastTreatRequestUtc) ||
        !integer("last_rare_utc",s.lastRareUtc) || !integer("last_routine_utc",s.lastRoutineUtc) ||
        !integer("last_morning_utc",s.lastMorningUtc) || !integer("last_lunch_utc",s.lastLunchUtc) ||
        !integer("last_dawn_utc",s.lastDawnUtc) || !integer("last_morning_day",s.lastMorningDay,true) ||
        !integer("last_lunch_day",s.lastLunchDay,true) || !integer("last_dawn_day",s.lastDawnDay,true)) return fail("Invalid routine timestamp");
    if (!fields.empty()) return fail("Unknown memory field");
    if (!Valid(s)) return fail("Memory values outside valid bounds");
    destination=s; error.clear(); return true;
}
} // namespace cat::memory
