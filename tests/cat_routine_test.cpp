#include "../src/cat_routine.hpp"
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {
int checks = 0;
#define CHECK(value) do { ++checks; if (!(value)) throw std::runtime_error("routine check failed at line " + std::to_string(__LINE__)); } while (false)
using cat::routine::Context;
using cat::routine::Decision;
using cat::routine::Eligible;
using cat::routine::Peek;
using cat::routine::Commit;

constexpr std::int64_t day = 86400;
std::int64_t Utc(int year, unsigned month, unsigned date, int hour = 12) {
    return cat::ToUnix({year, month, date, hour});
}
cat::memory::State State(std::int64_t born = Utc(2020, 2, 29), std::int64_t now = Utc(2026, 11, 4)) {
    auto pet = cat::NewPet(born);
    pet.updatedUtc = now;
    auto result = cat::memory::FromLegacy(pet, now);
    result.bond = 80;
    return result;
}
Context Quiet(std::int64_t now = Utc(2026, 11, 4), int hour = 12) {
    Context result;
    result.nowUtc = now; result.localDay = now / day; result.localHour = hour;
    result.busy = false; result.hunger = 55;
    return result;
}
Context Opportunity(const cat::memory::State& state, Decision decision, Context context) {
    for (int slot = 0; slot < 20000; ++slot) {
        if (Eligible(state, context, decision)) return context;
        context.nowUtc += 300;
        context.localDay = context.nowUtc / day;
    }
    throw std::runtime_error("could not find deterministic opportunity");
}
void DailyLifeAndCommit() {
    auto state = State(Utc(2020, 1, 1), Utc(2026, 11, 4, 8));
    auto morning = Quiet(state.updatedUtc, 8);
    CHECK(Peek(state, morning) == Decision::MorningStretch);
    const auto original = cat::memory::Serialize(state);
    CHECK(Peek(state, morning) == Decision::MorningStretch);
    CHECK(cat::memory::Serialize(state) == original); // selecting is read-only
    auto busy = morning; busy.busy = true;
    CHECK(!Commit(state, busy, Decision::MorningStretch));
    CHECK(cat::memory::Serialize(state) == original);
    CHECK(Commit(state, morning, Decision::MorningStretch));
    CHECK(!Commit(state, morning, Decision::MorningStretch));
    auto lunch = Quiet(Utc(2026, 11, 4, 12), 12);
    lunch.canRequestFood = true; lunch.hunger = 30;
    CHECK(Peek(state, lunch) == Decision::LunchRequest);
    CHECK(Commit(state, lunch, Decision::LunchRequest));
    CHECK(state.lastMorningDay == state.lastLunchDay);
    CHECK(state.lastMorningUtc != state.lastLunchUtc);
    auto tomorrow = Quiet(Utc(2026, 11, 5, 8), 8);
    CHECK(Commit(state, tomorrow, Decision::MorningStretch));
}
void RestartRollbackAndTimezone() {
    auto state = State(Utc(2020, 1, 1), Utc(2026, 11, 4, 8));
    auto context = Quiet(state.updatedUtc, 8);
    CHECK(Commit(state, context, Decision::MorningStretch));
    cat::memory::State restored;
    std::string error;
    CHECK(cat::memory::Parse(cat::memory::Serialize(state), restored, error));
    CHECK(!Eligible(restored, context, Decision::MorningStretch));
    // Date line / timezone changes cannot replay an event an hour later.
    context.nowUtc += 3600; ++context.localDay;
    CHECK(!Eligible(restored, context, Decision::MorningStretch));
    context.nowUtc = restored.updatedUtc - 3600;
    CHECK(Peek(restored, context) == Decision::None);
    const auto before = cat::memory::Serialize(restored);
    CHECK(!Commit(restored, context, Decision::MorningStretch));
    CHECK(cat::memory::Serialize(restored) == before);
    context = Quiet(Utc(2026, 11, 5, 7), 8); // DST: 23-hour day is valid.
    CHECK(Eligible(restored, context, Decision::MorningStretch));
    context.localDay += 100;
    CHECK(Peek(restored, context) == Decision::None);
}
void BusyPausedAndHunger() {
    auto state = State();
    auto context = Quiet();
    context.cursorReachable = true;
    context = Opportunity(state, Decision::Approach, context);
    CHECK(Eligible(state, context, Decision::Approach));
    auto guarded = context; guarded.busy = true;
    CHECK(Peek(state, guarded) == Decision::None);
    guarded = context; guarded.roamingPaused = true;
    CHECK(!Eligible(state, guarded, Decision::Approach));
    guarded = context; guarded.cursorReachable = false;
    CHECK(!Eligible(state, guarded, Decision::Approach));
    state.bond = 34.999;
    CHECK(!Eligible(state, context, Decision::Approach));
    state.bond = 80;
    CHECK(Commit(state, context, Decision::Approach));
    CHECK(!Eligible(state, context, Decision::Approach));
    context.nowUtc += 1799;
    context.localDay = context.nowUtc / day;
    CHECK(!Eligible(state, context, Decision::Approach));
    auto lunch = Quiet(context.nowUtc, 12);
    lunch.canRequestFood = true;
    for (double hunger : {36.0, 65.0, 100.0, -1.0, double(INFINITY), double(NAN)}) {
        lunch.hunger = hunger;
        CHECK(!Eligible(state, lunch, Decision::LunchRequest));
    }
    lunch.hunger = 35;
    CHECK(Eligible(state, lunch, Decision::LunchRequest));
    lunch.canRequestFood = false;
    CHECK(!Eligible(state, lunch, Decision::LunchRequest));
}
void DawnIsOccasionalAndDurable() {
    auto state = State(Utc(2020, 1, 1), Utc(2026, 1, 1, 3));
    int daysWithZoomies = 0;
    for (int i = 0; i < 400; ++i) {
        auto context = Quiet(Utc(2026, 1, 1, 3) + i * day, 3);
        const bool offered = Eligible(state, context, Decision::DawnZoomies);
        for (int frame = 0; frame < 240; ++frame)
            CHECK(Eligible(state, context, Decision::DawnZoomies) == offered);
        if (!offered) continue;
        ++daysWithZoomies;
        auto paused = context; paused.roamingPaused = true;
        CHECK(!Eligible(state, paused, Decision::DawnZoomies));
        auto hungry = context; hungry.hunger = 35;
        CHECK(!Eligible(state, hungry, Decision::DawnZoomies));
        CHECK(Commit(state, context, Decision::DawnZoomies));
        CHECK(!Eligible(state, context, Decision::DawnZoomies));
    }
    CHECK(daysWithZoomies > 60 && daysWithZoomies < 140);
}
void LearnedTreatRequests() {
    auto inexperienced = State();
    auto experienced = inexperienced;
    inexperienced.treatPreference = .25;
    experienced.treatPreference = .9;
    int low = 0, high = 0;
    auto context = Quiet(inexperienced.updatedUtc, 16);
    context.canRequestTreat = true;
    for (int slot = 0; slot < 10000; ++slot) {
        const bool a = Eligible(inexperienced, context, Decision::TreatRequest);
        const bool b = Eligible(experienced, context, Decision::TreatRequest);
        CHECK(!a || b); // same cat/slot: more treat memory cannot reduce chance
        low += a; high += b;
        context.nowUtc += 300; context.localDay = context.nowUtc / day;
    }
    CHECK(low > 500 && high > low * 2 && high < 2300);
    context = Quiet(experienced.updatedUtc, 16);
    context.canRequestTreat = true;
    context = Opportunity(experienced, Decision::TreatRequest, context);
    for (double fullness : {20.0, 35.0, 65.1, 100.0}) {
        auto unsuitable = context; unsuitable.hunger = fullness;
        CHECK(!Eligible(experienced, unsuitable, Decision::TreatRequest));
    }
    auto noCooldown = context; noCooldown.canRequestTreat = false;
    CHECK(!Eligible(experienced, noCooldown, Decision::TreatRequest));
    CHECK(Commit(experienced, context, Decision::TreatRequest));
    cat::memory::State restored; std::string error;
    CHECK(cat::memory::Parse(cat::memory::Serialize(experienced), restored, error));
    for (int seconds = 0; seconds < 6 * 3600; seconds += 60) {
        auto later = context; later.nowUtc += seconds; later.localDay = later.nowUtc / day;
        CHECK(!Eligible(restored, later, Decision::TreatRequest));
    }
    auto lowBond = State(), highBond = lowBond;
    lowBond.bond = 35; highBond.bond = 100;
    context = Quiet(lowBond.updatedUtc, 16); context.cursorReachable = true;
    low = high = 0;
    for (int slot = 0; slot < 10000; ++slot) {
        const bool a = Eligible(lowBond, context, Decision::Approach);
        const bool b = Eligible(highBond, context, Decision::Approach);
        CHECK(!a || b); low += a; high += b;
        context.nowUtc += 300; context.localDay = context.nowUtc / day;
    }
    CHECK(low > 350 && high > low * 1.5);
}

void NutritionRequestsStayValid() {
    const auto now = Utc(2026, 11, 4, 12);
    auto state = cat::nutrition::NewState(Utc(2020, 1, 1), now - 1, 55);
    const auto before = cat::nutrition::Serialize(state);
    CHECK(cat::routine::CanRequestTreat(state, now));
    CHECK(cat::nutrition::Serialize(state) == before);
    // Reproduce the integration defect: writing the request before Advance
    // creates an invalid record even with only one second of elapsed time.
    auto broken = state; broken.lastRequestUtc = now;
    CHECK(!cat::nutrition::IsValid(broken));
    CHECK(cat::routine::AcceptRequest(state, Decision::TreatRequest, now));
    CHECK(state.updatedUtc == now && state.lastRequestUtc == now);
    CHECK(cat::nutrition::IsValid(state));
    cat::nutrition::State restored; std::string error;
    CHECK(cat::nutrition::Parse(cat::nutrition::Serialize(state), restored, error));
    CHECK(cat::nutrition::TryFeed(restored, now + 1, true, 1) == cat::nutrition::FeedResult::Accepted);
    CHECK(cat::nutrition::IsValid(restored));
    const auto after = cat::nutrition::Serialize(state);
    CHECK(!cat::routine::AcceptRequest(state, Decision::TreatRequest, now + 60));
    CHECK(cat::nutrition::Serialize(state) == after);
    for (double fullness : {20.0, 35.0, 66.0, 99.0}) {
        auto unsuitable = cat::nutrition::NewState(Utc(2020, 1, 1), now - 1, fullness);
        const auto saved = cat::nutrition::Serialize(unsuitable);
        CHECK(!cat::routine::CanRequestTreat(unsuitable, now));
        CHECK(!cat::routine::AcceptRequest(unsuitable, Decision::TreatRequest, now));
        CHECK(cat::nutrition::Serialize(unsuitable) == saved);
    }
    for (int timestamp = 0; timestamp < 3; ++timestamp) {
        auto recent = cat::nutrition::NewState(Utc(2020, 1, 1), now - 1, 55);
        if (timestamp == 0) recent.lastMealUtc = now - 60;
        if (timestamp == 1) recent.lastTreatUtc = now - 60;
        if (timestamp == 2) recent.lastRequestUtc = now - 60;
        const auto saved = cat::nutrition::Serialize(recent);
        CHECK(!cat::routine::AcceptRequest(recent, Decision::TreatRequest, now));
        CHECK(cat::nutrition::Serialize(recent) == saved);
    }
    for (auto decision : {Decision::LunchRequest, Decision::TreatRequest}) {
        auto future = cat::nutrition::NewState(Utc(2020, 1, 1), now + 1, 30);
        const auto saved = cat::nutrition::Serialize(future);
        CHECK(!cat::routine::AcceptRequest(future, decision, now));
        CHECK(cat::nutrition::Serialize(future) == saved);
    }
    auto lunch = cat::nutrition::NewState(Utc(2020, 1, 1), now - 1, 30);
    CHECK(cat::routine::AcceptRequest(lunch, Decision::LunchRequest, now));
    CHECK(lunch.updatedUtc == now && lunch.lastRequestUtc == now);
    CHECK(cat::nutrition::IsValid(lunch));
    auto healthy = cat::nutrition::NewState(Utc(2020, 1, 1), now - 1, 60);
    const auto saved = cat::nutrition::Serialize(healthy);
    CHECK(!cat::routine::AcceptRequest(healthy, Decision::LunchRequest, now));
    CHECK(cat::nutrition::Serialize(healthy) == saved);
    CHECK(!cat::routine::AcceptRequest(healthy, Decision::MorningStretch, now));
    CHECK(cat::nutrition::Serialize(healthy) == saved);
}
void FoodPreflightAndAcceptanceAgree() {
    const auto now = Utc(2026, 11, 4, 12);
    const auto born = Utc(2020, 1, 1);
    auto hungry = cat::nutrition::NewState(born, now - 1, 30);
    const auto original = cat::nutrition::Serialize(hungry);
    CHECK(cat::routine::CanRequestFood(hungry, now));
    CHECK(cat::nutrition::Serialize(hungry) == original);
    CHECK(cat::routine::AcceptRequest(hungry, Decision::LunchRequest, now));
    CHECK(cat::nutrition::IsValid(hungry));
    CHECK(!cat::routine::CanRequestFood(hungry, now));
    const auto unchanged = [](const cat::nutrition::State& a, const cat::nutrition::State& b) {
        return a.bornUtc == b.bornUtc && a.updatedUtc == b.updatedUtc &&
            a.bodyCondition == b.bodyCondition && a.fullness == b.fullness &&
            a.lastMealUtc == b.lastMealUtc && a.lastTreatUtc == b.lastTreatUtc &&
            a.lastRequestUtc == b.lastRequestUtc;
    };
    for (int scenario = 0; scenario < 6; ++scenario) {
        auto state = cat::nutrition::NewState(born, now, 30);
        std::int64_t requestUtc = now;
        if (scenario == 0) requestUtc = now - 1; // clock rollback
        if (scenario == 1) state.updatedUtc = now + 1; // future saved state
        if (scenario == 2) requestUtc = cat::nutrition::MaximumUtc + 1;
        if (scenario == 3) state.lastRequestUtc = now + 1; // invalid chronology
        if (scenario == 4) state.fullness = 10; // invalid persisted numeric range
        if (scenario == 5) state.lastMealUtc = now - 60; // normal cooldown
        const auto before = state;
        CHECK(!cat::routine::CanRequestFood(state, requestUtc));
        CHECK(unchanged(state, before));
        CHECK(!cat::routine::AcceptRequest(state, Decision::LunchRequest, requestUtc));
        CHECK(unchanged(state, before));
    }
}

void AllAcceptedDaysRemainSerializable() {
    for (int offset = -3; offset <= 3; ++offset) {
        auto state = State(Utc(2020, 1, 1), Utc(2026, 11, 4, 8));
        auto context = Quiet(state.updatedUtc, 8);
        context.localDay += offset;
        const auto saved = cat::memory::Serialize(state);
        const bool accepted = Commit(state, context, Decision::MorningStretch);
        CHECK(accepted == (std::abs(offset) <= 1));
        CHECK(cat::memory::Valid(state));
        if (!accepted) CHECK(cat::memory::Serialize(state) == saved);
        cat::memory::State restored; std::string error;
        CHECK(cat::memory::Parse(cat::memory::Serialize(state), restored, error));
    }
}

void RareAgeAndRate() {
    const auto born = Utc(2024, 2, 29);
    auto state = State(born, born + 89 * day);
    auto context = Quiet(state.updatedUtc, 16);
    CHECK(!Eligible(state, context, Decision::RareKnead));
    // Search the opportunity with an old control, then test age boundaries at
    // that same slot (changing birth would change the deterministic draw).
    context.nowUtc = born + 90 * day;
    context.localDay = context.nowUtc / day;
    context = Opportunity(state, Decision::RareKnead, context);
    CHECK(Eligible(state, context, Decision::RareKnead));
    CHECK(!Eligible(state, context, Decision::RareGreeting));
    CHECK(!Eligible(state, context, Decision::RareBond));
    state.bond = 24.99;
    CHECK(!Eligible(state, context, Decision::RareKnead));
    state.bond = 80;
    CHECK(Commit(state, context, Decision::RareKnead));
    for (int seconds = 0; seconds < 12 * 3600; seconds += 60) {
        auto later = context; later.nowUtc += seconds; later.localDay = later.nowUtc / day;
        CHECK(!Eligible(state, later, Decision::RareKnead));
    }
    context = Quiet(cat::Anniversary(born, 1), 16);
    context = Opportunity(state, Decision::RareGreeting, context);
    CHECK(Eligible(state, context, Decision::RareGreeting));
    CHECK(!Eligible(state, context, Decision::RareBond));
    context = Quiet(cat::Anniversary(born, 5), 16);
    context = Opportunity(state, Decision::RareBond, context);
    CHECK(Eligible(state, context, Decision::RareBond));
    CHECK(Commit(state, context, Decision::RareBond));
    CHECK(!Eligible(state, context, Decision::RareGreeting));
    CHECK(!Eligible(state, context, Decision::RareKnead));
}
}
int main() {
    try {
        DailyLifeAndCommit(); RestartRollbackAndTimezone(); BusyPausedAndHunger();
        DawnIsOccasionalAndDurable(); LearnedTreatRequests(); RareAgeAndRate();
        NutritionRequestsStayValid(); FoodPreflightAndAcceptanceAgree(); AllAcceptedDaysRemainSerializable();
        std::cout << "Bond routines: " << checks << " checks passed\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n'; return 1;
    }
}
