#include "../src/cat_memory.hpp"
#include "../src/cat_routine.hpp"
#include "../src/cat_behavior.hpp"

#include <array>
#include <cmath>
#include <iostream>
#include <random>
#include <stdexcept>
#include <string>

namespace {
using cat::memory::Event;
using cat::memory::State;
using cat::routine::Decision;
constexpr std::int64_t day = 86400;
const auto born = cat::ToUnix({2020, 2, 29});
constexpr std::uint64_t seed = 0x10CA7ULL;
#define REQUIRE(condition) do { if (!(condition)) throw std::runtime_error( \
    std::string(__FILE__) + ":" + std::to_string(__LINE__) + ": " #condition); } while (false)

std::array<double, 4> Shifts(const State& state) {
    return {state.sociabilityShift, state.energyShift, state.curiosityShift, state.mischiefShift};
}
cat::behavior::Experience Experience(const State& state) {
    const auto p = cat::memory::Summarize(state);
    return {p.bond, p.petPreference, p.treatPreference, p.sociabilityShift,
            p.energyShift, p.curiosityShift, p.mischiefShift};
}
bool SameTraits(const cat::behavior::Personality& a, const cat::behavior::Personality& b) {
    return a.curiosity == b.curiosity && a.energy == b.energy &&
           a.sociability == b.sociability && a.mischief == b.mischief;
}
void Restart(State& state) {
    const auto bytes = cat::memory::Serialize(state);
    REQUIRE(!bytes.empty());
    State restored;
    std::string error;
    REQUIRE(cat::memory::Parse(bytes, restored, error));
    REQUIRE(error.empty());
    REQUIRE(cat::memory::Serialize(restored) == bytes);
    state = restored;
}
cat::routine::Context Context(std::int64_t now, double hunger = 55) {
    cat::routine::Context context;
    context.nowUtc = now;
    context.localDay = now / day;
    context.localHour = cat::FromUnix(now).hour;
    context.busy = false;
    context.cursorReachable = true;
    context.canRequestFood = hunger <= 35;
    context.canRequestTreat = hunger > 35 && hunger <= 65;
    context.hunger = hunger;
    return context;
}
void RecordCare(State& state, Event event, std::int64_t now) {
    const auto previous = Shifts(state);
    const auto learnedAt = state.lastLearningUtc;
    REQUIRE(cat::memory::Record(state, event, now));
    const auto current = Shifts(state);
    for (std::size_t i = 0; i < current.size(); ++i) {
        REQUIRE(std::abs(current[i]) <= .15);
        REQUIRE(std::abs(current[i] - previous[i]) <= .00150000001);
    }
    if (state.lastLearningUtc != learnedAt)
        REQUIRE(state.lastLearningUtc - learnedAt >= day || state.learningSamples == 1);
    REQUIRE(state.bond >= 0 && state.bond <= 100);
}

void TenCalendarYearsWithRestarts() {
    auto uninterrupted = cat::memory::FromLegacy(cat::NewPet(born), born);
    auto resumed = uninterrupted;
    const auto end = cat::Anniversary(born, 10);
    const auto civilEnd = cat::FromUnix(end);
    REQUIRE(civilEnd.year == 2030 && civilEnd.month == 2 && civilEnd.day == 28);
    std::mt19937 random(seed);
    std::array<std::uint64_t, 4> accepted{};
    int leapBirthdays = 0, checkpoints = 0, routineActions = 0;
    std::array<bool, 3> rareSeen{};
    cat::behavior::Brain birthBrain(born, seed);
    const auto birthTraits = birthBrain.GetBirthPersonality();
    for (auto date = born; date < end; date += day) {
        const auto civil = cat::FromUnix(date);
        if (civil.month == 2 && civil.day == 29) ++leapBirthdays;
        const auto beforeDay = Shifts(uninterrupted);
        // Accepted food is supplied by the caller's nutrition gate. Six events
        // spaced three hours apart keep this scenario below reward/care caps.
        std::array<Event, 6> events{Event::Meal, Event::Treat, Event::Pet, Event::Play,
            static_cast<Event>(random() % 4), static_cast<Event>(random() % 4)};
        for (std::size_t i = 0; i < events.size(); ++i) {
            const auto now = date + (8 + static_cast<int>(i) * 3) * 3600;
            RecordCare(uninterrupted, events[i], now);
            RecordCare(resumed, events[i], now);
            ++accepted[static_cast<std::size_t>(events[i])];
            auto context = Context(now + 300, i == 1 ? 30 : 55);
            const double choice = (random() % 1000) / 1000.0;
            const auto a = cat::routine::Peek(uninterrupted, context, choice);
            const auto b = cat::routine::Peek(resumed, context, choice);
            REQUIRE(a == b);
            if (a != Decision::None) {
                REQUIRE(cat::routine::Commit(uninterrupted, context, a));
                REQUIRE(cat::routine::Commit(resumed, context, b));
                ++routineActions;
                if (a == Decision::RareKnead) {
                    REQUIRE(context.nowUtc - born >= 90 * day); rareSeen[0] = true;
                }
                if (a == Decision::RareGreeting) {
                    REQUIRE(context.nowUtc >= cat::Anniversary(born, 1)); rareSeen[1] = true;
                }
                if (a == Decision::RareBond) {
                    REQUIRE(context.nowUtc >= cat::Anniversary(born, 5)); rareSeen[2] = true;
                }
            }
        }
        const auto afterDay = Shifts(uninterrupted);
        for (std::size_t i = 0; i < afterDay.size(); ++i)
            REQUIRE(std::abs(afterDay[i] - beforeDay[i]) <= .00150000001);
        REQUIRE(cat::memory::Valid(uninterrupted));
        REQUIRE(cat::memory::Serialize(uninterrupted) == cat::memory::Serialize(resumed));
        // Restart monthly-ish, on leap dates, and across every anniversary.
        if (((date - born) / day) % 37 == 0 || (civil.month == 2 && civil.day >= 28)) {
            Restart(resumed); ++checkpoints;
            cat::behavior::Brain live(born, seed), restarted(born, seed);
            live.SetExperience(Experience(uninterrupted));
            restarted.SetExperience(Experience(resumed));
            REQUIRE(SameTraits(live.GetBirthPersonality(), birthTraits));
            REQUIRE(SameTraits(restarted.GetBirthPersonality(), birthTraits));
            REQUIRE(SameTraits(live.GetPersonality(), restarted.GetPersonality()));
            REQUIRE(live.GetArchetype() == restarted.GetArchetype());
            const cat::behavior::Context behavior{1000000, cat::Age(born, date).years,
                                                 55, 85, 16, true, false};
            const auto livePlan = live.Next(behavior), resumedPlan = restarted.Next(behavior);
            REQUIRE(livePlan.action == resumedPlan.action);
            REQUIRE(livePlan.durationMs == resumedPlan.durationMs && livePlan.speed == resumedPlan.speed);
        }
    }
    REQUIRE(leapBirthdays == 3); // 2020, 2024 and 2028, not ten 365-day years.
    REQUIRE(checkpoints > 100);
    REQUIRE(routineActions > 0);
    REQUIRE(rareSeen[0] && rareSeen[1] && rareSeen[2]);
    REQUIRE(uninterrupted.bond == 100);
    REQUIRE(uninterrupted.meals == accepted[0] && uninterrupted.treats == accepted[1]);
    REQUIRE(uninterrupted.pets == accepted[2] && uninterrupted.plays == accepted[3]);
    REQUIRE(uninterrupted.bornUtc == born && resumed.bornUtc == born);
    REQUIRE(cat::Age(born, end).years == 10);
    Restart(resumed);
}

void OfflineTimeEarnsNothing() {
    auto memory = cat::memory::FromLegacy(cat::NewPet(born), born);
    for (int date = 0; date < 30; ++date) {
        RecordCare(memory, Event::Pet, born + date * day + 8 * 3600);
        RecordCare(memory, Event::Play, born + date * day + 12 * 3600);
    }
    auto expected = memory;
    const auto returnDate = cat::Anniversary(born, 10);
    cat::memory::Advance(memory, returnDate);
    expected.updatedUtc = returnDate;
    REQUIRE(cat::memory::Serialize(memory) == cat::memory::Serialize(expected));
    Restart(memory);
    const auto before = memory;
    RecordCare(memory, Event::Pet, returnDate + 3600);
    REQUIRE(memory.pets == before.pets + 1);
    REQUIRE(memory.learningSamples == before.learningSamples + 1);
    REQUIRE(std::abs(memory.bond - before.bond - .07) < 1e-10);
    REQUIRE(memory.plays == before.plays && memory.meals == before.meals && memory.treats == before.treats);
}

void CareStylesDivergeGradually() {
    auto social = cat::memory::FromLegacy(cat::NewPet(born), born);
    auto treats = social;
    cat::behavior::Brain socialBrain(born, seed), treatBrain(born, seed);
    const auto birthTraits = socialBrain.GetBirthPersonality();
    const auto birthClass = socialBrain.GetArchetype();
    for (int date = 0; date < 365; ++date) {
        const auto beforeSocial = Shifts(social), beforeTreats = Shifts(treats);
        for (int slot = 0; slot < 6; ++slot) {
            const auto now = born + date * day + (7 + slot * 3) * 3600;
            RecordCare(social, slot % 2 ? Event::Play : Event::Pet, now);
            RecordCare(treats, slot % 2 ? Event::Meal : Event::Treat, now);
        }
        socialBrain.SetExperience(Experience(social));
        treatBrain.SetExperience(Experience(treats));
        REQUIRE(SameTraits(socialBrain.GetBirthPersonality(), birthTraits));
        REQUIRE(SameTraits(treatBrain.GetBirthPersonality(), birthTraits));
        for (std::size_t i = 0; i < beforeSocial.size(); ++i) {
            REQUIRE(std::abs(Shifts(social)[i] - beforeSocial[i]) <= .00150000001);
            REQUIRE(std::abs(Shifts(treats)[i] - beforeTreats[i]) <= .00150000001);
        }
        if (date == 0) {
            REQUIRE(SameTraits(socialBrain.GetPersonality(), birthTraits));
            REQUIRE(SameTraits(treatBrain.GetPersonality(), birthTraits));
            REQUIRE(socialBrain.GetArchetype() == birthClass && treatBrain.GetArchetype() == birthClass);
        }
        if (date % 31 == 0) { Restart(social); Restart(treats); }
    }
    REQUIRE(social.sociabilityShift - treats.sociabilityShift > .08);
    REQUIRE(social.energyShift - treats.energyShift > .05);
    REQUIRE(socialBrain.GetPersonality().sociability > treatBrain.GetPersonality().sociability);
    REQUIRE(socialBrain.GetPersonality().energy > treatBrain.GetPersonality().energy);
    REQUIRE(treats.treatPreference > social.treatPreference);
    REQUIRE(social.bornUtc == treats.bornUtc);
}

void RoutineGuardsAndExactAgeThresholds() {
    auto memory = cat::memory::FromLegacy(cat::NewPet(born), born);
    memory.bond = 100;
    memory.treatPreference = .8;
    const std::array<Decision, 9> decisions{Decision::MorningStretch, Decision::LunchRequest,
        Decision::DawnZoomies, Decision::Approach, Decision::TreatRequest,
        Decision::RareKnead, Decision::RareGreeting, Decision::RareBond, Decision::None};
    for (auto now : {born + 89 * day, cat::Anniversary(born, 1) - 1,
                     cat::Anniversary(born, 5) - 1, cat::Anniversary(born, 10)}) {
        auto context = Context(now);
        context.busy = true;
        const auto original = cat::memory::Serialize(memory);
        REQUIRE(cat::routine::Peek(memory, context) == Decision::None);
        for (auto decision : decisions)
            REQUIRE(!cat::routine::Commit(memory, context, decision));
        REQUIRE(cat::memory::Serialize(memory) == original);
        context.busy = false;
        context.roamingPaused = true;
        REQUIRE(!cat::routine::Eligible(memory, context, Decision::Approach));
        REQUIRE(!cat::routine::Eligible(memory, context, Decision::DawnZoomies));
    }
    // Search deterministic eligible slots after each threshold, and every slot
    // for the preceding day. Same birthday prevents changing opportunity seeds.
    const std::array<Decision, 3> rare{Decision::RareKnead, Decision::RareGreeting, Decision::RareBond};
    const std::array<std::int64_t, 3> threshold{born + 90 * day,
        cat::Anniversary(born, 1), cat::Anniversary(born, 5)};
    for (std::size_t i = 0; i < rare.size(); ++i) {
        for (auto now = threshold[i] - day; now < threshold[i]; now += 300)
            REQUIRE(!cat::routine::Eligible(memory, Context(now), rare[i]));
        REQUIRE(!cat::routine::Eligible(memory, Context(threshold[i] - 1), rare[i]));
        bool unlocked = false;
        for (auto now = threshold[i]; now < threshold[i] + 7 * day; now += 300) {
            const auto context = Context(now);
            if (!cat::routine::Eligible(memory, context, rare[i])) continue;
            auto restarted = memory; Restart(restarted);
            REQUIRE(cat::routine::Eligible(restarted, context, rare[i]));
            REQUIRE(cat::routine::Commit(restarted, context, rare[i]));
            REQUIRE(!cat::routine::Commit(restarted, context, rare[i]));
            unlocked = true; break;
        }
        REQUIRE(unlocked);
    }
}
} // namespace

int main() {
    try {
        TenCalendarYearsWithRestarts();
        OfflineTimeEarnsNothing();
        CareStylesDivergeGradually();
        RoutineGuardsAndExactAgeThresholds();
        std::cout << "Memory lifetime: 4 scenarios passed (10 calendar years, restarts, offline absence, care styles and routine gates)\n";
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
