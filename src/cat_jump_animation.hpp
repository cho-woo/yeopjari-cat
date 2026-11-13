#pragma once

#include "cat_deformation.hpp"
#include <array>
#include <cstdint>

namespace cat::jump_animation {
// One painted animal is skinned throughout anticipation, flight and contact.
// These are continuous joint controls, not additional authored PNG frames.
struct Controls { double crouch=0,fore=0,hind=0,tail=0; };
inline bool Finite(const Controls& c) {
    return std::isfinite(c.crouch)&&std::isfinite(c.fore)&&std::isfinite(c.hind)&&std::isfinite(c.tail);
}
inline Controls Bounded(Controls c) {
    if(!Finite(c))return {};
    c.crouch=std::clamp(c.crouch,0.,1.);c.fore=std::clamp(c.fore,0.,1.);
    c.hind=std::clamp(c.hind,0.,1.);c.tail=std::clamp(c.tail,0.,1.);return c;
}
inline Controls Blend(Controls a,Controls b,double t) {
    t=cat::deform::Smooth(t);
    return {a.crouch+(b.crouch-a.crouch)*t,a.fore+(b.fore-a.fore)*t,
        a.hind+(b.hind-a.hind)*t,a.tail+(b.tail-a.tail)*t};
}
// The immutable texture is the real entrance's final three-quarter drawing.
constexpr unsigned kSourceFrame=29;
constexpr std::uint64_t kPreparationMs=300,kContactReleaseMs=440;
// A travelling hop keeps its short input response and real momentum, but
// must still reach the authored push-off pose before physics takes over.
inline std::uint64_t PreparationSourceClock(std::uint64_t elapsed,std::uint64_t physicalDuration,
                                           std::uint64_t sourceDuration) {
    if(!sourceDuration)return 0;
    if(!physicalDuration||elapsed>=physicalDuration)return sourceDuration-1;
    return std::min(sourceDuration-1,elapsed*sourceDuration/physicalDuration);
}
inline Controls Preparation(std::uint64_t elapsed,std::uint64_t duration=kPreparationMs) {
    const double t=duration?std::clamp(static_cast<double>(elapsed)/duration,0.,1.):1.;
    // Load the hindlegs, then push before the physical takeoff. Both ends are
    // exactly neutral, so neither entrance nor takeoff swaps body/camera.
    const double crouch=t<.64?cat::deform::Smooth(t/.64):1-cat::deform::Smooth((t-.64)/.36);
    return {.82*crouch,0,0,.12*crouch};
}
inline Controls Contact(std::uint64_t elapsed,double strength) {
    const double t=static_cast<double>(elapsed);
    const double impact=std::isfinite(strength)?std::clamp(strength,.15,1.):.5;
    const double compression=t<100?cat::deform::Smooth(t/100):1-cat::deform::Smooth((t-100)/(kContactReleaseMs-100));
    return {.82*impact*compression,0,0,.10*compression};
}
// Observes the same interpolated physical pose that is drawn. In-flight
// control advances with actual velocity/clearance, so pauses, a high pointer
// hop and a small pounce never race through a nominal animation clock.
class Driver {
public:
    void Reset(double supportHeight=0) {ground_=supportHeight;launch_=0;progress_=0;air_=false;last_={};contactStart_={};}
    void Ground(double height) {if(std::isfinite(height))ground_=height;air_=false;progress_=0;last_={};}
    Controls Prepare(std::uint64_t elapsed,std::uint64_t duration,double height) {
        Ground(height);return last_=Preparation(elapsed,duration);
    }
    Controls Flight(double velocityY,double height,double bodyScale=1) {
        const double vy=std::isfinite(velocityY)?velocityY:0;
        if(!air_){air_=true;launch_=std::max(40.,vy);}
        launch_=std::max(launch_,vy);
        const double speed=std::max(40.,launch_);
        // Authored flight poses follow this particular physical impulse.
        // The old fixed +/-200px/s clock clamped both ends of a normal
        // ~315px/s jump, holding its first/last flight drawing for ~130ms.
        progress_=std::clamp((speed-vy)/(2*speed),0.,1.);
        const double rise=cat::deform::Smooth((speed-vy)/(.62*speed));
        const double reach=cat::deform::Smooth((speed+vy)/(.62*speed));
        const double clearance=std::isfinite(height)?std::max(0.,height-ground_):0;
        const double lift=cat::deform::Smooth(clearance/(10*std::clamp(bodyScale,.1,5.)));
        const double tuck=rise*reach*lift;
        return last_={0,.84*tuck,.70*tuck,.22*tuck};
    }
    double SourceFlightProgress() const {return progress_;}
    Controls Land(std::uint64_t elapsed,double strength,double height) {
        if(air_){contactStart_=last_;air_=false;}
        if(std::isfinite(height))ground_=height;
        const auto contact=Contact(elapsed,strength);
        auto settled=Blend(contactStart_,contact,static_cast<double>(elapsed)/70);
        // Flight has no compression. Contact already eases this channel from
        // zero; multiplying by a second ramp delayed it and caused a sharp
        // catch-up around70ms. Only the airborne paw/tail offsets need blending.
        settled.crouch=contact.crouch;
        return last_=settled;
    }
private:
    double ground_=0,launch_=0,progress_=0;bool air_=false;
    Controls last_{},contactStart_{};
};

using Point=cat::deform::Point;
struct Leg {Point root,knee,paw;double radius=0;};
// Landmarks refer to source400/frame29, in its complete registered cell.
constexpr std::array<Leg,4> kLegs{{
    {{.53,.69},{.48,.83},{.466,.941},.105}, // near foreleg
    {{.75,.66},{.744,.79},{.777,.901},.090},// far foreleg
    {{.365,.69},{.320,.83},{.360,.929},.100},// near hindleg
    {{.570,.72},{.545,.84},{.590,.923},.085}// far hindleg
}};
inline double Distance(Point a,Point b){return std::hypot(a.x-b.x,a.y-b.y);}
inline Point KneeFor(const Leg& leg,Point paw) {
    const double upper=Distance(leg.root,leg.knee),lower=Distance(leg.knee,leg.paw);
    const double dx=paw.x-leg.root.x,dy=paw.y-leg.root.y;
    const double distance=std::clamp(std::hypot(dx,dy),std::abs(upper-lower)+1e-7,upper+lower-1e-7);
    const double along=(upper*upper-lower*lower+distance*distance)/(2*distance);
    const double across=std::sqrt(std::max(0.,upper*upper-along*along));
    const double norm=std::max(1e-8,std::hypot(dx,dy)),ux=dx/norm,uy=dy/norm;
    const double side=(leg.paw.x-leg.root.x)*(leg.knee.y-leg.root.y)-
        (leg.paw.y-leg.root.y)*(leg.knee.x-leg.root.x)>=0?1.:-1.;
    return {leg.root.x+ux*along-uy*across*side,leg.root.y+uy*along+ux*across*side};
}
inline Point PointFor(Point uv,Controls controls) {
    if(!std::isfinite(uv.x)||!std::isfinite(uv.y)||!Finite(controls))return uv;
    const auto c=Bounded(controls);Point out=uv;
    // Head pixels translate rigidly; only the chest/pelvis compress. The
    // registered supporting paws are invariant while loading or landing.
    out.y+=.052*c.crouch*cat::deform::Smooth((.955-uv.y)/.25);
    double sum=0,dx=0,dy=0;
    for(unsigned i=0;i<kLegs.size();++i) {
        const auto& leg=kLegs[i];const double amount=i<2?c.fore:c.hind;
        if(amount==0||uv.y<=leg.root.y-.02)continue;
        Point target=leg.paw;
        target.x+=(i==0?.035:i==1?-.027:i==2?.028:-.023)*amount;
        target.y-=(i<2?.033:.027)*amount;
        const auto ik=KneeFor(leg,target);
        const Point knee{leg.knee.x+(ik.x-leg.knee.x)*.55,leg.knee.y+(ik.y-leg.knee.y)*.55};
        const double t=std::clamp((uv.y-leg.root.y)/(leg.paw.y-leg.root.y),0.,1.);
        const double kt=(leg.knee.y-leg.root.y)/(leg.paw.y-leg.root.y);
        const double centre=t<kt?leg.root.x+(leg.knee.x-leg.root.x)*t/kt:
            leg.knee.x+(leg.paw.x-leg.knee.x)*(t-kt)/(1-kt);
        const double weight=cat::deform::Smooth((leg.radius-std::abs(uv.x-centre))/.045)*
            cat::deform::Smooth((uv.y-leg.root.y)/.065);
        const Point shift=t<kt?Point{(knee.x-leg.knee.x)*t/kt,(knee.y-leg.knee.y)*t/kt}:
            Point{(knee.x-leg.knee.x)*(1-t)/(1-kt)+(target.x-leg.paw.x)*(t-kt)/(1-kt),
                  (knee.y-leg.knee.y)*(1-t)/(1-kt)+(target.y-leg.paw.y)*(t-kt)/(1-kt)};
        dx+=shift.x*weight;dy+=shift.y*weight;sum+=weight;
    }
    out.x+=.70*dx/std::max(1.,sum);out.y+=.70*dy/std::max(1.,sum);
    // The tail root stays attached. Its tip follows the push a little later.
    const double tail=cat::deform::Bump(uv.x,.27,.11)*cat::deform::Bump(uv.y,.61,.22);
    out.x+=.009*c.tail*tail;out.y-=.025*c.tail*tail;
    return out;
}
inline bool Warp(const std::uint32_t* src,int width,int height,std::uint32_t* dst,Controls controls) {
    if(!src||!dst||src==dst||width<1||height<1||width>4096||height>4096||!Finite(controls))return false;
    constexpr int cells=40,side=cells+1;
    std::array<cat::deform::detail::Vertex,side*side> mesh;
    for(int y=0;y<=cells;++y)for(int x=0;x<=cells;++x){
        const Point uv{static_cast<double>(x)/cells,static_cast<double>(y)/cells};const auto p=PointFor(uv,controls);
        mesh[y*side+x]={{p.x*width,p.y*height},uv};
    }
    std::memset(dst,0,static_cast<std::size_t>(width)*height*4);
    for(int y=0;y<cells;++y)for(int x=0;x<cells;++x){
        const auto& a=mesh[y*side+x];const auto& b=mesh[y*side+x+1];
        const auto& c=mesh[(y+1)*side+x];const auto& d=mesh[(y+1)*side+x+1];
        cat::deform::detail::Triangle(a,b,d,src,width,height,width*4,dst,width*4);
        cat::deform::detail::Triangle(a,d,c,src,width,height,width*4,dst,width*4);
    }
    return true;
}
} // namespace cat::jump_animation
