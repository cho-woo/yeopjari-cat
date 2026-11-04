#pragma once

#include "pet_state.hpp"
#include <iomanip>
#include <locale>
#include <sstream>
#include <string>

namespace cat {

// Appearance is independent of birthday and care records. Schema 2 adds outfits
// while remaining fully compatible with schema-1 character-only saves.
enum class Character { Cream = 0, SilverTabby = 1 };
enum class Outfit { None = 0, BowTie = 1, Scarf = 2, Crown = 3 };
struct Preferences { Character character = Character::Cream; Outfit outfit = Outfit::None; };

inline bool IsValid(Character character) noexcept {
    return character == Character::Cream || character == Character::SilverTabby;
}
inline bool IsValid(Outfit outfit) noexcept {
    return outfit == Outfit::None || outfit == Outfit::BowTie || outfit == Outfit::Scarf || outfit == Outfit::Crown;
}

inline const wchar_t* CharacterName(Character character) noexcept {
    switch (character) {
        case Character::Cream: return L"크림 치즈냥";
        case Character::SilverTabby: return L"실버 태비냥";
        default: return L"알 수 없는 고양이";
    }
}
inline const wchar_t* OutfitName(Outfit outfit) noexcept {
    switch (outfit) {
        case Outfit::None: return L"아무것도 안 입기";
        case Outfit::BowTie: return L"빨간 리본";
        case Outfit::Scarf: return L"포근한 목도리";
        case Outfit::Crown: return L"쪼꼬미 왕관";
        default: return L"알 수 없는 의상";
    }
}
inline const char* CharacterKey(Character c) noexcept { return c == Character::SilverTabby ? "silver_tabby" : "cream"; }
inline const char* OutfitKey(Outfit o) noexcept {
    switch (o) { case Outfit::BowTie: return "bow_tie"; case Outfit::Scarf: return "scarf"; case Outfit::Crown: return "crown"; default: return "none"; }
}

inline std::string SerializePreferences(const Preferences& preferences) {
    if (!IsValid(preferences.character) || !IsValid(preferences.outfit)) return {};
    const std::string body = std::string("schema=2\ncharacter=") + CharacterKey(preferences.character) +
        "\noutfit=" + OutfitKey(preferences.outfit) + "\n";
    std::ostringstream out;
    out.imbue(std::locale::classic());
    out << body << "checksum=" << std::hex << std::setw(16) << std::setfill('0') << Checksum(body) << '\n';
    return out.str();
}

inline bool ParsePreferences(const std::string& text, Preferences& destination, std::string& error) {
    auto fail = [&](const char* why) { error = why; return false; };
    if (text.empty() || text.size() > 2048) return fail("Empty or oversized preferences");
    const auto checksumAt = text.rfind("checksum=");
    if (checksumAt == std::string::npos || checksumAt == 0 || text[checksumAt - 1] != '\n') return fail("Missing preference checksum");
    const std::string body = text.substr(0, checksumAt);
    std::string hash = text.substr(checksumAt + 9);
    if (!hash.empty() && hash.back() == '\n') { hash.pop_back(); if (!hash.empty() && hash.back() == '\r') hash.pop_back(); }
    if (hash.size() != 16 || hash.find_first_not_of("0123456789abcdefABCDEF") != std::string::npos) return fail("Malformed preference checksum");
    std::uint64_t expected = 0; std::istringstream input(hash); input.imbue(std::locale::classic()); input >> std::hex >> expected;
    if (!input || expected != Checksum(body)) return fail("Preference checksum mismatch");

    // Preserve the exact old format from v1.2-v1.8.
    Preferences parsed;
    if (body == "schema=1\ncharacter=cream\n" || body == "schema=1\r\ncharacter=cream\r\n") {
        parsed.character = Character::Cream; parsed.outfit = Outfit::None;
    } else if (body == "schema=1\ncharacter=silver_tabby\n" || body == "schema=1\r\ncharacter=silver_tabby\r\n") {
        parsed.character = Character::SilverTabby; parsed.outfit = Outfit::None;
    } else {
        std::istringstream lines(body); lines.imbue(std::locale::classic());
        std::string line, schema, character, outfit;
        while (std::getline(lines, line)) {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (line.rfind("schema=", 0) == 0) schema = line.substr(7);
            else if (line.rfind("character=", 0) == 0) character = line.substr(10);
            else if (line.rfind("outfit=", 0) == 0) outfit = line.substr(7);
            else if (!line.empty()) return fail("Unsupported preference field");
        }
        if (schema != "2") return fail("Unsupported preference schema");
        if (character == "cream") parsed.character = Character::Cream;
        else if (character == "silver_tabby") parsed.character = Character::SilverTabby;
        else return fail("Unsupported character");
        if (outfit == "none") parsed.outfit = Outfit::None;
        else if (outfit == "bow_tie") parsed.outfit = Outfit::BowTie;
        else if (outfit == "scarf") parsed.outfit = Outfit::Scarf;
        else if (outfit == "crown") parsed.outfit = Outfit::Crown;
        else return fail("Unsupported outfit");
    }
    destination = parsed; error.clear(); return true;
}

inline bool ParsePreferences(const std::string& text, Preferences& destination) {
    std::string error; return ParsePreferences(text, destination, error);
}

} // namespace cat
