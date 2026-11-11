#pragma once

#include "cat_sprite_sequence.hpp"
#include "cat_render_geometry.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace cat::coat {
// A cached material on the original drawings: registration, silhouette, alpha,
// props, and animation frames are shared by both cats.
struct Region {
    double cx=.62,cy=.34,rx=.32,ry=.31;
    bool profile=false;
    double angle=0;
};
constexpr double kJumpEyeY[]{.40,.45,.51,.38,.35,.35,.31,.255,.35,.396,.48,.54,.43,.42,.47,.40};
inline Region Head(sprite_sequence::Clip clip,unsigned frame) {
    using C=sprite_sequence::Clip;
    // These forehead origins follow the authored eye line, including head tilt.
    // They affect material coordinates only; source registration never changes.
    static constexpr Region groom[]{
        {.647,.311,.32,.31,false,-.178},{.616,.309,.32,.31,false,-.207},
        {.589,.360,.32,.31,false,-.247},{.542,.375,.32,.31,false,-.239},
        {.624,.347,.32,.31,false,-.276},{.575,.333,.32,.31,false,-.391},
        {.533,.285,.32,.31,false,-.483},{.544,.375,.32,.31,false,-.239},
        {.656,.366,.32,.31,false,-.202},{.592,.362,.32,.31,false,-.232},
        {.545,.357,.32,.31,false,-.246},{.518,.379,.32,.31,false,-.231},
        {.607,.352,.32,.31,false,-.251},{.556,.359,.32,.31,false,-.235},
        {.548,.291,.32,.31,false,-.168},{.537,.283,.32,.31,false,-.178}};
    static constexpr Region fish[]{
        {.594,.279,.34,.32,false,-.149},{.556,.423,.34,.32,false,-.122},
        {.513,.279,.34,.32,false,-.142},{.503,.286,.34,.32,false,-.099},
        {.591,.272,.34,.32,false,-.141},{.561,.275,.34,.32,false,-.144},
        {.523,.280,.34,.32,false,-.112},{.513,.280,.34,.32,false,-.140}};
    static constexpr Region punch[]{
        {.575,.288,.33,.32,false,-.200},{.572,.285,.33,.32,false,-.204},
        {.570,.285,.33,.32,false,-.214},{.569,.286,.33,.32,false,-.209},
        {.572,.285,.33,.32,false,-.219},{.573,.283,.33,.32,false,-.238},
        {.575,.282,.33,.32,false,-.224},{.573,.285,.33,.32,false,-.245},
        {.573,.289,.33,.32,false,-.200},{.574,.291,.33,.32,false,-.194},
        {.572,.291,.33,.32,false,-.195},{.570,.291,.33,.32,false,-.182}};
    static constexpr Region eat[]{
        {.558,.280,.32,.31,false,-.160},{.637,.392,.32,.31,false,-.035},
        {.641,.427,.32,.31,false,-.014},{.619,.269,.32,.31,false,-.151},
        {.603,.260,.32,.31,false,-.158},{.605,.262,.32,.31,false,-.144},
        {.614,.269,.32,.31,false,-.179},{.600,.261,.32,.31,false,-.183},
        {.589,.243,.32,.31,false,-.208},{.599,.259,.32,.31,false,-.167},
        {.596,.273,.32,.31,false,-.195},{.566,.274,.32,.31,false,-.163}};
    static constexpr Region sleep[]{
        {.564,.313,.32,.32,false,-.165},{.528,.316,.32,.32,false,.170},
        {.508,.427,.32,.32,false,.259},{.389,.546,.31,.30,false,-.457},
        {.395,.516,.31,.30,false,-.513},{.401,.514,.31,.30,false,-.415},
        {.361,.491,.30,.29,false,.230},{.559,.487,.31,.30,false,.168}};
    static constexpr Region box[]{
        {.497,.298,.30,.31,false,-.140},{.557,.284,.29,.29,false,.128},
        {.419,.302,.30,.30,false,-.083},{.52,.50,.30,.24,true},
        {.539,.337,.30,.30,false,-.079},{.531,.236,.30,.30,false,.009},
        {.533,.287,.30,.30,false,.118},{.522,.362,.30,.30,false,.084}};
    switch(clip) {
    case C::Groom:return groom[frame%16];
    case C::Knead:case C::Reach:return {.5,.30,.34,.32,false};
    case C::BallPlay:case C::ToyPlay:return {.5,.30,.34,.32,false};
    case C::Brake:case C::Slide:case C::Stumble:case C::Climb:return {.73,.32,.24,.30,true};
    case C::Punch:return punch[frame%12];
    case C::Eat:return eat[frame%12];
    case C::Treat:return {.537,.295,.44,.34,false,-.16};
    case C::Fish:return fish[frame%8];
    case C::Box:return box[frame%8];
    case C::Walk:return {.73,.36,.24,.30,true};
    case C::Run:return {.74,.40,.25,.30,true};
    case C::Jump:
        if(frame==14)return {.627,.269,.30,.30,false,-.180};
        if(frame==15)return {.557,.270,.32,.31,false,-.128};
        return {frame==13?.65:.68,kJumpEyeY[frame%16]-.13,.28,.30,true};
    case C::Sleep:return sleep[frame%8];
    case C::None: {
        const auto a=render::AnchorsForFrame(static_cast<int>(frame));
        const bool profile=frame<4||frame==6||frame==7||frame==8||frame==11||frame==24||frame>=27;
        return {a.crown.x,(a.crown.y+a.neck.y)*.5-.08,a.headWidth*.55,
            (a.neck.y-a.crown.y)*.64,profile,frame==6?.20:frame==25?.48:0};
    }
    }
    return {};
}
inline bool Prop(sprite_sequence::Clip clip,unsigned frame,double x,double y) {
    using C=sprite_sequence::Clip;
    if(clip==C::Box) {
        if(frame==0)return x>.45&&y>.59;
        if(frame==1)return x<.58&&y>.57;
        if(frame==7&&x>.81&&y<.53+(x-.81)*1.2)return false;
        return y>.53;
    }
    if(clip==C::Eat)return x>.66&&y>.75;
    if(clip==C::Treat && frame>=4 && frame<=14)
        return (x>.56&&y>.10+x*.72&&y<.22+x*.78)||(x>.87&&y>.64);
    if(clip==C::Fish && frame<2)return y>.87;
    return false;
}
inline double Smooth(double x) {x=std::clamp(x,0.,1.);return x*x*(3-2*x);}
inline double Ellipse(double x,double y,double cx,double cy,double rx,double ry) {
    const double dx=(x-cx)/rx,dy=(y-cy)/ry;
    return 1-Smooth((dx*dx+dy*dy-.90)/.12);
}
inline std::uint32_t Pixel(std::uint32_t pixel,double x,double y,
                           sprite_sequence::Clip clip,unsigned frame) {
    using C=sprite_sequence::Clip;
    const unsigned a=pixel>>24;
    if(!a||Prop(clip,frame,x,y))return pixel;
    if(clip==C::None&&frame==7&&x>.57&&y>.81)return pixel;
    if(clip==C::None&&(frame==18||frame==19)&&x>.68&&y>.50)return pixel;
    const double r=((pixel>>16)&255)*255./a,g=((pixel>>8)&255)*255./a,b=(pixel&255)*255./a;
    // Brown crumbs/cardboard and the neutral silver fish are authored props.
    if(clip==C::Eat&&x>.52&&y>.46&&r<205&&r-g>25&&g-b>15)return pixel;
    if(clip==C::Box&&frame>=2&&y>.44&&r<215&&r-g>20&&g-b>15&&
        !((frame==2&&x>.74)||(frame==3&&x>.69)||(frame==7&&x>.81)))return pixel;
    if(clip==C::Fish && r-g<12&&g-b<12)return pixel;
    const auto head=Head(clip,frame);
    const double cosine=std::cos(head.angle),sine=std::sin(head.angle);
    const double dx=((x-head.cx)*cosine+(y-head.cy)*sine)/head.rx;
    const double dy=(-(x-head.cx)*sine+(y-head.cy)*cosine)/head.ry;
    // A wide white blaze opens into white cheeks below the eyes. Its boundary
    // follows the head, rather than the orange stripes in the source painting.
    const double blaze=head.profile?
        Smooth((.65-.80*Smooth((dy+.5)/.85)-dx)/.055):
        Smooth((std::abs(dx)-(.12+.48*Smooth((dy+.6)/1.0)))/.055);
    const double cap=blaze*(1-Smooth((dy-(head.profile?.62:.32))/.13))*
        (1-Smooth((std::abs(dx)-1.18)/.10));
    double body=0,tail=0;
    const bool side=clip==C::Walk||clip==C::Run||(clip==C::None&&frame<4);
    if(side) {
        body=Ellipse(x,y,.43,clip==C::Run?.59:.61,.28,.20)*
            (1-Smooth((x-.60)/.07));
        tail=(1-Smooth((x-.29)/.055))*Smooth((y-.20)/.06)*
            (1-Smooth((y-(clip==C::Run?.55:.66))/.04));
    } else if(clip==C::None) {
        if(frame==6||frame>=27)body=1;
        else if(frame==8||frame==11||frame==24)
            body=Ellipse(x,y,.36,.63,.34,.25);
        else if(frame==25)body=Smooth((y-.80)/.05);
        else body=(1-Smooth((x-(head.cx-head.rx*.62))/.06))*Smooth((y-.50)/.04);
        tail=(1-Smooth((x-.33)/.05));
        if(frame==26)tail=std::max(tail,Smooth((y-.80)/.03)*(1-Smooth((std::abs(x-.48)-.13)/.04)));
        if(frame==7)tail=Ellipse(x,y,.13,.23,.15,.25);
    } else if(clip==C::Jump&&frame<14) {
        const double spine=kJumpEyeY[frame%16]+.13;
        body=Ellipse(x,y,.46,spine+.075,.28,.15)*(1-Smooth((x-.60)/.04));
        tail=(1-Smooth((x-.31)/.045))*Smooth((y-.06)/.04)*
            (1-Smooth((y-(spine+.15))/.035));
    } else if(clip==C::Sleep&&frame>=3) {
        if(frame<6) {
            body=1;
            tail=Ellipse(x,y,.61,.84,.28,.09);
        } else if(frame==6) {
            body=Ellipse(x,y,.67,.65,.22,.14);
            tail=Ellipse(x,y,.77,.38,.16,.22);
        } else tail=Ellipse(x,y,.65,.80,.22,.13);
    } else if(clip==C::Box&&frame==7) {
        tail=Smooth((x-.79)/.02)*Smooth((y-.35)/.03);
    } else if(clip==C::Box&&frame==3) {
        body=1;tail=1;
    } else if(clip==C::Box&&frame<=2) {
        const bool right=frame!=0;
        const double tx=right?.84:.20;
        tail=frame==2?Smooth((x-.74)/.02)*(1-Smooth((y-.59)/.03)):
            Ellipse(x,y,tx,.76,.11,.19);
        body=right?Ellipse(x,y,.77,.63,.15,.17):0;
    } else {
        double tx=.30,ty=.78,rx=.115,ry=.16;
        if(clip==C::Treat){tx=.14;ty=.78;rx=.16;}
        if(clip==C::Eat){tx=.22;ty=.76;}
        if(clip==C::Sleep){tx=frame?.80:.20;ty=.77;}
        if(clip==C::Groom&&frame==13){tx=.45;ty=.81;rx=.26;ry=.14;}
        tail=Ellipse(x,y,tx,ty,rx,ry);
        if(clip==C::Groom&&(frame==10||frame==11))tail*=1-Smooth((x-.31)/.025);
        if(!(clip==C::Groom&&frame==13))
            tail=std::max(tail,(tx>.5?Smooth((x-(tx-rx+.015))/.025):
                1-Smooth((x-((clip==C::Groom&&(frame==10||frame==11))?.31:tx+rx-.015))/.025))*
                Smooth((y-.50)/.035)*(1-Smooth((y-.94)/.02)));
    }
    // The white face masks any torso material behind the tilted/sleeping head.
    const double face=1-Smooth((dx*dx+dy*dy-1.0)/.16);
    double black=std::max(cap,std::max(body,tail)*(1-face));
    if(clip==C::Box&&frame==3)black=1;
    if(clip==C::None) {
        if(frame==6||frame>=27)black*=1-Ellipse(x,y,frame==6?.68:.77,.93,.13,.10);
        else if(frame!=26)black*=1-Smooth((y-.91)/.045);
    }
    // Only actual skin is pink. Warm reflected fur and orange tabby stripes
    // never punch holes in the black cap or leave blush-colored ear rims.
    const bool skinRegion=(dy<-.12&&std::abs(dx)>.38)||
        (dy>.14&&dy<1.30&&std::abs(dx)<(head.profile?.85:.32))||y>.76;
    const double pink=skinRegion?Smooth((r-g-32)/15.)*
        Smooth(((r-g)-(g-b)*1.8-16)/15.):0;
    const bool eyeRegion=dy>-.05&&dy<.95&&std::abs(dx)<.85;
    const bool eye=eyeRegion&&r<170&&r-g>1&&g-b>-5;
    const bool ink=eyeRegion&&r<90;
    if(eye||ink)return pixel;
    black*=1-pink;
    // Red-channel shading removes orange stripe contrast. A compact range
    // leaves soft fur volume without ghost tabby bars in either coat material.
    const double light=std::clamp(255-(255-r)*1.2,0.,255.);
    const double dark=8+45*std::clamp((r+g)/510.,0.,1.);
    const double value=light+(dark-light)*black;
    const auto channel=[&](double original,double tint){
        return static_cast<unsigned>(std::clamp(std::lround(((value+tint)*(1-pink)+original*pink)*a/255.),0l,static_cast<long>(a)));
    };
    return (a<<24)|(channel(r,-black)<<16)|(channel(g,0)<<8)|channel(b,black*1.5);
}
inline void Apply(std::uint32_t* pixels,int width,int height,sprite_sequence::Clip clip,unsigned frame) {
    if(!pixels||width<=0||height<=0)return;
    for(int y=0;y<height;++y)for(int x=0;x<width;++x)
        pixels[static_cast<std::size_t>(y)*width+x]=Pixel(pixels[static_cast<std::size_t>(y)*width+x],
            (x+.5)/width,(y+.5)/height,clip,frame);
}
} // namespace cat::coat
