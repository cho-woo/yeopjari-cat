#pragma once
#include "cat_sprite_sequence.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace cat::lively {
// Timing landmarks observed in retained Dropshot walk/run references. Smooth
// interpolation feeds the physical motor; foot placement follows real travel.
inline double Pace(std::uint64_t elapsed,bool running,unsigned seed) {
    if(running) {
        // Ordinary galloping varies gently. A repeated 62% trough looked
        // like an unintended brake in every burst; real turns own braking.
        constexpr double tau=6.283185307179586;
        const double time=static_cast<double>(elapsed)+(seed%19)*173.;
        return 1+.075*std::sin(time*tau/(3900+(seed%5)*211))+.025*std::sin(time*tau/1733);
    }
    constexpr std::array<double,8> walk{.78,.94,1.12,.90,1.16,.83,1.03,.78};
    const auto& keys=walk;
    const double period=4300.;
    const double phase=std::fmod(static_cast<double>(elapsed)+(seed%7)*173.,period)/period*7;
    const auto index=static_cast<unsigned>(phase);
    const double t=phase-index,smooth=t*t*(3-2*t);
    return keys[index]+(keys[index+1]-keys[index])*smooth;
}
inline cat::sprite_sequence::Clip StopClip(unsigned entropy) {
    using C=cat::sprite_sequence::Clip;
    return entropy%10<5?C::Brake:entropy%10<8?C::Slide:C::Stumble;
}
inline double Grip(cat::sprite_sequence::Clip clip) {
    using C=cat::sprite_sequence::Clip;
    return clip==C::Slide?.18:clip==C::Stumble?.38:1.25;
}
inline double ClimbSpeed(std::uint64_t elapsed) {
    if(elapsed<1100)return 0;
    if(elapsed<2050)return 62;
    if(elapsed<2600)return 12;
    if(elapsed<3650)return 70;
    if(elapsed<4300)return 10;
    return 0;
}
}
