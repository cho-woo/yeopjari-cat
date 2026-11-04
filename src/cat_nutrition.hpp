#pragma once

#include "pet_state.hpp"

namespace cat::nutrition {

// This is a gentle game model, independent of the original birthday/care save.
// "Fullness" is the quantity called "hunger" in the original pet save.
struct State {
    std::int64_t bornUtc = 0, updatedUtc = 0;
    double bodyCondition = 50, fullness = 88;
    std::int64_t lastMealUtc = 0, lastTreatUtc = 0, lastRequestUtc = 0;
};

inline constexpr std::int64_t MinimumUtc = 946684800LL;
inline constexpr std::int64_t MaximumUtc = 32503680000LL;
inline constexpr std::int64_t FeedingCooldown = 5 * 60;
inline constexpr std::int64_t RequestCooldown = 2 * 60 * 60;

inline bool IsValid(const State& state) noexcept {
    if (state.bornUtc < MinimumUtc || state.bornUtc > MaximumUtc ||
        state.updatedUtc < state.bornUtc || state.updatedUtc > MaximumUtc ||
        !std::isfinite(state.bodyCondition) || state.bodyCondition < 0 || state.bodyCondition > 100 ||
        !std::isfinite(state.fullness) || state.fullness < 20 || state.fullness > 100)
        return false;
    const auto validEvent = [&](std::int64_t time) {
        return time == 0 || (time >= state.bornUtc && time <= state.updatedUtc);
    };
    return validEvent(state.lastMealUtc) && validEvent(state.lastTreatUtc) && validEvent(state.lastRequestUtc);
}

// Migration starts at a normal body today. Old care counts are never replayed.
inline State NewState(std::int64_t born, std::int64_t now, double initialFullness = 88) noexcept {
    State state;
    state.bornUtc = std::clamp(born, MinimumUtc, MaximumUtc);
    state.updatedUtc = std::clamp(now, state.bornUtc, MaximumUtc);
    state.fullness = std::isfinite(initialFullness) ? std::clamp(initialFullness, 20.0, 100.0) : 88.0;
    return state;
}

inline void Advance(State& state, std::int64_t now) noexcept {
    if (!IsValid(state)) return;
    now = std::min(now, MaximumUtc);
    if (now <= state.updatedUtc) return; // A rolled-back clock never rewinds care or cooldowns.
    const double hours = static_cast<double>(now - state.updatedUtc) / 3600.0;
    const auto hoursAbove = [&](double threshold) {
        return std::clamp((state.fullness - threshold) / 3.0, 0.0, hours);
    };

    // Fullness only decreases between meals, so these bands occur in this order.
    // Integrating each band directly is independent of frame rate and works for
    // years offline without a loop over elapsed seconds, hours, or days.
    const double overfullHours = hoursAbove(85);
    const double balancedHours = hoursAbove(40) - overfullHours;
    const double hungryHours = hours - hoursAbove(35);
    double body = std::min(100.0, state.bodyCondition + overfullHours * (6.0 / 24.0));
    if (body < 50)
        body = std::min(50.0, body + balancedHours * (6.0 / 24.0));
    else
        body = std::max(50.0, body - balancedHours * (4.0 / 24.0));
    state.bodyCondition = std::clamp(body - hungryHours * (5.0 / 24.0), 0.0, 100.0);
    state.fullness = std::max(20.0, state.fullness - hours * 3.0);
    state.updatedUtc = now;
}

enum class FeedResult { Accepted, Full, TooSoon };

// random01 comes from the caller's RNG; tests can supply it deterministically.
// A refusal may advance real time, but never adds fullness or a meal timestamp.
inline FeedResult TryFeed(State& state, std::int64_t now, bool snack, double random01) noexcept {
    if (!IsValid(state)) return FeedResult::TooSoon;
    Advance(state, now);
    const std::int64_t previousFeed = std::max(state.lastMealUtc, state.lastTreatUtc);
    if (previousFeed != 0 && state.updatedUtc - previousFeed < FeedingCooldown)
        return FeedResult::TooSoon;
    if (state.fullness >= 98) return FeedResult::Full;
    const double chance = std::isfinite(random01) ? std::clamp(random01, 0.0, 1.0) : 0.0;
    if ((!snack && state.fullness >= 85 && chance < 0.70) ||
        (snack && state.fullness >= 92 && chance < 0.80))
        return FeedResult::Full;

    state.fullness = std::min(100.0, state.fullness + (snack ? 12.0 : 35.0));
    if (snack) state.lastTreatUtc = state.updatedUtc;
    else state.lastMealUtc = state.updatedUtc;
    return FeedResult::Accepted;
}

// Call only when a visible, calm cat may actually display a bubble. The caller
// handles startup, sleeping, menus, dragging, and an already-visible bubble.
// Recording the request here prevents repeated requests after saving/restarting.
inline bool NeedsFoodRequest(State& state, std::int64_t now) noexcept {
    if (!IsValid(state)) return false;
    Advance(state, now);
    if (state.fullness > 35) return false;
    const std::int64_t previous = std::max(state.lastRequestUtc,
        std::max(state.lastMealUtc, state.lastTreatUtc));
    if (previous != 0 && state.updatedUtc - previous < RequestCooldown) return false;
    state.lastRequestUtc = state.updatedUtc;
    return true;
}

struct Visual {
    double bodyWidth = 1, bodyHeight = 1, speed = 1, jump = 1;
};

inline Visual VisualModifiers(const State& state) noexcept {
    const double body = std::isfinite(state.bodyCondition) ? std::clamp(state.bodyCondition, 0.0, 100.0) : 50.0;
    const double offset = (body - 50.0) / 50.0;
    const double weak = std::clamp((35.0 - body) / 35.0, 0.0, 1.0);
    const double heavy = std::clamp((body - 65.0) / 35.0, 0.0, 1.0);
    return {
        1.0 + offset * (offset < 0 ? 0.22 : 0.26),
        1.0 + offset * (offset < 0 ? 0.04 : -0.02),
        1.0 - 0.30 * weak - 0.22 * heavy,
        1.0 - 0.35 * weak - 0.25 * heavy
    };
}

inline const wchar_t* BodyDescription(const State& state) noexcept {
    if (state.bodyCondition < 20) return L"아주 마른 허약냥";
    if (state.bodyCondition < 35) return L"마른 고양이";
    if (state.bodyCondition < 65) return L"적당한 체형";
    if (state.bodyCondition < 80) return L"통통한 고양이";
    return L"비만 고양이";
}

inline std::string Serialize(const State& state) {
    if (!IsValid(state)) return {};
    std::ostringstream out;
    out.imbue(std::locale::classic());
    out << "schema=1\n" << "born_utc=" << state.bornUtc << '\n'
        << "updated_utc=" << state.updatedUtc << '\n' << std::fixed << std::setprecision(9)
        << "body_condition=" << state.bodyCondition << '\n'
        << "fullness=" << state.fullness << '\n'
        << "last_meal_utc=" << state.lastMealUtc << '\n'
        << "last_treat_utc=" << state.lastTreatUtc << '\n'
        << "last_request_utc=" << state.lastRequestUtc << '\n';
    const std::string body = out.str();
    out << "checksum=" << std::hex << std::setw(16) << std::setfill('0') << cat::Checksum(body) << '\n';
    return out.str();
}

inline bool Parse(const std::string& text, State& destination, std::string& error) {
    const auto fail = [&](const char* why) { error = why; return false; };
    if (text.empty() || text.size() > 4096) return fail("Empty or oversized nutrition save");
    const auto checksumAt = text.rfind("checksum=");
    if (checksumAt == std::string::npos || checksumAt == 0 || text[checksumAt - 1] != '\n')
        return fail("Missing nutrition checksum");
    const std::string body = text.substr(0, checksumAt);
    const auto firstNewline = body.find('\n');
    const bool crlf = firstNewline > 0 && body[firstNewline - 1] == '\r';
    std::string hash = text.substr(checksumAt + 9);
    if (!hash.empty() && hash.back() == '\n') {
        hash.pop_back();
        const bool hashCrlf = !hash.empty() && hash.back() == '\r';
        if (hashCrlf != crlf) return fail("Inconsistent nutrition line endings");
        if (hashCrlf) hash.pop_back();
    }
    if (hash.size() != 16 || hash.find_first_not_of("0123456789abcdefABCDEF") != std::string::npos)
        return fail("Malformed nutrition checksum");
    std::uint64_t expected = 0;
    std::istringstream hashIn(hash);
    hashIn.imbue(std::locale::classic());
    hashIn >> std::hex >> expected;
    // Hash the original bytes: newline changes are edits, not silent repairs.
    if (!hashIn || expected != cat::Checksum(body)) return fail("Nutrition checksum mismatch");

    std::map<std::string, std::string> entries;
    std::istringstream lines(body);
    std::string line;
    while (std::getline(lines, line)) {
        if (crlf) {
            if (line.empty() || line.back() != '\r') return fail("Inconsistent nutrition line endings");
            line.pop_back();
        }
        if (line.find('\r') != std::string::npos) return fail("Malformed nutrition line ending");
        const auto split = line.find('=');
        if (split == std::string::npos || split == 0 ||
            !entries.emplace(line.substr(0, split), line.substr(split + 1)).second)
            return fail("Malformed or duplicate nutrition field");
    }
    const char* fields[] = {"schema", "born_utc", "updated_utc", "body_condition", "fullness",
        "last_meal_utc", "last_treat_utc", "last_request_utc"};
    if (entries.size() != 8) return fail("Unexpected nutrition fields");
    for (const char* key : fields)
        if (!entries.count(key)) return fail("Missing nutrition field");
    if (entries.at("schema") != "1") return fail("Unsupported nutrition version");

    const auto integer = [&](const char* key, std::int64_t& value) {
        const std::string& input = entries.at(key);
        if (input.empty() || input.size() > 18 || (input.size() > 1 && input[0] == '0') ||
            input.find_first_not_of("0123456789") != std::string::npos) return false;
        std::istringstream in(input);
        in.imbue(std::locale::classic());
        in >> value;
        return in && in.peek() == std::char_traits<char>::eof();
    };
    const auto real = [&](const char* key, double& value) {
        const std::string& input = entries.at(key);
        if (input.empty() || input.size() > 32 || input.front() == '.' || input.back() == '.' ||
            input.find_first_not_of("0123456789.") != std::string::npos ||
            (input.size() > 1 && input[0] == '0' && input[1] != '.')) return false;
        const auto point = input.find('.');
        if (point != std::string::npos && input.find('.', point + 1) != std::string::npos) return false;
        std::istringstream in(input);
        in.imbue(std::locale::classic());
        in >> value;
        return in && in.peek() == std::char_traits<char>::eof() && std::isfinite(value);
    };
    State parsed;
    if (!integer("born_utc", parsed.bornUtc) || !integer("updated_utc", parsed.updatedUtc) ||
        !integer("last_meal_utc", parsed.lastMealUtc) || !integer("last_treat_utc", parsed.lastTreatUtc) ||
        !integer("last_request_utc", parsed.lastRequestUtc)) return fail("Invalid nutrition timestamp");
    if (!real("body_condition", parsed.bodyCondition) || !real("fullness", parsed.fullness))
        return fail("Invalid nutrition number");
    if (!IsValid(parsed)) return fail("Nutrition value out of range");
    destination = parsed;
    error.clear();
    return true;
}

} // namespace cat::nutrition
