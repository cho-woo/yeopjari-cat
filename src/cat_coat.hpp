#pragma once

#include "cat_sprite_sequence.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace cat::coat {
constexpr double kJumpEyeY[]{.40,.45,.51,.38,.35,.35,.31,.255,.35,.396,.48,.54,.43,.42,.47,.40};
// A render material, not extra drawings: both coats use the original source
// rectangles and alpha without resampling or moving a single pixel. Applied
// once when warming authored frames; never modifies the PNGs on disk.
struct Region { double cx=.57,cy=.32,rx=.28,ry=.31;bool profile=false; };
inline Region Head(sprite_sequence::Clip clip,unsigned frame) {
    using C=sprite_sequence::Clip;
    switch(clip) {
    case C::Groom:return {.62,.34,.31,.31,false};
    case C::Punch:return {.57,.34,.32,.32,false};
    case C::Eat:return {.51,.35,.30,.30,false};
    case C::Treat:return {.47,.35,.45,.35,false};
    case C::Fish:return {.57,.33,.34,.32,false};
    case C::Box:
        if(frame==0)return {.46,.32,.30,.31,false};
        if(frame==1)return {.63,.32,.29,.29,false};
        if(frame==3)return {.55,.38,.30,.24,true};
        return {.54,frame==2?.33:.43,.30,.30,false};
    case C::Walk:return {.73,.39,.23,.29,true};
    case C::Run:return {.75,.44,.24,.29,true};
    case C::Jump:return {frame<14?.68:.61,kJumpEyeY[frame%16]-.13,.28,.30,frame<14};
    case C::Sleep:
        if(frame<3)return {.56,.34,.32,.32,false};
        return {.39,.51,.31,.29,true};
    default:return {};
    }
}
inline bool Prop(sprite_sequence::Clip clip,unsigned frame,double x,double y) {
    using C=sprite_sequence::Clip;
    if(clip==C::Box) {
        if(frame==0)return x>.45&&y>.59;
        if(frame==1)return x<.58&&y>.57;
        return y>.53;
    }
    if(clip==C::Eat)return x>.61&&y>.68;
    // The hand and tube form a diagonal, rather than a rectangular area that
    // would also remove the cat's cheek from the material.
    if(clip==C::Treat && frame>=4 && frame<=14)return x>.60&&y>.52+(x-.60)*.7;
    if(clip==C::Fish && frame<2)return y>.87;
    return false;
}
inline double Smooth(double x) {x=std::clamp(x,0.,1.);return x*x*(3-2*x);}
inline std::uint32_t Pixel(std::uint32_t pixel,double x,double y,
                           sprite_sequence::Clip clip,unsigned frame) {
    const unsigned a=pixel>>24;
    if(!a||Prop(clip,frame,x,y))return pixel;
    const double r=((pixel>>16)&255)*255./a,g=((pixel>>8)&255)*255./a,b=(pixel&255)*255./a;
    if(clip==sprite_sequence::Clip::Eat&&x>.52&&y>.46&&r<205&&r-g>25&&g-b>15)return pixel;
    if(clip==sprite_sequence::Clip::Box&&frame>=2&&y>.44&&r<215&&r-g>20&&g-b>15)return pixel;
    const auto head=Head(clip,frame);
    const double dx=(x-head.cx)/head.rx,dy=(y-head.cy)/head.ry;
    // Protect features by both material and location. A hue-only hard cut
    // mistakes reflected pink highlights in white fur for skin and mottles it.
    const bool skinRegion=(dy<-.24&&std::abs(dx)>.38)||
        (dy>.1&&dy<1.1&&std::abs(dx)<.45)||y>.78;
    const double pink=skinRegion?Smooth((r-g-19)/20.)*Smooth(((r-g)-(g-b)*1.3-5)/18.):0;
    const double eyeX=head.profile?.48:dx<0?-.40:.50;
    double eyeDistance=std::pow((dx-eyeX)/.36,2)+std::pow((dy-.28)/.34,2);
    if(clip==sprite_sequence::Clip::Jump) {
        const double left=frame<14?.71:.55,right=frame<14?.855:.74,eyeY=kJumpEyeY[frame%16];
        eyeDistance=std::min(std::pow((x-left)/.057,2)+std::pow((y-eyeY)/.066,2),
            std::pow((x-right)/.052,2)+std::pow((y-eyeY+.04)/.062,2));
    }
    // The whole original eye (including the tiny white glints) must survive.
    const double eye=1-Smooth((eyeDistance-.75)/.35);
    if(r<115&&dy>-.05&&dy<.85)return pixel;
    if(clip==sprite_sequence::Clip::Fish && r-g<12&&g-b<12)return pixel;
    const double ellipse=1-Smooth((dx*dx+dy*dy-.85)/.25);
    const double cap=1-Smooth((dy-.18)/.16);
    const double blazeCenter=head.profile?.52:0.;
    const double blazeWidth=head.profile?.09:.035+.18*Smooth((dy+.75)/1.1);
    const double blaze=Smooth((std::abs(dx-blazeCenter)-blazeWidth)/.08);
    const double orange=Smooth((r-b-62)/36.)*Smooth((g-b-22)/20.);
    const bool side=clip==sprite_sequence::Clip::Walk||clip==sprite_sequence::Clip::Run;
    const double tail=(1-Smooth((x-(side?.24:.24))/.06))*Smooth((y-(side?.22:.53))/.08)*
        (1-Smooth((y-(side?.66:.91))/.05));
    const double back=side?1-Smooth((std::pow((x-.43)/.23,2)+std::pow((y-.60)/.19,2)-.65)/.55):0.;
    const bool canonical=clip==sprite_sequence::Clip::None;
    const double black=(canonical?orange:std::max({orange,ellipse*cap*blaze,tail,back}))*(1-pink)*(1-eye);
    const double luminance=.2126*r+.7152*g+.0722*b;
    const double white=std::min(255.,luminance+9),dark=9+std::pow(luminance/255.,5)*62;
    const double value=white+(dark-white)*black;
    const double preserve=std::max(pink,eye);
    const auto channel=[&](double original,double tint){
        // Light fur around an eye stays white; preserve only its iris color.
        if(eye>pink&&r>195)original=white;
        return static_cast<unsigned>(std::clamp(std::lround(((value+tint)*(1-preserve)+original*preserve)*a/255.),0l,static_cast<long>(a)));
    };
    return (a<<24)|(channel(r,-black)<<16)|(channel(g,0)<<8)|channel(b,black*2);
}
inline void Apply(std::uint32_t* pixels,int width,int height,sprite_sequence::Clip clip,unsigned frame) {
    if(!pixels||width<=0||height<=0)return;
    for(int y=0;y<height;++y)for(int x=0;x<width;++x)
        pixels[static_cast<std::size_t>(y)*width+x]=Pixel(pixels[static_cast<std::size_t>(y)*width+x],
            (x+.5)/width,(y+.5)/height,clip,frame);
}
} // namespace cat::coat
