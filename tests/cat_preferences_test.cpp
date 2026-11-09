#include "../src/cat_preferences.hpp"

#include <cwchar>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {
int checks = 0;
#define CHECK(condition) do { ++checks; if (!(condition)) throw std::runtime_error( \
    std::string(__FILE__) + ":" + std::to_string(__LINE__) + " failed: " #condition); } while (false)

std::string Signed(const std::string& body) {
    std::ostringstream out;
    out << body << "checksum=" << std::hex << std::setw(16) << std::setfill('0') << cat::Checksum(body) << '\n';
    return out.str();
}

void RoundTripAllAppearances() {
    for (const auto character:cat::kAvailableCharacters) {
        for (int oi=0;oi<cat::kOutfitCount;++oi) {
            const auto outfit=static_cast<cat::Outfit>(oi);
            cat::Preferences original{character, outfit}, restored;
            const auto text = cat::SerializePreferences(original);
            CHECK(text.rfind("schema=2\n", 0) == 0);
            CHECK(cat::ParsePreferences(text, restored));
            CHECK(restored.character == character);
            CHECK(restored.outfit == outfit);
            CHECK(cat::SerializePreferences(restored) == text);
        }
    }
    CHECK(std::wcscmp(cat::OutfitName(cat::Outfit::BowTie), L"빨간 리본") == 0);
    CHECK(std::wcscmp(cat::OutfitName(cat::Outfit::Scarf), L"포근한 목도리") == 0);
    CHECK(std::wcscmp(cat::OutfitName(cat::Outfit::Crown), L"쪼꼬미 왕관") == 0);
}

void LegacySchemaOneStillLoads() {
    for (const auto& pair : {std::pair<const char*,cat::Character>{"cream",cat::Character::Cream},
                             std::pair<const char*,cat::Character>{"silver_tabby",cat::Character::SilverTabby}}) {
        const auto body = std::string("schema=1\ncharacter=") + pair.first + "\n";
        cat::Preferences restored{cat::Character::Cream, cat::Outfit::Crown};
        CHECK(cat::ParsePreferences(Signed(body), restored));
        CHECK(restored.character == cat::Character::Cream);
        CHECK(restored.outfit == cat::Outfit::None);
    }
}

void InvalidDataDoesNotMutate() {
    const cat::Preferences original{cat::Character::SilverTabby, cat::Outfit::Scarf};
    for (const auto& body : {std::string("schema=2\ncharacter=orange\noutfit=none\n"),
                             std::string("schema=2\ncharacter=cream\noutfit=spacesuit\n"),
                             std::string("schema=3\ncharacter=cream\noutfit=none\n"),
                             std::string("schema=2\ncharacter=cream\noutfit=none\nextra=x\n")}) {
        auto destination = original; std::string error;
        CHECK(!cat::ParsePreferences(Signed(body), destination, error));
        CHECK(!error.empty());
        CHECK(destination.character == original.character && destination.outfit == original.outfit);
    }
    auto text = cat::SerializePreferences(original);
    text[text.find("scarf")] = 'S';
    auto destination = original; std::string error;
    CHECK(!cat::ParsePreferences(text, destination, error));
    CHECK(destination.character == original.character && destination.outfit == original.outfit);
}

void PetRecordRemainsIndependent() {
    auto pet = cat::NewPet(cat::ToUnix({2026, 10, 31, 3, 14, 15}));
    cat::Feed(pet, pet.bornUtc + 86400, false);
    const auto saved = cat::Serialize(pet);
    cat::Preferences prefs{cat::Character::Cream, cat::Outfit::BowTie};
    CHECK(cat::ParsePreferences(cat::SerializePreferences(prefs), prefs));
    CHECK(cat::Serialize(pet) == saved);
    cat::Preferences destination; std::string error;
    CHECK(!cat::ParsePreferences(saved, destination, error));
}
void RetiredCharactersMigrateWithoutLosingOutfits() {
    CHECK(cat::kAvailableCharacters.size()==1);
    for(int i=0;i<cat::kLegacyCharacterCount;++i) {
        cat::Preferences old{static_cast<cat::Character>(i),cat::Outfit::Scarf},migrated;
        CHECK(cat::ParsePreferences(cat::SerializePreferences(old),migrated));
        CHECK(migrated.character==cat::Character::Cream);
        CHECK(migrated.outfit==cat::Outfit::Scarf);
        CHECK(cat::CharacterAvailable(old.character)==(i==0));
        CHECK(cat::ParsePreferences(cat::SerializePreferences(migrated),migrated));
    }
}
}

int main() {
    try {
        RoundTripAllAppearances();
        LegacySchemaOneStillLoads();
        InvalidDataDoesNotMutate();
        PetRecordRemainsIndependent();
        RetiredCharactersMigrateWithoutLosingOutfits();
        std::cout << "PASS: preferences schema 2 and legacy compatibility; " << checks << " checks\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
