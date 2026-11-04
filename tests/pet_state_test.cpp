#include "../src/pet_state.hpp"

#include <cstdlib>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace {

int checks = 0;
#define CHECK(condition) do { ++checks; if (!(condition)) throw std::runtime_error( \
    std::string(__FILE__) + ":" + std::to_string(__LINE__) + " failed: " #condition); } while (false)

void Near(double actual, double expected, double tolerance = 1e-9) {
    ++checks;
    if (std::abs(actual - expected) > tolerance) {
        std::ostringstream error;
        error << "Expected " << std::setprecision(17) << expected << ", got " << actual;
        throw std::runtime_error(error.str());
    }
}

void SameState(const cat::PetState& a, const cat::PetState& b) {
    CHECK(a.bornUtc == b.bornUtc);
    CHECK(a.updatedUtc == b.updatedUtc);
    CHECK(a.feedCount == b.feedCount);
    CHECK(a.snackCount == b.snackCount);
    CHECK(a.hunger == b.hunger);
    CHECK(a.affection == b.affection);
    CHECK(a.roamingPaused == b.roamingPaused);
    CHECK(a.position == b.position);
}

std::string WithField(std::string text, const std::string& key, const std::string& value) {
    const auto checksum = text.rfind("checksum=");
    CHECK(checksum != std::string::npos);
    std::string body = text.substr(0, checksum);
    const auto first = body.find(key + "=");
    CHECK(first != std::string::npos);
    const auto last = body.find('\n', first);
    body.replace(first, last - first, key + "=" + value);
    std::ostringstream output;
    output << body << "checksum=" << std::hex << std::setfill('0') << std::setw(16)
           << cat::Checksum(body) << '\n';
    return output.str();
}

void RejectWithoutMutation(const std::string& text) {
    auto destination = cat::NewPet(cat::ToUnix({2025, 7, 9, 10, 11, 12}));
    destination.feedCount = 321;
    destination.snackCount = 123;
    destination.hunger = 64.5;
    destination.affection = 71.25;
    destination.position = 0.125;
    destination.roamingPaused = true;
    const auto before = destination;
    std::string error;
    CHECK(!cat::Parse(text, destination, error));
    CHECK(!error.empty());
    SameState(destination, before);
}

void CalendarAndUtc() {
    CHECK(cat::ToUnix({1970, 1, 1}) == 0);
    CHECK(cat::ToUnix({2000, 1, 1}) == 946684800);
    CHECK(cat::ToUnix({1969, 12, 31, 23, 59, 59}) == -1);
    const auto negative = cat::FromUnix(-1);
    CHECK(negative.year == 1969 && negative.month == 12 && negative.day == 31);
    CHECK(negative.hour == 23 && negative.minute == 59 && negative.second == 59);
    CHECK(cat::Leap(2000));
    CHECK(!cat::Leap(2100));
    CHECK(cat::MonthDays(2024, 2) == 29);
    CHECK(cat::MonthDays(2025, 2) == 28);

    // UTC timestamps must not depend on the computer's active local timezone.
    const auto reference = cat::ToUnix({2026, 10, 31, 3, 14, 15});
    for (const char* zone : {"UTC0", "KST-9", "EST5EDT"}) {
        setenv("TZ", zone, 1);
        tzset();
        CHECK(cat::ToUnix({2026, 10, 31, 3, 14, 15}) == reference);
        const auto c = cat::FromUnix(reference);
        CHECK(c.year == 2026 && c.month == 10 && c.day == 31);
        CHECK(c.hour == 3 && c.minute == 14 && c.second == 15);
    }
    setenv("TZ", "UTC0", 1);
    tzset();
}

