#pragma once
#include "cat_preferences.hpp"
#include "cat_expression.hpp"

namespace cat::species {
// Subtle physical tendencies multiply the existing age/nutrition/personality
// models. Choosing an appearance never replaces the pet's accumulated care.
struct Profile {
    double jumpHeightMultiplier = 1, movementSpeedMultiplier = 1;
    double foodInterest = 1, talkFrequency = 1, cursorInterest = 1;
};
inline Profile ProfileFor(Character character) noexcept {
    switch (character) {
    case Character::Black: return {1.06,1.04,.98,.85,1.12};
    case Character::Calico: return {1.02,1.02,1.02,1.10,1.10};
    case Character::Siamese: return {1.10,1.08,.97,1.25,1.15};
    case Character::Munchkin: return {.78,.90,1.04,1.0,1.05};
    case Character::Dog: return {.90,1.08,1.15,1.10,1.18};
    case Character::Rabbit: return {1.18,1.10,.95,.55,1.10};
    case Character::Tuxedo: return {}; // Same motion/physics profile as Cream.
    case Character::British: return {.88,.88,1.06,.75,.85};
    default: return {};
    }
}
inline bool UsesNewArt(Character character) noexcept { return character!=Character::Tuxedo && IsValid(character) && static_cast<int>(character) >= 2; }
inline int ArtCell(Character character) noexcept { return UsesNewArt(character) ? static_cast<int>(character)-2 : 0; }
// Measured against alpha-cropped canonical cells, including rabbit ears and
// the puppy's shorter muzzle. Reusing cat eye coordinates would blink a nose.
inline expression::Landmarks FaceFor(Character character) noexcept {
    switch(character){
    case Character::Black:return {{.59,.39},{.839,.344},{.74,.52}};
    case Character::Calico:return {{.607,.409},{.835,.377},{.74,.53}};
    case Character::Siamese:return {{.598,.364},{.839,.330},{.75,.49}};
    case Character::Munchkin:return {{.617,.421},{.836,.389},{.75,.55}};
    case Character::Dog:return {{.528,.301},{.766,.255},{.73,.47}};
    case Character::Rabbit:return {{.557,.471},{.831,.452},{.72,.59}};
    case Character::Tuxedo:return {{.60,.37},{.833,.33},{.74,.50}};
    case Character::British:return {{.586,.346},{.825,.307},{.74,.48}};
    default:return {};
    }
}
inline expression::Landmarks ClosedFaceFor(Character character) noexcept {
    switch(character){
    case Character::Cream:return {{.413,.481},{.760,.424},{.57,.60}};
    case Character::SilverTabby:return {{.389,.471},{.756,.414},{.57,.60}};
    case Character::Black:return {{.50,.47},{.82,.39},{.74,.52}};
    case Character::Calico:return {{.55,.467},{.83,.386},{.74,.53}};
    case Character::Siamese:return {{.51,.476},{.85,.405},{.75,.49}};
    case Character::Munchkin:return {{.526,.505},{.834,.429},{.75,.55}};
    case Character::Dog:return {{.472,.38},{.769,.262},{.73,.47}};
    case Character::Rabbit:return {{.556,.557},{.875,.495},{.72,.59}};
    case Character::Tuxedo:return {{.543,.471},{.844,.395},{.74,.50}};
    case Character::British:return {{.517,.471},{.817,.39},{.74,.48}};
    default:return {};
    }
}
} // namespace cat::species
