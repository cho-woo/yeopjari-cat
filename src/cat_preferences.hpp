#pragma once

#include "pet_state.hpp"
#include <iomanip>
#include <array>
#include <locale>
#include <sstream>
#include <string>

namespace cat {

// Appearance is independent of birthday and care records. Schema 2 adds outfits
// while remaining fully compatible with schema-1 character-only saves.
enum class Character { Cream = 0, SilverTabby = 1, Black = 2, Calico = 3, Siamese = 4, Munchkin = 5, Dog = 6, Rabbit = 7, Tuxedo = 8, British = 9 };
constexpr int kCharacterCount = 10; // Serialized ID range, not available menu entries.
constexpr int kLegacyCharacterCount = kCharacterCount;
// Stable retired IDs remain readable in old records. Only Cream/Tuxedo are offered
// or loaded by the application; changing appearance never replaces a pet.
constexpr std::array<Character,2> kAvailableCharacters{{Character::Cream,Character::Tuxedo}};
inline bool CharacterAvailable(Character c) noexcept { return c==Character::Cream || c==Character::Tuxedo; }
inline Character AvailableCharacter(Character c) noexcept { return c==Character::Tuxedo?c:Character::Cream; }
enum class Outfit { None = 0, BowTie = 1, Scarf = 2, Crown = 3, Santa = 4, FishHat = 5, Tie = 6, Hoodie = 7, Sunglasses = 8, Shark = 9, Dinosaur = 10, Pumpkin = 11, Birthday = 12 };
constexpr int kOutfitCount = 13;
struct Preferences { Character character = Character::Cream; Outfit outfit = Outfit::None; };

inline bool IsValid(Character character) noexcept {
    return static_cast<int>(character) >= 0 && static_cast<int>(character) < kLegacyCharacterCount;
}
inline bool IsValid(Outfit outfit) noexcept {
    return static_cast<int>(outfit) >= 0 && static_cast<int>(outfit) < kOutfitCount;
}

inline const wchar_t* CharacterName(Character character) noexcept {
    switch (character) {
        case Character::Cream: return L"크림 치즈냥";
        case Character::SilverTabby: return L"실버 태비냥";
        case Character::Black: return L"까만냥";
        case Character::Calico: return L"삼색냥";
        case Character::Siamese: return L"샴냥";
        case Character::Munchkin: return L"먼치킨냥";
        case Character::Dog: return L"옆자리 멍멍이";
        case Character::Rabbit: return L"옆자리 토끼";
        case Character::Tuxedo: return L"턱시도냥";
        case Character::British: return L"브리티시냥";
        default: return L"알 수 없는 고양이";
    }
}
inline const wchar_t* OutfitName(Outfit outfit) noexcept {
    switch (outfit) {
        case Outfit::None: return L"아무것도 안 입기";
        case Outfit::BowTie: return L"빨간 리본";
        case Outfit::Scarf: return L"포근한 목도리";
        case Outfit::Crown: return L"쪼꼬미 왕관";
        case Outfit::Santa: return L"산타 모자";
        case Outfit::FishHat: return L"붕어빵 모자";
        case Outfit::Tie: return L"회사원 넥타이";
        case Outfit::Hoodie: return L"포근한 후드티";
        case Outfit::Sunglasses: return L"멋쟁이 선글라스";
        case Outfit::Shark: return L"상어 옷";
        case Outfit::Dinosaur: return L"공룡 옷";
        case Outfit::Pumpkin: return L"할로윈 호박 모자";
        case Outfit::Birthday: return L"생일 파티 모자";
        default: return L"알 수 없는 의상";
    }
}
inline const char* CharacterKey(Character c) noexcept {
    static constexpr std::array<const char*, kLegacyCharacterCount> keys{{"cream", "silver_tabby", "black", "calico", "siamese", "munchkin", "dog", "rabbit", "tuxedo", "british"}};
    return IsValid(c) ? keys[static_cast<std::size_t>(c)] : "cream";
}
inline const char* OutfitKey(Outfit o) noexcept {
    static constexpr std::array<const char*, kOutfitCount> keys{{"none", "bow_tie", "scarf", "crown", "santa", "fish_hat", "tie", "hoodie", "sunglasses", "shark", "dinosaur", "pumpkin", "birthday"}};
    return IsValid(o) ? keys[static_cast<std::size_t>(o)] : "none";
}
// Unlocks gate new menu selections; an already saved outfit is always retained.
inline bool OutfitAvailable(Outfit outfit, int month, bool birthday, bool seasonal = true) noexcept {
    if (!IsValid(outfit)) return false;
    if (outfit == Outfit::Santa) return seasonal && month == 12;
    if (outfit == Outfit::Pumpkin) return seasonal && month == 10;
    if (outfit == Outfit::Birthday) return seasonal && birthday;
    return true;
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
        bool characterFound = false, outfitFound = false;
        for (int i = 0; i < kLegacyCharacterCount; ++i) if (character == CharacterKey(static_cast<Character>(i))) {
            parsed.character = static_cast<Character>(i); characterFound = true; break;
        }
        for (int i = 0; i < kOutfitCount; ++i) if (outfit == OutfitKey(static_cast<Outfit>(i))) {
            parsed.outfit = static_cast<Outfit>(i); outfitFound = true; break;
        }
        if (!characterFound) return fail("Unsupported character");
        if (!outfitFound) return fail("Unsupported outfit");
    }
    parsed.character=AvailableCharacter(parsed.character);
    destination = parsed; error.clear(); return true;
}

inline bool ParsePreferences(const std::string& text, Preferences& destination) {
    std::string error; return ParsePreferences(text, destination, error);
}

} // namespace cat
