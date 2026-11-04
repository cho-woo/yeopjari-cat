#pragma once

#include "cat_memory.hpp"
#include "cat_nutrition.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace cat::routine {

enum class Decision {
    None, MorningStretch, LunchRequest, DawnZoomies, Approach, TreatRequest,
    RareKnead, RareGreeting, RareBond
};

// All food-request checks operate on a candidate first. In particular, a treat
// timestamp may not be newer than nutrition.updatedUtc: otherwise the record
// becomes invalid and future feeding is refused by the nutrition model.
inline bool CanRequestFood(const nutrition::State& state, std::int64_t nowUtc) noexcept {
    if (!nutrition::IsValid(state) || nowUtc < state.updatedUtc || nowUtc > nutrition::MaximumUtc)
        return false;
    auto candidate = state;
    return nutrition::NeedsFoodRequest(candidate, nowUtc);
}

inline bool CanRequestTreat(const nutrition::State& state, std::int64_t nowUtc) noexcept {
    if (!nutrition::IsValid(state) || nowUtc < state.updatedUtc || nowUtc > nutrition::MaximumUtc)
        return false;
    auto candidate = state;
    nutrition::Advance(candidate, nowUtc);
    const auto previous = std::max({candidate.lastRequestUtc, candidate.lastMealUtc, candidate.lastTreatUtc});
    return candidate.fullness > 35 && candidate.fullness <= 65 &&
        (!previous || candidate.updatedUtc - previous >= nutrition::RequestCooldown);
}

inline bool AcceptRequest(nutrition::State& state, Decision decision, std::int64_t nowUtc) noexcept {
    if (!nutrition::IsValid(state) || nowUtc < state.updatedUtc || nowUtc > nutrition::MaximumUtc)
        return false;
    auto candidate = state;
    if (decision == Decision::LunchRequest) {
        if (!CanRequestFood(state, nowUtc)) return false;
        if (!nutrition::NeedsFoodRequest(candidate, nowUtc)) return false;
    } else if (decision == Decision::TreatRequest) {
        if (!CanRequestTreat(state, nowUtc)) return false;
        nutrition::Advance(candidate, nowUtc);
        candidate.lastRequestUtc = candidate.updatedUtc;
    } else return false;
    if (!nutrition::IsValid(candidate)) return false;
    state = candidate;
    return true;
}

struct Context {
    std::int64_t nowUtc = 0, localDay = 0;
    int localHour = 12;
    // The caller includes hidden, sleeping, menus, dragging, eating, airborne,
    // bubbles, pending manual care and other unsuitable states in busy.
    bool busy = true, roamingPaused = false, cursorReachable = false;
    // Food uses a copy of nutrition::NeedsFoodRequest to avoid consuming its
    // cooldown before selection. Treat additionally needs the same 2h global
    // request/feed interval; unlike food, it may ask at fullness 35..65.
    bool canRequestFood = false, canRequestTreat = false;
    double hunger = 88;
};

inline constexpr std::int64_t SharedGap = 120;
inline constexpr std::int64_t DailyGap = 20 * 3600;
inline constexpr std::int64_t ApproachGap = 30 * 60;
inline constexpr std::int64_t TreatGap = 6 * 3600;
inline constexpr std::int64_t RareGap = 12 * 3600;

inline bool Due(std::int64_t now, std::int64_t previous, std::int64_t gap) {
    return previous == 0 || (now >= previous && now - previous >= gap);
}

inline std::uint64_t Mix(std::uint64_t value) {
    value += 0x9e3779b97f4a7c15ULL;
    value = (value ^ (value >> 30)) * 0xbf58476d1ce4e5b9ULL;
    value = (value ^ (value >> 27)) * 0x94d049bb133111ebULL;
    return value ^ (value >> 31);
}

// A stable opportunity per five-minute slot avoids per-frame/restart farming.
// It uses no process RNG, wall-clock adjustment or persisted invented care.
inline double Opportunity(const memory::State& state, std::int64_t slot, std::uint64_t salt) {
    const auto bits = Mix(static_cast<std::uint64_t>(state.bornUtc) ^
        Mix(static_cast<std::uint64_t>(slot)) ^ salt);
    return static_cast<double>(bits >> 11) * (1.0 / 9007199254740992.0);
}

inline bool Calm(const memory::State& state, const Context& context) {
    if (context.busy || context.nowUtc < state.updatedUtc || context.nowUtc < state.bornUtc ||
        context.nowUtc > 32503680000LL || context.localHour < 0 || context.localHour >= 24 ||
        !std::isfinite(context.hunger) || context.hunger < 0 || context.hunger > 100) return false;
    // Match the persistent memory model: real civil time zones differ from
    // the UTC date by at most one day. Accepted commits must remain serializable.
    const auto utcDay = context.nowUtc / 86400;
    if (context.localDay < utcDay - 1 || context.localDay > utcDay + 1) return false;
    return Due(context.nowUtc, state.lastRoutineUtc, SharedGap);
}

