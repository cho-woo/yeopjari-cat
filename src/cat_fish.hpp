#pragma once

#include "cat_memory.hpp"
#include "cat_nutrition.hpp"
#include "cat_preferences.hpp"

namespace cat::feeding {

// Presentation is transient. Fish uses the established meal timestamps and
// lifetime counters; no new field or save-format migration is needed.
enum class FoodKind { Meal, Treat, Fish };

inline constexpr bool IsValid(FoodKind kind) noexcept {
    return kind == FoodKind::Meal || kind == FoodKind::Treat || kind == FoodKind::Fish;
}
inline constexpr bool IsTreat(FoodKind kind) noexcept { return kind == FoodKind::Treat; }
inline bool CanOfferFish(cat::Character character) noexcept {
    return cat::IsValid(character) && character != cat::Character::Dog && character != cat::Character::Rabbit;
}
inline constexpr cat::memory::Event MemoryEvent(FoodKind kind) noexcept {
    return IsTreat(kind) ? cat::memory::Event::Treat : cat::memory::Event::Meal;
}

// The existing nutrition model remains the sole owner of fullness, body
// condition, refusal and the shared meal/treat cooldown. A fish is a meal,
// including when switching food types during that cooldown.
inline cat::nutrition::FeedResult TryFeed(cat::nutrition::State& nutrition,
    std::int64_t now, FoodKind kind, double random01) noexcept {
    if (!IsValid(kind)) return cat::nutrition::FeedResult::TooSoon;
    return cat::nutrition::TryFeed(nutrition, now, IsTreat(kind), random01);
}

// For callers that own all three records (such as the companion). As with
// the existing feeding paths, the caller synchronizes their identity/time
// before offering food and handles animation, brain notification and saving.
// A refusal never adds a lifetime care count or records a care memory.
inline cat::nutrition::FeedResult TryFeed(cat::PetState& pet,
    cat::nutrition::State& nutrition, cat::memory::State& memory,
    std::int64_t now, FoodKind kind, double random01) {
    if (!IsValid(kind)) return cat::nutrition::FeedResult::TooSoon;
    const auto result = TryFeed(nutrition, now, kind, random01);
    if (result == cat::nutrition::FeedResult::Accepted) {
        cat::Feed(pet, nutrition.updatedUtc, IsTreat(kind));
        cat::memory::Record(memory, MemoryEvent(kind), nutrition.updatedUtc);
    }
    pet.hunger = nutrition.fullness;
    return result;
}

} // namespace cat::feeding