void RealTenYearBoundary() {
    const auto born = cat::ToUnix({2026, 10, 31, 3, 14, 15});
    const auto tenth = cat::ToUnix({2036, 10, 31, 3, 14, 15});
    CHECK(cat::Anniversary(born, 10) == tenth);
    CHECK((tenth - born) / 86400 == 3653);
    const auto fixed3650 = cat::Age(born, born + 3650LL * 86400);
    CHECK(!fixed3650.reachedTenYears);
    const auto before = cat::Age(born, tenth - 1);
    CHECK(!before.reachedTenYears);
    CHECK(before.years == 9 && before.months == 11);
    const auto exact = cat::Age(born, tenth);
    CHECK(exact.reachedTenYears);
    CHECK(exact.years == 10 && exact.months == 0);
    CHECK(exact.tenthAnniversary == tenth);
    CHECK(cat::Age(born, cat::ToUnix({2051, 10, 31, 3, 14, 15})).years == 25);
    const auto backwards = cat::Age(born, born - 86400);
    CHECK(backwards.days == 0 && backwards.years == 0 && backwards.months == 0);
    CHECK(!backwards.reachedTenYears);
}

void LeapDayBirth() {
    const auto born = cat::ToUnix({2024, 2, 29, 12, 30, 45});
    CHECK(cat::Anniversary(born, 1) == cat::ToUnix({2025, 2, 28, 12, 30, 45}));
    CHECK(cat::Anniversary(born, 4) == cat::ToUnix({2028, 2, 29, 12, 30, 45}));
    const auto tenth = cat::ToUnix({2034, 2, 28, 12, 30, 45});
    CHECK(!cat::Age(born, tenth - 1).reachedTenYears);
    CHECK(cat::Age(born, tenth).reachedTenYears);
    CHECK(cat::Age(born, tenth).years == 10);
    CHECK(cat::Age(born, cat::ToUnix({2025, 2, 28, 12, 30, 44})).years == 0);
    CHECK(cat::Age(born, cat::ToUnix({2025, 2, 28, 12, 30, 45})).years == 1);
}

void GradualGrowth() {
    const auto born = cat::ToUnix({2026, 10, 31, 3, 14, 15});
    const auto first = cat::Anniversary(born, 1);
    Near(cat::GrowthScale(born, born), 0.82);
    Near(cat::GrowthScale(born, born - 100000), 0.82);
    CHECK(cat::GrowthScale(born, born + 86400) > 0.82);
    CHECK(cat::GrowthScale(born, born + 86400) < 0.821);
    Near(cat::GrowthScale(born, born + (first - born) / 2), 0.91);
    CHECK(cat::GrowthScale(born, first - 1) < 1.0);
    Near(cat::GrowthScale(born, first), 1.0);
    Near(cat::GrowthScale(born, cat::Anniversary(born, 25)), 1.0);

    auto pet = cat::NewPet(born);
    for (int i = 0; i < 50; ++i) cat::Feed(pet, born, i % 2 == 0);
    CHECK(pet.bornUtc == born);
    CHECK(cat::Age(pet.bornUtc, born).days == 0);
    Near(cat::GrowthScale(pet.bornUtc, born), 0.82);
}

void ClockRollbackAndTickSize() {
    const auto born = cat::ToUnix({2026, 10, 31, 3, 14, 15});
    auto pet = cat::NewPet(born);
    cat::Advance(pet, born + 3600);
    const auto afterHour = pet;
    cat::Advance(pet, born - 86400);
    SameState(pet, afterHour);
    cat::Advance(pet, born + 1800);
    SameState(pet, afterHour);
    cat::Advance(pet, born + 7200);
    CHECK(pet.updatedUtc == born + 7200);
    Near(pet.hunger, 82);
    Near(pet.affection, 84.4);

    auto chunks = cat::NewPet(born);
    for (int seconds = 30; seconds <= 7200; seconds += 30) cat::Advance(chunks, born + seconds);
    Near(chunks.hunger, pet.hunger, 1e-8);
    Near(chunks.affection, pet.affection, 1e-8);
}

