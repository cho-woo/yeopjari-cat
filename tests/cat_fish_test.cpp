#include "../src/cat_fish.hpp"

#include <array>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>

namespace {
int checks = 0;
#define CHECK(condition) do { ++checks; if (!(condition)) throw std::runtime_error( \
    std::string(__FILE__) + ":" + std::to_string(__LINE__) + " failed: " #condition); } while (false)
using cat::feeding::FoodKind;
using cat::nutrition::FeedResult;
constexpr std::int64_t start = 1762387200, hour = 3600;
constexpr std::array<FoodKind, 3> foods{{FoodKind::Meal, FoodKind::Treat, FoodKind::Fish}};

struct Records {
    cat::PetState pet = cat::NewPet(start);
    cat::nutrition::State nutrition = cat::nutrition::NewState(start, start, 45);
    cat::memory::State memory = cat::memory::FromLegacy(pet, start);
    Records() { pet.hunger = nutrition.fullness; }
    std::string Snapshot() const {
        return cat::Serialize(pet) + cat::nutrition::Serialize(nutrition) + cat::memory::Serialize(memory);
    }
    FeedResult Offer(std::int64_t now, FoodKind kind, double random = .9) {
        return cat::feeding::TryFeed(pet, nutrition, memory, now, kind, random);
    }
};

void FishExactlyMatchesTheExistingMealModel() {
    for (double fullness : {20., 50., 84.99, 85., 92., 98., 100.})
    for (double body : {0., 50., 100.})
    for (double random : {-1., 0., .6999, .7, 1., 2., std::numeric_limits<double>::quiet_NaN()})
    for (std::int64_t elapsed : {0LL, 299LL, 300LL, 3600LL}) {
        auto expected = cat::nutrition::NewState(start, start, fullness);
        expected.bodyCondition = body;
        auto fish = expected;
        const auto mealResult = cat::nutrition::TryFeed(expected, start + elapsed, false, random);
        CHECK(cat::feeding::TryFeed(fish, start + elapsed, FoodKind::Fish, random) == mealResult);
        CHECK(cat::nutrition::Serialize(fish) == cat::nutrition::Serialize(expected));
    }
    CHECK(cat::feeding::MemoryEvent(FoodKind::Fish) == cat::memory::Event::Meal);
    CHECK(!cat::feeding::IsTreat(FoodKind::Fish));
}

void EveryFoodSharesOneCooldown() {
    for (auto first : foods) for (auto next : foods) {
        Records records;
        CHECK(records.Offer(start, first) == FeedResult::Accepted);
        const auto fedPet = cat::Serialize(records.pet), fedMemory = cat::memory::Serialize(records.memory);
        for (int click = 0; click < 20; ++click)
            CHECK(records.Offer(start, next) == FeedResult::TooSoon);
        CHECK(cat::Serialize(records.pet) == fedPet);
        CHECK(cat::memory::Serialize(records.memory) == fedMemory);
        CHECK(records.Offer(start + 299, next) == FeedResult::TooSoon);
        CHECK(records.pet.feedCount + records.pet.snackCount == 1);
        CHECK(records.memory.meals + records.memory.treats == 1);
        CHECK(records.Offer(start + 300, next, 1) == FeedResult::Accepted);
        CHECK(records.pet.feedCount == static_cast<unsigned>(first != FoodKind::Treat) + static_cast<unsigned>(next != FoodKind::Treat));
        CHECK(records.pet.snackCount == static_cast<unsigned>(first == FoodKind::Treat) + static_cast<unsigned>(next == FoodKind::Treat));
        CHECK(records.memory.meals == records.pet.feedCount && records.memory.treats == records.pet.snackCount);
    }
}

void FishUsesMealRefusalAndCannotFarmCare() {
    for (double fullness : {85., 92., 98., 100.}) {
        Records records; records.nutrition.fullness = records.pet.hunger = fullness;
        const auto saved = records.Snapshot();
        CHECK(records.Offer(start, FoodKind::Fish, 0) == FeedResult::Full);
        CHECK(records.Snapshot() == saved);
        CHECK(records.pet.feedCount == 0 && records.memory.meals == 0);
    }
    Records fish, treat;
    fish.nutrition.fullness = fish.pet.hunger = 85;
    treat.nutrition.fullness = treat.pet.hunger = 85;
    CHECK(fish.Offer(start, FoodKind::Fish, .6999) == FeedResult::Full);
    CHECK(treat.Offer(start, FoodKind::Treat, .6999) == FeedResult::Accepted);
    CHECK(fish.Offer(start, FoodKind::Fish, .7) == FeedResult::Accepted);
    CHECK(fish.nutrition.lastMealUtc == start && fish.nutrition.lastTreatUtc == 0);
    CHECK(fish.pet.feedCount == 1 && fish.pet.snackCount == 0);
    CHECK(fish.memory.meals == 1 && fish.memory.treats == 0);
    CHECK(fish.pet.affection == 88); // Meal affection; treats add eight.
    CHECK(fish.nutrition.bodyCondition == 50); // No instant resize.
}

void FishKeepsLifetimeAndWeightEffectsOfMeals() {
    Records expected, fish;
    expected.pet.position = fish.pet.position = .27;
    expected.pet.roamingPaused = fish.pet.roamingPaused = true;
    for (int meal = 0; meal < 72; ++meal) {
        const auto now = start + meal * 2 * hour;
        CHECK(expected.Offer(now, FoodKind::Meal, 1) == FeedResult::Accepted);
        CHECK(fish.Offer(now, FoodKind::Fish, 1) == FeedResult::Accepted);
        CHECK(fish.Snapshot() == expected.Snapshot());
    }
    CHECK(fish.pet.feedCount == 72 && fish.memory.meals == 72);
    CHECK(fish.pet.snackCount == 0 && fish.memory.treats == 0);
    CHECK(fish.nutrition.bodyCondition > 80);
    CHECK(fish.pet.bornUtc == start && fish.pet.position == .27 && fish.pet.roamingPaused);
    CHECK(fish.memory.treatPreference == 0);
}

void AcceptedCareMatchesTheEstablishedCallerSequence() {
    for (auto food : foods) {
        Records actual, expected;
        const bool treat = food == FoodKind::Treat;
        CHECK(cat::nutrition::TryFeed(expected.nutrition, start, treat, .9) == FeedResult::Accepted);
        cat::Feed(expected.pet, start, treat);
        cat::memory::Record(expected.memory, treat ? cat::memory::Event::Treat : cat::memory::Event::Meal, start);
        expected.pet.hunger = expected.nutrition.fullness;
        CHECK(actual.Offer(start, food) == FeedResult::Accepted);
        CHECK(actual.Snapshot() == expected.Snapshot());
    }
    Records fullLedger;
    fullLedger.memory.creditedUtc.fill(0);
    for (std::size_t i = 0; i < fullLedger.memory.creditedUtc.size(); ++i)
        fullLedger.memory.creditedUtc[i] = start + 300 * i;
    const auto now = start + 300 * 24;
    fullLedger.memory.updatedUtc = now;
    CHECK(cat::memory::Valid(fullLedger.memory));
    CHECK(fullLedger.Offer(now, FoodKind::Fish) == FeedResult::Accepted);
    CHECK(fullLedger.memory.meals == 1 && fullLedger.pet.feedCount == 1);
    // Exhausting bond credits cannot erase an accepted lifetime meal count.
    CHECK(fullLedger.memory.learningSamples == 0);
}

void ExistingSchemaRoundTripsAndClockRollbackPreserveCooldown() {
    Records records;
    CHECK(records.Offer(start, FoodKind::Fish) == FeedResult::Accepted);
    std::string error;
    Records restored;
    CHECK(cat::Parse(cat::Serialize(records.pet), restored.pet, error));
    CHECK(cat::nutrition::Parse(cat::nutrition::Serialize(records.nutrition), restored.nutrition, error));
    CHECK(cat::memory::Parse(cat::memory::Serialize(records.memory), restored.memory, error));
    CHECK(restored.Snapshot() == records.Snapshot());
    const auto saved = restored.Snapshot();
    for (auto food : foods) CHECK(restored.Offer(start - hour, food) == FeedResult::TooSoon);
    CHECK(restored.Snapshot() == saved);
    CHECK(restored.Offer(start + 299, FoodKind::Treat) == FeedResult::TooSoon);
    CHECK(restored.Offer(start + 300, FoodKind::Meal) == FeedResult::Accepted);
    CHECK(restored.pet.feedCount == 2 && restored.memory.meals == 2);
}

void CatsAreIndependentAndCountsStaySaturated() {
    Records primary, companion;
    const auto primarySave = primary.Snapshot();
    CHECK(companion.Offer(start, FoodKind::Fish) == FeedResult::Accepted);
    CHECK(primary.Snapshot() == primarySave);
    CHECK(companion.pet.feedCount == 1 && companion.memory.meals == 1);
    Records lifetime;
    lifetime.pet.feedCount = lifetime.memory.meals = cat::memory::kMaxCount;
    CHECK(lifetime.Offer(start, FoodKind::Fish) == FeedResult::Accepted);
    CHECK(lifetime.pet.feedCount == cat::memory::kMaxCount && lifetime.memory.meals == cat::memory::kMaxCount);
    CHECK(cat::memory::Valid(lifetime.memory));
}

void InvalidFoodIsRejectedBeforeMutation() {
    Records records; const auto saved = records.Snapshot();
    CHECK(records.Offer(start + hour, static_cast<FoodKind>(-1)) == FeedResult::TooSoon);
    CHECK(records.Snapshot() == saved);
    CHECK(cat::feeding::TryFeed(records.nutrition, start + hour, static_cast<FoodKind>(99), 1) == FeedResult::TooSoon);
    CHECK(records.Snapshot() == saved);
}
void FishIsOfferedOnlyToCats() {
    for (int index = 0; index < cat::kCharacterCount; ++index) {
        const auto character = static_cast<cat::Character>(index);
        CHECK(cat::feeding::CanOfferFish(character) ==
            (character != cat::Character::Dog && character != cat::Character::Rabbit));
    }
    CHECK(!cat::feeding::CanOfferFish(static_cast<cat::Character>(-1)));
    CHECK(!cat::feeding::CanOfferFish(static_cast<cat::Character>(cat::kCharacterCount)));
}
} // namespace

int main() {
    try {
        FishExactlyMatchesTheExistingMealModel(); EveryFoodSharesOneCooldown();
        FishUsesMealRefusalAndCannotFarmCare(); FishKeepsLifetimeAndWeightEffectsOfMeals();
        AcceptedCareMatchesTheEstablishedCallerSequence();
        ExistingSchemaRoundTripsAndClockRollbackPreserveCooldown();
        CatsAreIndependentAndCountsStaySaturated(); InvalidFoodIsRejectedBeforeMutation(); FishIsOfferedOnlyToCats();
        std::cout << "Fish feeding: " << checks << " checks passed\n";
        return EXIT_SUCCESS;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return EXIT_FAILURE; }
}
