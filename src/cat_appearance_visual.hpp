#pragma once

#include "cat_nutrition.hpp"
#include "cat_preferences.hpp"

namespace cat::appearance {

// Painted actors already contain their body/head proportions. Stretching the
// complete drawing for weight also stretches the face and changes painted
// stride/paw geometry. Keep their reference shape; care state and its actual
// movement/jump effects still come from the unchanged nutrition model.
inline nutrition::Visual VisualModifiers(const nutrition::State& state,Character character) noexcept {
    auto visual=nutrition::VisualModifiers(state);
    if(CharacterAvailable(character))visual.bodyWidth=visual.bodyHeight=1;
    return visual;
}

} // namespace cat::appearance
