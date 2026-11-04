#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <limits>
#include <locale>
#include <map>
#include <sstream>
#include <string>

namespace cat {

struct Civil { int year; unsigned month, day; int hour = 0, minute = 0, second = 0; };
inline bool Leap(int y) { return y % 4 == 0 && (y % 100 != 0 || y % 400 == 0); }
inline unsigned MonthDays(int y, unsigned m) {
    const unsigned days[] = {31,28,31,30,31,30,31,31,30,31,30,31};
    return m >= 1 && m <= 12 ? days[m - 1] + (m == 2 && Leap(y)) : 0;
}
// Proleptic Gregorian civil dates, with Unix epoch at 1970-01-01.
inline std::int64_t DaysFromCivil(int y, unsigned m, unsigned d) {
    y -= m <= 2;
    const int era = (y >= 0 ? y : y - 399) / 400;
    const unsigned yoe = static_cast<unsigned>(y - era * 400);
    const unsigned mp = m > 2 ? m - 3 : m + 9;
    const unsigned doy = (153 * mp + 2) / 5 + d - 1;
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return static_cast<std::int64_t>(era) * 146097 + doe - 719468;
}
inline std::int64_t ToUnix(Civil c) {
    return DaysFromCivil(c.year, c.month, c.day) * 86400 + c.hour * 3600 + c.minute * 60 + c.second;
}
inline Civil FromUnix(std::int64_t seconds) {
    std::int64_t days = seconds / 86400;
    std::int64_t rem = seconds % 86400;
    if (rem < 0) { rem += 86400; --days; }
    std::int64_t z = days + 719468;
    const std::int64_t era = (z >= 0 ? z : z - 146096) / 146097;
    const unsigned doe = static_cast<unsigned>(z - era * 146097);
    const unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    int y = static_cast<int>(yoe) + static_cast<int>(era * 400);
    const unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    const unsigned mp = (5 * doy + 2) / 153;
    const unsigned d = doy - (153 * mp + 2) / 5 + 1;
    const unsigned m = mp < 10 ? mp + 3 : mp - 9;
    y += m <= 2;
    return {y, m, d, static_cast<int>(rem / 3600), static_cast<int>((rem % 3600) / 60), static_cast<int>(rem % 60)};
}
inline std::int64_t Anniversary(std::int64_t born, int years) {
    Civil c = FromUnix(born); c.year += years;
    c.day = std::min(c.day, MonthDays(c.year, c.month)); // Feb 29 becomes Feb 28 in a common year.
    return ToUnix(c);
}
struct AgeInfo {
    std::int64_t days = 0;
    int years = 0, months = 0;
    std::int64_t tenthAnniversary = 0;
    bool reachedTenYears = false;
};
inline AgeInfo Age(std::int64_t born, std::int64_t now) {
    AgeInfo result;
    result.tenthAnniversary = Anniversary(born, 10);
    result.reachedTenYears = now >= result.tenthAnniversary;
    if (now < born) return result;
    result.days = (now - born) / 86400;
    const Civil a = FromUnix(born), b = FromUnix(now);
    int months = (b.year - a.year) * 12 + static_cast<int>(b.month) - static_cast<int>(a.month);
    Civil marker = a;
    marker.year = a.year + (static_cast<int>(a.month) - 1 + months) / 12;
    marker.month = static_cast<unsigned>((static_cast<int>(a.month) - 1 + months) % 12 + 1);
    marker.day = std::min(a.day, MonthDays(marker.year, marker.month));
    if (ToUnix(marker) > now) --months;
    months = std::max(0, months);
    result.years = months / 12; result.months = months % 12;
    return result;
}
inline double GrowthScale(std::int64_t born, std::int64_t now) {
    const double elapsed = static_cast<double>(std::max<std::int64_t>(0, now - born));
    const double firstYear = static_cast<double>(std::max<std::int64_t>(1, Anniversary(born, 1) - born));
    return 0.82 + 0.18 * std::clamp(elapsed / firstYear, 0.0, 1.0);
}

struct PetState {
    std::int64_t bornUtc = 0, updatedUtc = 0;
    std::uint64_t feedCount = 0, snackCount = 0;
    double hunger = 88, affection = 85;
    bool roamingPaused = false;
    double position = 0.5;
};
inline PetState NewPet(std::int64_t now) {
    PetState p; p.bornUtc = p.updatedUtc = now; return p;
}
inline void Advance(PetState& pet, std::int64_t now) {
    const std::int64_t elapsed = now > pet.updatedUtc ? std::min<std::int64_t>(now - pet.updatedUtc, 7 * 86400) : 0;
    const double hours = static_cast<double>(elapsed) / 3600;
    pet.hunger = std::max(20.0, pet.hunger - hours * 3.0);
    pet.affection = std::max(35.0, pet.affection - hours * 0.3);
    pet.updatedUtc = std::max(pet.updatedUtc, now);
}
inline void Feed(PetState& pet, std::int64_t now, bool snack) {
    Advance(pet, now);
    pet.hunger = std::min(100.0, pet.hunger + (snack ? 12 : 35));
    pet.affection = std::min(100.0, pet.affection + (snack ? 8 : 3));
    if (snack) pet.snackCount = std::min<std::uint64_t>(999999999, pet.snackCount + 1);
    else pet.feedCount = std::min<std::uint64_t>(999999999, pet.feedCount + 1);
}
inline std::uint64_t Checksum(const std::string& text) {
    std::uint64_t value = 14695981039346656037ULL;
    for (unsigned char ch : text) { value ^= ch; value *= 1099511628211ULL; }
    return value;
}
inline std::string Serialize(const PetState& p) {
    std::ostringstream out; out.imbue(std::locale::classic());
    out << "schema=1\n" << "born_utc=" << p.bornUtc << '\n'
        << "updated_utc=" << p.updatedUtc << '\n' << "feed_count=" << p.feedCount << '\n'
        << "snack_count=" << p.snackCount << '\n' << std::fixed << std::setprecision(6)
        << "hunger=" << p.hunger << '\n' << "affection=" << p.affection << '\n'
        << "paused=" << (p.roamingPaused ? 1 : 0) << '\n' << "position=" << p.position << '\n';
    const std::string body = out.str();
    out << "checksum=" << std::hex << std::setw(16) << std::setfill('0') << Checksum(body) << '\n';
    return out.str();
}
inline bool Parse(const std::string& text, PetState& destination, std::string& error) {
    auto fail = [&error](const char* why) { error = why; return false; };
    if (text.empty() || text.size() > 65536) return fail("Empty or oversized save");
    const auto checksumAt = text.rfind("checksum=");
    if (checksumAt == std::string::npos || (checksumAt && text[checksumAt - 1] != '\n')) return fail("Missing checksum");
    const std::string body = text.substr(0, checksumAt);
    std::string hash = text.substr(checksumAt + 9);
    if (!hash.empty() && hash.back() == '\n') hash.pop_back();
    if (hash.size() != 16 || hash.find_first_not_of("0123456789abcdefABCDEF") != std::string::npos) return fail("Malformed checksum");
    std::uint64_t expected = 0;
    std::istringstream hashIn(hash); hashIn >> std::hex >> expected;
    if (expected != Checksum(body)) return fail("Checksum mismatch");
    std::map<std::string, std::string> entries;
    std::istringstream lines(body); std::string line;
    while (std::getline(lines, line)) {
        const auto split = line.find('=');
        if (split == std::string::npos || split == 0 || !entries.emplace(line.substr(0, split), line.substr(split + 1)).second) return fail("Malformed or duplicate field");
    }
    const char* fields[] = {"schema", "born_utc", "updated_utc", "feed_count", "snack_count", "hunger", "affection", "paused", "position"};
    if (entries.size() != 9) return fail("Unexpected fields");
    for (const char* key : fields) if (!entries.count(key)) return fail("Missing field");
    if (entries["schema"] != "1") return fail("Unsupported save version");
    auto integer = [&](const char* key, std::int64_t& value) {
        const std::string& s = entries[key];
        if (s.empty() || s.find_first_not_of("0123456789") != std::string::npos || s.size() > 18) return false;
        std::istringstream input(s); input.imbue(std::locale::classic()); input >> value;
        return input && input.peek() == std::char_traits<char>::eof();
    };
    auto real = [&](const char* key, double& value) {
        const std::string& s = entries[key];
        if (s.empty() || s.find_first_not_of("0123456789.") != std::string::npos) return false;
        std::istringstream input(s); input.imbue(std::locale::classic()); input >> value;
        return input && input.peek() == std::char_traits<char>::eof() && std::isfinite(value);
    };
    PetState p; std::int64_t feeds = 0, snacks = 0;
    if (!integer("born_utc", p.bornUtc) || !integer("updated_utc", p.updatedUtc) || !integer("feed_count", feeds) || !integer("snack_count", snacks)) return fail("Invalid integer");
    if (p.bornUtc < 946684800LL || p.bornUtc > 32503680000LL || p.updatedUtc < p.bornUtc || p.updatedUtc > 32503680000LL) return fail("Invalid dates");
    if (feeds > 999999999 || snacks > 999999999) return fail("Invalid care counts");
    p.feedCount = static_cast<std::uint64_t>(feeds); p.snackCount = static_cast<std::uint64_t>(snacks);
    if (!real("hunger", p.hunger) || !real("affection", p.affection) || !real("position", p.position)) return fail("Invalid numeric value");
    if (p.hunger < 0 || p.hunger > 100 || p.affection < 0 || p.affection > 100 || p.position < 0 || p.position > 1) return fail("Numeric value out of range");
    if (entries["paused"] != "0" && entries["paused"] != "1") return fail("Invalid pause flag");
    p.roamingPaused = entries["paused"] == "1";
    destination = p; error.clear(); return true;
}

} // namespace cat
