#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>

// Frame-1 Cream/Silver tail registration only. The tail is painted separately
// from the head but merges into the back near u=.29,v=.56. Keep a short fur
// overlap at that root; rotate the distal texture behind the body plate.
namespace cat::tail_pose {
struct Point { double x=0,y=0; };
struct PixelParts { std::uint32_t body=0,tail=0; };
constexpr Point kPivot{.29,.56};
inline double Smooth(double t){t=std::clamp(t,0.,1.);return t*t*(3-2*t);}
inline double Weight(double u,double v){
 // Above v=.52 the source tail and body are distinct alpha components.
 // At v=.54 they meet. This overlap fades out before v=.585, preserving
 // the original back rather than removing a rectangular body fragment.
 return (1-Smooth((u-.278)/.016))*(1-Smooth((v-.52)/.065));
}
inline std::uint32_t Multiply(std::uint32_t pixel,double factor){
 factor=std::clamp(factor,0.,1.);std::uint32_t result=0;
 for(int shift=0;shift<32;shift+=8)
  result|=static_cast<unsigned>(std::lround(((pixel>>shift)&255)*factor))<<shift;
 return result;
}
inline PixelParts Split(std::uint32_t pixel,double u,double v){
 const double tail=Weight(u,v);
 if(tail<=0)return {pixel,0};
 if(tail>=1)return {0,pixel};
 const double body=1-tail;
 // Body is drawn over tail. Compensate the hidden tail opacity so the
 // neutral composition does not punch a translucent seam into opaque fur.
 const double remaining=1-(pixel>>24)/255.*body;
 return {Multiply(pixel,body),Multiply(pixel,tail/std::max(1e-12,remaining))};
}
// Inverse-map an already inverse-transformed torso UV to the source tail UV.
// A negative angle moves this right-facing cat's tail toward the left/back.
// Body dimensions keep a rigid rotation even when nutrition changes width.
inline Point SourceUV(Point uv,double degrees,double widthBody,double heightBody){
 if(!std::isfinite(degrees)||!std::isfinite(widthBody)||!std::isfinite(heightBody)||
    widthBody<=0||heightBody<=0)return uv;
 const double angle=std::clamp(degrees,-70.,15.)*3.14159265358979323846/180;
 const double cosine=std::cos(angle),sine=std::sin(angle);
 const double x=(uv.x-kPivot.x)*widthBody,y=(uv.y-kPivot.y)*heightBody;
 return {kPivot.x+(x*cosine+y*sine)/widthBody,
         kPivot.y+(-x*sine+y*cosine)/heightBody};
}
}