void LongOfflineAndCare() {
    const auto born = cat::ToUnix({2026, 10, 31, 3, 14, 15});
    auto pet = cat::NewPet(born);
    const auto yearsLater = cat::Anniversary(born, 25);
    cat::Advance(pet, yearsLater);
    CHECK(pet.bornUtc == born);
    CHECK(pet.updatedUtc == yearsLater);
    Near(pet.hunger, 20);
    Near(pet.affection, 35);
    CHECK(cat::Age(pet.bornUtc, yearsLater).years == 25);
    cat::Feed(pet, yearsLater, false);
    CHECK(pet.feedCount == 1 && pet.snackCount == 0);
    Near(pet.hunger, 55);
    Near(pet.affection, 38);
    cat::Feed(pet, yearsLater, true);
    CHECK(pet.feedCount == 1 && pet.snackCount == 1);
    Near(pet.hunger, 67);
    Near(pet.affection, 46);
    for (int i = 0; i < 25; ++i) cat::Feed(pet, yearsLater, false);
    Near(pet.hunger, 100);
    CHECK(pet.affection <= 100);
    pet.feedCount = pet.snackCount = 999999999;
    cat::Feed(pet, yearsLater, false);
    cat::Feed(pet, yearsLater, true);
    CHECK(pet.feedCount == 999999999 && pet.snackCount == 999999999);
}

void SaveRoundTrip() {
    CHECK(cat::Checksum("") == 0xcbf29ce484222325ULL);
    CHECK(cat::Checksum("hello") == 0xa430d84680aabd0bULL);
    auto original = cat::NewPet(cat::ToUnix({2024, 2, 29, 12, 30, 45}));
    original.updatedUtc = cat::ToUnix({2034, 2, 28, 12, 30, 45});
    original.feedCount = 12345;
    original.snackCount = 6789;
    original.hunger = 66.123456;
    original.affection = 77.654321;
    original.position = 0.812345;
    original.roamingPaused = true;
    const std::string serialized = cat::Serialize(original);
    cat::PetState parsed;
    std::string error = "old error";
    CHECK(cat::Parse(serialized, parsed, error));
    CHECK(error.empty());
    SameState(parsed, original);
    CHECK(cat::Serialize(parsed) == serialized);
    CHECK(cat::Age(parsed.bornUtc, parsed.updatedUtc).reachedTenYears);
}

void DamagedAndInvalidSaves() {
    const auto pet = cat::NewPet(cat::ToUnix({2026, 10, 31, 3, 14, 15}));
    const auto saved = cat::Serialize(pet);
    RejectWithoutMutation("");
    RejectWithoutMutation(saved.substr(0, saved.size() / 2));
    RejectWithoutMutation(saved.substr(0, saved.size() - 5));
    RejectWithoutMutation(saved + "trailing junk");
    RejectWithoutMutation(std::string(65537, 'x'));
    std::string flipped = saved;
    flipped[flipped.find("hunger=") + 7] = '7';
    RejectWithoutMutation(flipped);
    for (const auto& invalid : std::vector<std::pair<std::string, std::string>>{
        {"schema", "2"}, {"born_utc", "0"}, {"updated_utc", "946684800"},
        {"feed_count", "-1"}, {"snack_count", "1000000000"},
        {"hunger", "nan"}, {"hunger", "101.000000"}, {"affection", "inf"},
        {"position", "1.01"}, {"position", "0.2.3"}, {"paused", "yes"}
    }) RejectWithoutMutation(WithField(saved, invalid.first, invalid.second));
}

} // namespace

int main() {
    const std::vector<std::pair<const char*, std::function<void()>>> groups{
        {"Gregorian dates, Unix epoch, UTC/timezone independence", CalendarAndUtc},
        {"Actual ten-year calendar boundary and no expiry", RealTenYearBoundary},
        {"February 29 birthdays and exact anniversary time", LeapDayBirth},
        {"Gradual year-long growth, no care-based age acceleration", GradualGrowth},
        {"Clock rollback, monotonic updatedUtc, tick-size consistency", ClockRollbackAndTickSize},
        {"25-year offline absence, comfortable floors, continued care", LongOfflineAndCare},
        {"Checksum reference vectors and complete save round-trip", SaveRoundTrip},
        {"Truncated/corrupt/invalid saves preserve destination", DamagedAndInvalidSaves},
    };
    try {
        for (const auto& group : groups) { group.second(); std::cout << "PASS " << group.first << '\n'; }
        std::cout << "All " << groups.size() << " groups passed; " << checks << " checks.\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "FAIL " << e.what() << '\n';
        return 1;
    }
}
