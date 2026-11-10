#include "../src/cat_nutrition.hpp"
#include "../src/cat_appearance_visual.hpp"

#include <iostream>
#include <stdexcept>
#include <utility>
#include <vector>

namespace nutrition = cat::nutrition;
namespace {

int checks = 0;
#define CHECK(condition) do { ++checks; if (!(condition)) throw std::runtime_error( \
    std::string(__FILE__) + ":" + std::to_string(__LINE__) + " failed: " #condition); } while (false)

constexpr std::int64_t Hour = 3600;
constexpr std::int64_t Day = 24 * Hour;
const std::int64_t Start = cat::ToUnix({2026, 11, 3, 12, 0, 0});

bool Near(double left, double right, double tolerance = 1e-8) {
    return std::isfinite(left) && std::isfinite(right) && std::abs(left - right) <= tolerance;
}

std::string Body(const std::string& save) {
    return save.substr(0, save.rfind("checksum="));
}

std::string Seal(const std::string& body, const std::string& ending = "\n") {
    std::ostringstream out;
    out.imbue(std::locale::classic());
    out << body << "checksum=" << std::hex << std::setw(16) << std::setfill('0') << cat::Checksum(body) << ending;
    return out.str();
}

std::string ReplaceField(std::string body, const std::string& key, const std::string& value) {
    const auto begin = body.find(key + '=');
    CHECK(begin != std::string::npos);
    const auto end = body.find('\n', begin);
    CHECK(end != std::string::npos);
    body.replace(begin, end - begin, key + '=' + value);
    return body;
}

void RejectWithoutMutation(const std::string& input) {
    auto destination = nutrition::NewState(Start - Day, Start, 73.125);
    destination.bodyCondition = 68.5;
    destination.lastMealUtc = Start - Hour;
    const auto original = nutrition::Serialize(destination);
    std::string error;
    CHECK(!nutrition::Parse(input, destination, error));
    CHECK(!error.empty());
    CHECK(nutrition::Serialize(destination) == original);
}

void InitializationAndMigration() {
    const auto oldBirthday = cat::ToUnix({2016, 2, 29});
    auto state = nutrition::NewState(oldBirthday, Start, 62.5);
    CHECK(nutrition::IsValid(state));
    CHECK(state.bornUtc == oldBirthday);
    CHECK(state.updatedUtc == Start);
    CHECK(state.fullness == 62.5 && state.bodyCondition == 50);
    CHECK(state.lastMealUtc == 0 && state.lastTreatUtc == 0 && state.lastRequestUtc == 0);
    nutrition::Advance(state, Start);
    CHECK(state.bodyCondition == 50); // Years of old care are not retroactively scored.
    auto backwards = nutrition::NewState(Start, Start - Day, 1);
    CHECK(backwards.updatedUtc == Start && backwards.fullness == 20);
    auto invalidInputs = nutrition::NewState(std::numeric_limits<std::int64_t>::min(),
        std::numeric_limits<std::int64_t>::max(), std::numeric_limits<double>::quiet_NaN());
    CHECK(nutrition::IsValid(invalidInputs));
    CHECK(invalidInputs.bornUtc == nutrition::MinimumUtc && invalidInputs.updatedUtc == nutrition::MaximumUtc);
    CHECK(invalidInputs.fullness == 88);
}

void ContinuousProgress() {
    auto state = nutrition::NewState(Start, Start, 100);
    nutrition::Advance(state, Start + Hour);
    CHECK(Near(state.fullness, 97));
    CHECK(Near(state.bodyCondition, 50.25));
    nutrition::Advance(state, Start + 5 * Hour);
    CHECK(Near(state.fullness, 85));
    CHECK(Near(state.bodyCondition, 51.25));
    nutrition::Advance(state, Start + 10 * Hour);
    CHECK(Near(state.fullness, 70));
    CHECK(Near(state.bodyCondition, 50.0 + 5.0 / 12.0));
    nutrition::Advance(state, Start + 25 * Hour);
    CHECK(Near(state.fullness, 25));
    CHECK(Near(state.bodyCondition, 50.0 - 25.0 / 36.0));

    // Exactly the same elapsed care, whether advanced once, by frames, or after restart.
    for (double initialBody : {0.0, 20.0, 50.0, 80.0, 100.0}) {
        for (double initialFullness : {20.0, 35.0, 40.0, 85.0, 100.0}) {
            auto whole = nutrition::NewState(Start, Start, initialFullness);
            whole.bodyCondition = initialBody;
            auto split = whole;
            nutrition::Advance(whole, Start + 3 * Day);
            for (std::int64_t now = Start + 137; now < Start + 3 * Day; now += 137)
                nutrition::Advance(split, now);
            nutrition::Advance(split, Start + 3 * Day);
            CHECK(Near(whole.bodyCondition, split.bodyCondition));
            CHECK(Near(whole.fullness, split.fullness));
            CHECK(whole.updatedUtc == split.updatedUtc);
        }
    }
}

void NormalMealsStayHealthy() {
    auto state = nutrition::NewState(Start, Start, 50);
    double minimum = 100, maximum = 0;
    for (int meal = 0; meal < 200; ++meal) {
        const auto now = Start + meal * 12 * Hour;
        nutrition::Advance(state, now);
        minimum = std::min(minimum, state.bodyCondition);
        maximum = std::max(maximum, state.bodyCondition);
        CHECK(nutrition::TryFeed(state, now, false, 0.99) == nutrition::FeedResult::Accepted);
    }
    CHECK(minimum > 48 && maximum < 51);
    CHECK(state.bodyCondition > 48 && state.bodyCondition <= 50);
    const auto visual = nutrition::VisualModifiers(state);
    CHECK(visual.speed == 1 && visual.jump == 1);
}

void OverfeedingAndTreats() {
    auto state = nutrition::NewState(Start, Start, 75);
    CHECK(nutrition::TryFeed(state, Start, false, 0.99) == nutrition::FeedResult::Accepted);
    CHECK(state.bodyCondition == 50); // Feeding never causes an instant body resize.
    for (int meal = 1; meal <= 72; ++meal)
        CHECK(nutrition::TryFeed(state, Start + meal * 2 * Hour, false, 0.99) == nutrition::FeedResult::Accepted);
    CHECK(Near(state.bodyCondition, 86));
    CHECK(std::wstring(nutrition::BodyDescription(state)) == L"비만 고양이");
    const auto obese = nutrition::VisualModifiers(state);
    CHECK(obese.bodyWidth > 1.18 && obese.speed < 1 && obese.jump < 1);

    auto treats = nutrition::NewState(Start, Start, 100);
    for (int snack = 1; snack <= 48; ++snack)
        CHECK(nutrition::TryFeed(treats, Start + snack * 3 * Hour, true, 0.0) == nutrition::FeedResult::Accepted);
    CHECK(Near(treats.bodyCondition, 86)); // Churu also contributes by keeping the belly full.
    CHECK(treats.lastTreatUtc == Start + 6 * Day && treats.lastMealUtc == 0);
}

void NeglectAndRecovery() {
    auto state = nutrition::NewState(Start, Start);
    nutrition::Advance(state, Start + Day);
    CHECK(state.bodyCondition > 48 && state.bodyCondition < 50); // One busy day is gentle.
    nutrition::Advance(state, Start + 7 * Day);
    CHECK(state.bodyCondition > 15 && state.bodyCondition < 20);
    CHECK(state.fullness == 20);
    CHECK(std::wstring(nutrition::BodyDescription(state)) == L"아주 마른 허약냥");
    const auto weak = nutrition::VisualModifiers(state);
    CHECK(weak.bodyWidth < 0.88 && weak.speed < 0.9 && weak.jump < 0.9);
    nutrition::Advance(state, Start + 30 * Day);
    CHECK(state.bodyCondition == 0 && nutrition::IsValid(state)); // No death/reset.

    for (double initialBody : {10.0, 90.0}) {
        auto recovery = nutrition::NewState(Start, Start, 45);
        recovery.bodyCondition = initialBody;
        CHECK(nutrition::TryFeed(recovery, Start, false, 0.99) == nutrition::FeedResult::Accepted);
        CHECK(recovery.bodyCondition == initialBody);
        for (int meal = 1; meal <= 80; ++meal)
            CHECK(nutrition::TryFeed(recovery, Start + meal * 12 * Hour, false, 0.99) == nutrition::FeedResult::Accepted);
        CHECK(recovery.bodyCondition > 48 && recovery.bodyCondition <= 50);
    }
}

void FeedingDecisionsAndSpam() {
    for (bool snack : {false, true}) {
        auto full = nutrition::NewState(Start, Start, 98);
        const auto before = nutrition::Serialize(full);
        CHECK(nutrition::TryFeed(full, Start, snack, 1.0) == nutrition::FeedResult::Full);
        CHECK(nutrition::Serialize(full) == before);
    }
    auto meal = nutrition::NewState(Start, Start, 85);
    const auto beforeMeal = nutrition::Serialize(meal);
    CHECK(nutrition::TryFeed(meal, Start, false, 0.6999) == nutrition::FeedResult::Full);
    CHECK(nutrition::Serialize(meal) == beforeMeal);
    CHECK(nutrition::TryFeed(meal, Start, false, 0.7) == nutrition::FeedResult::Accepted);
    CHECK(meal.lastMealUtc == Start && meal.lastTreatUtc == 0 && meal.bodyCondition == 50);

    auto treat = nutrition::NewState(Start, Start, 92);
    const auto beforeTreat = nutrition::Serialize(treat);
    CHECK(nutrition::TryFeed(treat, Start, true, 0.7999) == nutrition::FeedResult::Full);
    CHECK(nutrition::Serialize(treat) == beforeTreat);
    CHECK(nutrition::TryFeed(treat, Start, true, 0.8) == nutrition::FeedResult::Accepted);
    CHECK(treat.lastTreatUtc == Start && treat.lastMealUtc == 0);
    auto lowerFood = nutrition::NewState(Start, Start, 84.99);
    auto lowerTreat = nutrition::NewState(Start, Start, 91.99);
    CHECK(nutrition::TryFeed(lowerFood, Start, false, 0.0) == nutrition::FeedResult::Accepted);
    CHECK(nutrition::TryFeed(lowerTreat, Start, true, 0.0) == nutrition::FeedResult::Accepted);
    auto badRandom = nutrition::NewState(Start, Start, 90);
    CHECK(nutrition::TryFeed(badRandom, Start, false, std::numeric_limits<double>::quiet_NaN()) == nutrition::FeedResult::Full);

    auto spam = nutrition::NewState(Start, Start, 20);
    CHECK(nutrition::TryFeed(spam, Start, false, 0.5) == nutrition::FeedResult::Accepted);
    CHECK(spam.fullness == 55 && spam.bodyCondition == 50);
    const auto accepted = nutrition::Serialize(spam);
    for (int click = 0; click < 100; ++click)
        CHECK(nutrition::TryFeed(spam, Start, click % 2 == 0, 0.99) == nutrition::FeedResult::TooSoon);
    CHECK(nutrition::Serialize(spam) == accepted);
    CHECK(nutrition::TryFeed(spam, Start + 299, true, 0.5) == nutrition::FeedResult::TooSoon);
    CHECK(spam.lastTreatUtc == 0 && spam.lastMealUtc == Start);
    CHECK(nutrition::TryFeed(spam, Start + 300, true, 0.5) == nutrition::FeedResult::Accepted);
    CHECK(Near(spam.fullness, 66.75));
    CHECK(spam.bodyCondition == 50 && spam.lastTreatUtc == Start + 300);

    auto timedRefusal = nutrition::NewState(Start, Start, 95);
    CHECK(nutrition::TryFeed(timedRefusal, Start + Hour, false, 0.0) == nutrition::FeedResult::Full);
    CHECK(timedRefusal.fullness == 92 && timedRefusal.lastMealUtc == 0);
    CHECK(Near(timedRefusal.bodyCondition, 50.25)); // Natural time still passes on refusal.
}

void GentlePersistentRequests() {
    auto state = nutrition::NewState(Start, Start, 35.01);
    CHECK(!nutrition::NeedsFoodRequest(state, Start));
    CHECK(state.lastRequestUtc == 0);
    nutrition::Advance(state, Start + 60);
    CHECK(nutrition::NeedsFoodRequest(state, Start + 60));
    const auto requestedAt = state.lastRequestUtc;
    CHECK(!nutrition::NeedsFoodRequest(state, requestedAt + nutrition::RequestCooldown - 1));
    CHECK(state.lastRequestUtc == requestedAt);

    nutrition::State restored;
    std::string error;
    CHECK(nutrition::Parse(nutrition::Serialize(state), restored, error));
    CHECK(!nutrition::NeedsFoodRequest(restored, requestedAt + nutrition::RequestCooldown - 1));
    CHECK(nutrition::NeedsFoodRequest(restored, requestedAt + nutrition::RequestCooldown));
    CHECK(restored.lastRequestUtc == requestedAt + nutrition::RequestCooldown);
    const auto nextRequest = restored.lastRequestUtc;
    CHECK(nutrition::TryFeed(restored, nextRequest + 60, true, 0.0) == nutrition::FeedResult::Accepted);
    CHECK(restored.lastRequestUtc == nextRequest); // A meal does not erase persisted request history.
    CHECK(!nutrition::NeedsFoodRequest(restored, nextRequest + nutrition::RequestCooldown));
    CHECK(nutrition::NeedsFoodRequest(restored, nextRequest + 60 + nutrition::RequestCooldown));
    CHECK(!nutrition::NeedsFoodRequest(restored, nextRequest + 4 * Hour));
    CHECK(nutrition::NeedsFoodRequest(restored, nextRequest + 60 + 4 * Hour));
    CHECK(!nutrition::NeedsFoodRequest(restored, nextRequest + 60 + 4 * Hour));

    auto hungrySnack = nutrition::NewState(Start, Start, 20);
    CHECK(nutrition::TryFeed(hungrySnack, Start, true, 0.0) == nutrition::FeedResult::Accepted);
    CHECK(hungrySnack.fullness == 32);
    CHECK(!nutrition::NeedsFoodRequest(hungrySnack, Start + nutrition::RequestCooldown - 1));
    CHECK(nutrition::NeedsFoodRequest(hungrySnack, Start + nutrition::RequestCooldown));
}

void ClockAndLongAbsence() {
    auto state = nutrition::NewState(Start, Start, 40);
    CHECK(nutrition::TryFeed(state, Start + Hour, false, 0.0) == nutrition::FeedResult::Accepted);
    const auto before = nutrition::Serialize(state);
    nutrition::Advance(state, std::numeric_limits<std::int64_t>::min());
    CHECK(nutrition::Serialize(state) == before);
    CHECK(nutrition::TryFeed(state, Start - Day, true, 1.0) == nutrition::FeedResult::TooSoon);
    CHECK(nutrition::Serialize(state) == before);
    CHECK(!nutrition::NeedsFoodRequest(state, Start - Day));
    CHECK(nutrition::Serialize(state) == before);

    auto offline = nutrition::NewState(Start, Start, 100);
    nutrition::Advance(offline, cat::Anniversary(Start, 25));
    CHECK(offline.fullness == 20 && offline.bodyCondition == 0);
    CHECK(nutrition::IsValid(offline));
    CHECK(offline.updatedUtc == cat::Anniversary(Start, 25));
    CHECK(nutrition::TryFeed(offline, offline.updatedUtc, false, 0.0) == nutrition::FeedResult::Accepted);
    nutrition::Advance(offline, offline.updatedUtc + 5 * Hour);
    CHECK(offline.bodyCondition > 1); // Even after decades offline, ordinary meals recover it.
    nutrition::Advance(offline, std::numeric_limits<std::int64_t>::max());
    CHECK(offline.updatedUtc == nutrition::MaximumUtc && nutrition::IsValid(offline));
    CHECK(offline.bodyCondition == 0 && offline.fullness == 20);
}

void VisualBounds() {
    auto state = nutrition::NewState(Start, Start);
    double previousWidth = 0;
    for (int body = 0; body <= 100; ++body) {
        state.bodyCondition = body;
        const auto visual = nutrition::VisualModifiers(state);
        CHECK(visual.bodyWidth >= 0.78 && visual.bodyWidth <= 1.26);
        CHECK(visual.bodyWidth >= previousWidth);
        CHECK(visual.bodyHeight >= 0.96 && visual.bodyHeight <= 1);
        CHECK(visual.speed >= 0.70 && visual.speed <= 1);
        CHECK(visual.jump >= 0.65 && visual.jump <= 1);
        if (body >= 35 && body <= 65) CHECK(visual.speed == 1 && visual.jump == 1);
        previousWidth = visual.bodyWidth;
    }
    state.bodyCondition = 0;
    CHECK(Near(nutrition::VisualModifiers(state).bodyWidth, 0.78));
    CHECK(Near(nutrition::VisualModifiers(state).speed, 0.70));
    state.bodyCondition = 100;
    CHECK(Near(nutrition::VisualModifiers(state).bodyWidth, 1.26));
    CHECK(Near(nutrition::VisualModifiers(state).jump, 0.75));
    state.bodyCondition = std::numeric_limits<double>::quiet_NaN();
    CHECK(nutrition::VisualModifiers(state).bodyWidth == 1);
    CHECK(!nutrition::IsValid(state) && nutrition::Serialize(state).empty());
}

void ReferenceAppearanceKeepsCareEffects() {
    auto state=nutrition::NewState(Start-Day,Start,73);
    state.lastMealUtc=Start-Hour;
    for(const auto character:{cat::Character::Cream,cat::Character::Tuxedo}) {
        for(const double condition:{0.,20.,35.,50.,65.,80.,100.}) {
            state.bodyCondition=condition;
            const auto before=nutrition::Serialize(state);
            const auto health=nutrition::VisualModifiers(state);
            const auto visual=cat::appearance::VisualModifiers(state,character);
            CHECK(visual.bodyWidth==1 && visual.bodyHeight==1);
            CHECK(visual.speed==health.speed && visual.jump==health.jump);
            // A fixed sprite, collider registration, stride and paw contact
            // retain one coordinate system even at extreme body conditions.
            CHECK(Near(68*visual.bodyWidth,68));
            CHECK(Near(38.15*visual.bodyWidth,38.15));
            CHECK(Near(110*visual.bodyHeight,110));
            CHECK(Near((329.0/367)*visual.bodyWidth/visual.bodyHeight,329.0/367));
            CHECK(nutrition::Serialize(state)==before);
        }
    }
    state.bodyCondition=0;
    const auto weak=cat::appearance::VisualModifiers(state,cat::Character::Cream);
    CHECK(Near(weak.speed,.70) && Near(weak.jump,.65));
    state.bodyCondition=100;
    const auto heavy=cat::appearance::VisualModifiers(state,cat::Character::Cream);
    CHECK(Near(heavy.speed,.78) && Near(heavy.jump,.75));
    CHECK(cat::appearance::VisualModifiers(state,cat::Character::SilverTabby).bodyWidth==
        nutrition::VisualModifiers(state).bodyWidth);
}

void SaveRoundTripsAndCorruption() {
    auto state = nutrition::NewState(Start - Day, Start, 23.456789123);
    state.bodyCondition = 67.234567891;
    state.lastMealUtc = Start - 5 * Hour;
    state.lastTreatUtc = Start - 3 * Hour;
    state.lastRequestUtc = Start - Hour;
    const auto save = nutrition::Serialize(state);
    nutrition::State restored;
    std::string error = "old error";
    CHECK(nutrition::Parse(save, restored, error));
    CHECK(error.empty() && nutrition::Serialize(restored) == save);
    CHECK(restored.bornUtc == state.bornUtc && restored.updatedUtc == state.updatedUtc);
    CHECK(restored.lastMealUtc == state.lastMealUtc && restored.lastTreatUtc == state.lastTreatUtc);
    CHECK(restored.lastRequestUtc == state.lastRequestUtc);
    CHECK(Near(restored.fullness, state.fullness) && Near(restored.bodyCondition, state.bodyCondition));
    CHECK(nutrition::Parse(save.substr(0, save.size() - 1), restored, error));

    const auto body = Body(save);
    std::string windowsBody;
    for (char ch : body) {
        if (ch == '\n') windowsBody += '\r';
        windowsBody += ch;
    }
    CHECK(nutrition::Parse(Seal(windowsBody, "\r\n"), restored, error));
    CHECK(nutrition::Serialize(restored) == save);
    CHECK(nutrition::Parse(Seal(windowsBody, ""), restored, error));
    RejectWithoutMutation(windowsBody + save.substr(body.size())); // Changed bytes without a new hash.
    RejectWithoutMutation(Seal(windowsBody, "\n"));
    auto mixed = windowsBody;
    mixed.erase(mixed.find('\r'), 1);
    RejectWithoutMutation(Seal(mixed));
    auto damaged = save;
    damaged[damaged.find("body_condition=") + 15] = '9';
    RejectWithoutMutation(damaged);
    RejectWithoutMutation(save + "trailing=1\n");
    RejectWithoutMutation(save + '\n');
    RejectWithoutMutation(body);
    RejectWithoutMutation("");
    RejectWithoutMutation(std::string(4097, 'x'));
    RejectWithoutMutation(Seal(body + "surprise=1\n"));
    RejectWithoutMutation(Seal(body + "fullness=20\n"));
    RejectWithoutMutation(Seal(body.substr(body.find('\n') + 1)));
    RejectWithoutMutation(Seal(ReplaceField(body, "schema", "2")));

    const std::vector<std::pair<std::string, std::string>> invalid = {
        {"born_utc", "946684799"}, {"born_utc", "32503680001"},
        {"updated_utc", std::to_string(Start - 2 * Day)}, {"updated_utc", "32503680001"},
        {"last_meal_utc", std::to_string(Start + 1)}, {"last_treat_utc", std::to_string(Start - Day - 1)},
        {"last_request_utc", std::to_string(Start + 1)}, {"last_meal_utc", "-1"},
        {"last_treat_utc", "00"}, {"last_request_utc", "+0"},
        {"born_utc", "9999999999999999999"}, {"updated_utc", "1.5"},
        {"body_condition", "100.000000001"}, {"body_condition", "-0.1"},
        {"body_condition", "NaN"}, {"body_condition", "inf"}, {"body_condition", "1e2"},
        {"body_condition", "50."}, {"body_condition", ".50"}, {"body_condition", "50..0"},
        {"body_condition", "050"}, {"body_condition", "50 "}, {"body_condition", ""},
        {"fullness", "19.999999999"}, {"fullness", "100.000000001"}, {"fullness", "-inf"}
    };
    for (const auto& value : invalid)
        RejectWithoutMutation(Seal(ReplaceField(body, value.first, value.second)));

    auto badState = state;
    badState.lastRequestUtc = Start + 1;
    CHECK(!nutrition::IsValid(badState));
    CHECK(nutrition::Serialize(badState).empty());
    CHECK(!nutrition::NeedsFoodRequest(badState, Start + Day));
    CHECK(nutrition::TryFeed(badState, Start + Day, false, 0.0) == nutrition::FeedResult::TooSoon);
    CHECK(badState.updatedUtc == state.updatedUtc && badState.fullness == state.fullness);
}

} // namespace

int main() {
    try {
        InitializationAndMigration();
        ContinuousProgress();
        NormalMealsStayHealthy();
        OverfeedingAndTreats();
        NeglectAndRecovery();
        FeedingDecisionsAndSpam();
        GentlePersistentRequests();
        ClockAndLongAbsence();
        VisualBounds();
        ReferenceAppearanceKeepsCareEffects();
        SaveRoundTripsAndCorruption();
        std::cout << "cat nutrition: " << checks << " checks passed (10 scenario groups)\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