inline bool DailyReady(const Context& context, std::int64_t lastDay, std::int64_t lastUtc) {
    return context.localDay > lastDay && Due(context.nowUtc, lastUtc, DailyGap);
}

inline bool Eligible(const memory::State& state, const Context& context, Decision decision) {
    if (decision == Decision::None || !Calm(state, context)) return false;
    const auto profile = memory::Summarize(state);
    const auto now = context.nowUtc;
    const auto slot = now / 300;
    switch (decision) {
    case Decision::MorningStretch:
        return context.localHour >= 7 && context.localHour < 10 &&
            DailyReady(context, state.lastMorningDay, state.lastMorningUtc);
    case Decision::LunchRequest:
        return context.localHour >= 11 && context.localHour < 14 &&
            context.canRequestFood && context.hunger <= 35 &&
            DailyReady(context, state.lastLunchDay, state.lastLunchUtc);
    case Decision::DawnZoomies:
        return context.localHour >= 2 && context.localHour < 5 &&
            !context.roamingPaused && context.hunger > 35 &&
            DailyReady(context, state.lastDawnDay, state.lastDawnUtc) &&
            Opportunity(state, context.localDay, 0xa4da9b75ULL) < .25;
    case Decision::Approach:
        return !context.roamingPaused && context.cursorReachable && profile.bond >= 35 &&
            Due(now, state.lastApproachUtc, ApproachGap) &&
            Opportunity(state, slot, 0x6a30a9ULL) < .02 + .10 * profile.bond / 100.0;
    case Decision::TreatRequest:
        return context.canRequestTreat && context.hunger > 35 && context.hunger <= 65 &&
            profile.treatPreference >= .25 && Due(now, state.lastTreatRequestUtc, TreatGap) &&
            Opportunity(state, slot, 0x7ea75ULL) < .03 + .17 * profile.treatPreference;
    case Decision::RareKnead:
    case Decision::RareGreeting:
    case Decision::RareBond: {
        if (context.hunger <= 35 || !Due(now, state.lastRareUtc, RareGap) ||
            Opportunity(state, slot, 0x9aeeULL) >= .03) return false;
        if (decision == Decision::RareKnead)
            return now - state.bornUtc >= 90LL * 86400 && profile.bond >= 25;
        if (decision == Decision::RareGreeting)
            return now >= cat::Anniversary(state.bornUtc, 1) && profile.bond >= 45;
        return now >= cat::Anniversary(state.bornUtc, 5) && profile.bond >= 65;
    }
    case Decision::None: return false;
    }
    return false;
}

inline Decision Peek(const memory::State& state, const Context& context, double random01 = .5) {
    for (auto choice : {Decision::MorningStretch, Decision::LunchRequest, Decision::DawnZoomies,
                        Decision::TreatRequest})
        if (Eligible(state, context, choice)) return choice;
    // Randomness selects among already-unlocked rare variants. Eligibility and
    // frequency are stable for the entire slot, independent of render cadence.
    Decision rare[3]; int count = 0;
    for (auto choice : {Decision::RareKnead, Decision::RareGreeting, Decision::RareBond})
        if (Eligible(state, context, choice)) rare[count++] = choice;
    if (count) {
        const double sample = std::isfinite(random01) ? std::clamp(random01, 0.0, 1.0) : .5;
        return rare[std::min(count - 1, static_cast<int>(sample * count))];
    }
    return Eligible(state, context, Decision::Approach) ? Decision::Approach : Decision::None;
}

// Call only after the corresponding action/bubble has actually been accepted.
// A failed/abandoned Peek never consumes a once-per-day opportunity. Rechecking
// conditions also protects callers that defer execution until a later frame.
inline bool Commit(memory::State& state, const Context& context, Decision decision) {
    if (!Eligible(state, context, decision)) return false;
    memory::Advance(state, context.nowUtc);
    state.lastRoutineUtc = context.nowUtc;
    switch (decision) {
    case Decision::MorningStretch:
        state.lastMorningDay = context.localDay; state.lastMorningUtc = context.nowUtc; break;
    case Decision::LunchRequest:
        state.lastLunchDay = context.localDay; state.lastLunchUtc = context.nowUtc; break;
    case Decision::DawnZoomies:
        state.lastDawnDay = context.localDay; state.lastDawnUtc = context.nowUtc; break;
    case Decision::Approach: state.lastApproachUtc = context.nowUtc; break;
    case Decision::TreatRequest: state.lastTreatRequestUtc = context.nowUtc; break;
    case Decision::RareKnead:
    case Decision::RareGreeting:
    case Decision::RareBond: state.lastRareUtc = context.nowUtc; break;
    case Decision::None: return false;
    }
    return true;
}

} // namespace cat::routine
