#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>

namespace cat::deform {
struct Point { double x = 0, y = 0; };
enum class Shape { Sitting, ReclineSide, ReclineBack, Airborne, Eating, Grooming, Treat, Stretch, TailChase };
struct Joints {
    double leftEar = 0, rightEar = 0, headNod = 0;
    double frontLeft = 0, frontRight = 0, tailRaise = 0, breath = 0;
    double crouch = 0;
};
inline bool Finite(const Joints& j) {
    return std::isfinite(j.leftEar) && std::isfinite(j.rightEar) && std::isfinite(j.headNod) &&
        std::isfinite(j.frontLeft) && std::isfinite(j.frontRight) && std::isfinite(j.tailRaise) && std::isfinite(j.breath) && std::isfinite(j.crouch);
}
inline double Smooth(double x) { x = std::clamp(x, 0.0, 1.0); return x*x*(3-2*x); }
inline double Bump(double x, double centre, double radius) {
    return Smooth(1-std::abs(x-centre)/radius);
}
inline Joints Bounded(Joints j) {
    j.leftEar = std::clamp(j.leftEar,0.0,1.0); j.rightEar = std::clamp(j.rightEar,0.0,1.0);
    j.headNod = std::clamp(j.headNod,-1.0,1.0);
    j.frontLeft = std::clamp(j.frontLeft,0.0,1.0); j.frontRight = std::clamp(j.frontRight,0.0,1.0);
    j.tailRaise = std::clamp(j.tailRaise,0.0,1.0); j.breath = std::clamp(j.breath,-1.0,1.0);
    j.crouch = std::clamp(j.crouch,0.0,1.0);
    return j;
}
inline bool Zero(const Joints& j) {
    return j.leftEar==0 && j.rightEar==0 && j.headNod==0 && j.frontLeft==0 &&
        j.frontRight==0 && j.tailRaise==0 && j.breath==0 && j.crouch==0;
}

// Forward deformation of a point in the canonical sitting sprite (base frame4),
// in normalized crop coordinates. Ear roots and lower body never move during
// an ear flick; no alternate head/body image or whole-sprite scaling is used.
inline Point SittingPoint(Point uv, const Joints& controls) {
    if (!std::isfinite(uv.x) || !std::isfinite(uv.y)) return {};
    if (!Finite(controls)) return uv;
    const auto j = Bounded(controls);
    Point result = uv;
    // Lower the whole head as a rigid shape, compress only the torso, and
    // leave the supporting paws at their original baseline. Global scaleY
    // would squash the eyes/ears along with the body during loaf/box sitting.
    result.y += .10*j.crouch*Smooth((.98-uv.y)/.33);
    const double earHeight = Smooth((.36-uv.y)/.30);
    const double left = j.leftEar * Bump(uv.x,.23,.19) * earHeight;
    const double right = j.rightEar * Bump(uv.x,.82,.18) * earHeight;
    result.x += .027*(left-right);
    result.y += .038*(left+right);

    // Tiny head tilt around the neck. The mask fades out before the chest, so
    // nodding does not drag paws or the ground contact point along with it.
    const double head = Smooth((.68-uv.y)/.15);
    const double angle = j.headNod * .035 * head;
    const double dx=uv.x-.57, dy=uv.y-.67;
    result.x += dx*(std::cos(angle)-1)-dy*std::sin(angle);
    result.y += dx*std::sin(angle)+dy*(std::cos(angle)-1);

    // Paws lift from attached forelegs, fading to zero at their upper roots.
    const double distal = Smooth((uv.y-.73)/.24);
    result.y -= .023*distal*(j.frontLeft*Bump(uv.x,.43,.12)+j.frontRight*Bump(uv.x,.69,.12));

    // The tail is at the left of canonical frame4. Its base remains fixed;
    // the tip curls upward and inward. Keep this small enough to prevent mesh
    // foldovers, and avoid the head/cheeks above the tail entirely.
    const double tail = Smooth((.33-uv.x)/.26) * Smooth((uv.y-.54)/.20) * Smooth((.98-uv.y)/.16);
    result.x += .015*j.tailRaise*tail;
    result.y -= .090*j.tailRaise*tail;

    // Breathing is a local chest expansion, not a floating whole-body offset.
    const double chest = Bump(uv.x,.57,.29)*Bump(uv.y,.76,.19);
    result.x += (uv.x-.57)*.025*j.breath*chest;
    result.y -= .007*j.breath*chest;
    return result;
}

// Dedicated reclining artwork supplies the anatomy. Deformation only adds
// small local movement; the supported belly/back edge never leaves the floor.
inline Point RecliningPoint(Point uv,const Joints& controls,Shape shape=Shape::ReclineBack) {
    if (!std::isfinite(uv.x) || !std::isfinite(uv.y)) return {};
    if (!Finite(controls)) return uv;
    const auto j=Bounded(controls);
    Point result=uv;
    const double supported=Smooth((.94-uv.y)/.08);
    if (shape==Shape::ReclineBack) {
        const double left=Bump(uv.x,.30,.12)*Bump(uv.y,.43,.18)+Bump(uv.x,.39,.12)*Bump(uv.y,.73,.16);
        const double right=Bump(uv.x,.56,.11)*Bump(uv.y,.41,.16)+Bump(uv.x,.61,.11)*Bump(uv.y,.66,.16);
        result.y-=.018*supported*(j.frontLeft*left+j.frontRight*right);
        result.x+=.006*supported*(j.frontLeft*left-j.frontRight*right);
    } else {
        const double paw=Bump(uv.x,.66,.13)*Bump(uv.y,.88,.10)*supported;
        result.x+=.009*j.frontLeft*paw;
        result.y-=.008*j.frontRight*paw;
    }
    const double belly=Bump(uv.x,.42,.29)*Bump(uv.y,.70,.23)*supported;
    result.y-=.007*j.breath*belly;
    result.x+=(uv.x-.42)*.015*j.breath*belly;
    const Point centre{shape==Shape::ReclineBack ? .79 : .77,.47};
    const double head=Bump(uv.x,centre.x,.21)*Bump(uv.y,centre.y,.37)*supported;
    const double angle=.018*j.headNod*head;
    const double dx=uv.x-centre.x,dy=uv.y-centre.y;
    result.x+=dx*(std::cos(angle)-1)-dy*std::sin(angle);
    result.y+=dx*std::sin(angle)+dy*(std::cos(angle)-1);
    return result;
}
// Canonical leap frame16: only distal forepaws tuck toward the body near the
// apex. The face, ears, tail, torso and hindlegs retain their original pixels.
inline Point AirbornePoint(Point uv,const Joints& controls) {
    if (!std::isfinite(uv.x) || !std::isfinite(uv.y)) return {};
    if (!Finite(controls)) return uv;
    const auto j=Bounded(controls);
    Point result=uv;
    const double nearPaw=j.frontLeft*Bump(uv.x,.85,.13)*Smooth((uv.y-.64)/.12)*Smooth((.91-uv.y)/.10);
    const double farPaw=j.frontRight*Bump(uv.x,.66,.14)*Smooth((uv.y-.77)/.10)*Smooth((.99-uv.y)/.08);
    result.x-=.024*nearPaw+.015*farPaw;
    result.y+=.020*nearPaw-.018*farPaw;
    return result;
}

// Care artwork has different neck/paw positions from sitting. Keep its bowl,
// treat packet, tail and supporting feet fixed, and move only the active parts.
// A single source face is retained; independent photographs are never blended.
inline Point CarePoint(Point uv,const Joints& controls,Shape shape) {
    if(!std::isfinite(uv.x)||!std::isfinite(uv.y))return {};
    if(!Finite(controls))return uv;
    const auto j=Bounded(controls);
    Point result=uv;
    const bool eating=shape==Shape::Eating;
    const double fixed=eating ? .86 : .90;
    if(uv.y>=fixed)return uv;
    const double head=eating ? Smooth((uv.x-.30)/.15)*Smooth((.84-uv.y)/.14)
                             : Smooth((.68-uv.y)/.12);
    const Point neck=eating ? Point{.68,.76} : Point{.62,.65};
    const double angle=.025*j.headNod*head;
    const double dx=uv.x-neck.x,dy=uv.y-neck.y;
    result.x+=dx*(std::cos(angle)-1)-dy*std::sin(angle);
    result.y+=dx*std::sin(angle)+dy*(std::cos(angle)-1);
    if(shape==Shape::Grooming) {
        const double paw=Bump(uv.x,.81,.14)*Bump(uv.y,.61,.17);
        result.x-=.010*j.frontRight*paw;
        result.y-=.018*j.frontRight*paw;
    } else if(shape==Shape::Treat) {
        const double tongue=Bump(uv.x,.70,.09)*Bump(uv.y,.54,.07);
        result.y+=.008*j.frontLeft*tongue;
    }
    return result;
}

inline Point GesturePoint(Point uv,const Joints& controls,Shape shape) {
    if(!std::isfinite(uv.x)||!std::isfinite(uv.y))return {};
    if(!Finite(controls))return uv;
    const auto j=Bounded(controls);
    Point result=uv;
    if(uv.y>=.94)return uv;
    if(shape==Shape::Stretch) {
        // The long forelegs remain planted; only the back/chest lengthens.
        const double back=Bump(uv.x,.40,.24)*Bump(uv.y,.48,.32);
        result.x-=.014*j.breath*back;
        result.y-=.010*j.breath*back;
    } else {
        // Look-back art keeps its facing; the tail curls toward the nose.
        const double tail=Bump(uv.x,.23,.23)*Bump(uv.y,.56,.25);
        result.x+=.018*j.tailRaise*tail;
        result.y-=.035*j.tailRaise*tail;
        const double head=Smooth((uv.x-.44)/.15)*Smooth((.65-uv.y)/.14);
        const double angle=.018*j.headNod*head;
        const double dx=uv.x-.70,dy=uv.y-.55;
        result.x+=dx*(std::cos(angle)-1)-dy*std::sin(angle);
        result.y+=dx*std::sin(angle)+dy*(std::cos(angle)-1);
    }
    return result;
}

inline Point DeformPoint(Point uv,const Joints& joints,Shape shape=Shape::Sitting) {
    return shape==Shape::Sitting ? SittingPoint(uv,joints) :
        shape==Shape::Airborne ? AirbornePoint(uv,joints) :
        shape==Shape::Eating || shape==Shape::Grooming || shape==Shape::Treat ? CarePoint(uv,joints,shape) :
        shape==Shape::Stretch || shape==Shape::TailChase ? GesturePoint(uv,joints,shape) :
        RecliningPoint(uv,joints,shape);
}

namespace detail {
struct Vertex { Point position, uv; };
inline double Cross(Point a,Point b,Point c) {
    return (b.x-a.x)*(c.y-a.y)-(b.y-a.y)*(c.x-a.x);
}
inline std::uint32_t Read(const std::uint32_t* src,int x,int y,int stride) {
    std::uint32_t pixel;
    std::memcpy(&pixel,reinterpret_cast<const unsigned char*>(src)+static_cast<std::size_t>(y)*stride+static_cast<std::size_t>(x)*4,4);
    return pixel;
}
inline std::uint32_t Sample(const std::uint32_t* src,int width,int height,int stride,Point uv) {
    const double sx=std::clamp(uv.x*width-.5,0.0,static_cast<double>(width-1));
    const double sy=std::clamp(uv.y*height-.5,0.0,static_cast<double>(height-1));
    const int x=static_cast<int>(sx), y=static_cast<int>(sy);
    const int x1=std::min(x+1,width-1), y1=std::min(y+1,height-1);
    const double fx=sx-x, fy=sy-y;
    const std::array<std::uint32_t,4> p{{Read(src,x,y,stride),Read(src,x1,y,stride),Read(src,x,y1,stride),Read(src,x1,y1,stride)}};
    const std::array<double,4> weights{{(1-fx)*(1-fy),fx*(1-fy),(1-fx)*fy,fx*fy}};
    std::uint32_t result=0;
    for (int channel=0;channel<4;++channel) {
        double value=0;
        for (int k=0;k<4;++k) value+=weights[k]*((p[k]>>(channel*8))&255);
        result|=static_cast<std::uint32_t>(std::clamp(std::lround(value),0L,255L))<<(channel*8);
    }
    return result;
}
inline void Triangle(Vertex a,Vertex b,Vertex c,const std::uint32_t* src,int width,int height,int stride,
                     std::uint32_t* dst,int dstStride) {
    double area=Cross(a.position,b.position,c.position);
    if (std::abs(area)<1e-12) return;
    if (area<0) { std::swap(b,c); area=-area; }
    const int x0=std::max(0,static_cast<int>(std::floor(std::min({a.position.x,b.position.x,c.position.x}))));
    const int x1=std::min(width-1,static_cast<int>(std::ceil(std::max({a.position.x,b.position.x,c.position.x}))));
    const int y0=std::max(0,static_cast<int>(std::floor(std::min({a.position.y,b.position.y,c.position.y}))));
    const int y1=std::min(height-1,static_cast<int>(std::ceil(std::max({a.position.y,b.position.y,c.position.y}))));
    const auto unchanged=[&](const Vertex& v) {
        return v.position.x==v.uv.x*width && v.position.y==v.uv.y*height;
    };
    const bool identity=unchanged(a) && unchanged(b) && unchanged(c);
    // Including shared edges on both triangles is watertight. Both interpolate
    // the same edge UVs; there is no source-over composition/double alpha.
    for (int y=y0;y<=y1;++y) for (int x=x0;x<=x1;++x) {
        const Point p{x+.5,y+.5};
        const double wa=Cross(b.position,c.position,p)/area;
        const double wb=Cross(c.position,a.position,p)/area;
        const double wc=1-wa-wb;
        if (wa < -1e-10 || wb < -1e-10 || wc < -1e-10) continue;
        const Point uv{a.uv.x*wa+b.uv.x*wb+c.uv.x*wc,a.uv.y*wa+b.uv.y*wb+c.uv.y*wc};
        const auto pixel=identity ? Read(src,x,y,stride) : Sample(src,width,height,stride,uv);
        std::memcpy(reinterpret_cast<unsigned char*>(dst)+static_cast<std::size_t>(y)*dstStride+static_cast<std::size_t>(x)*4,&pixel,4);
    }
}
} // namespace detail

// Positive byte strides, premultiplied BGRA (little-endian packed uint32_t).
// Destination has the same dimensions; the caller retains its reusable buffer.
// No heap allocation, atlas lookup or mutable global state. Active source and
// destination buffers must not overlap. Returns false without writing on invalid
// input; zero controls copy rows exactly (including zero-alpha colour channels).
inline bool Warp(const std::uint32_t* src,int width,int height,int strideBytes,
                 std::uint32_t* dst,int dstStrideBytes,const Joints& controls,Shape shape=Shape::Sitting) {
    if (!src || !dst || width<1 || height<1 || width>4096 || height>4096 ||
        strideBytes<width*4 || dstStrideBytes<width*4 || strideBytes>65536 || dstStrideBytes>65536 ||
        !Finite(controls) || static_cast<int>(shape)<0 || static_cast<int>(shape)>static_cast<int>(Shape::TailChase)) return false;
    const auto j=Bounded(controls);
    const auto a=reinterpret_cast<std::uintptr_t>(src), b=reinterpret_cast<std::uintptr_t>(dst);
    const auto sourceBytes=static_cast<std::size_t>(height-1)*strideBytes+width*4;
    const auto targetBytes=static_cast<std::size_t>(height-1)*dstStrideBytes+width*4;
    if (a>std::numeric_limits<std::uintptr_t>::max()-sourceBytes || b>std::numeric_limits<std::uintptr_t>::max()-targetBytes) return false;
    const bool overlaps=a<b+targetBytes && b<a+sourceBytes;
    if (overlaps && !(src==dst && strideBytes==dstStrideBytes && Zero(j))) return false;
    if (Zero(j)) {
        if (src==dst) return true;
        for (int y=0;y<height;++y)
            std::memcpy(reinterpret_cast<unsigned char*>(dst)+static_cast<std::size_t>(y)*dstStrideBytes,
                        reinterpret_cast<const unsigned char*>(src)+static_cast<std::size_t>(y)*strideBytes,static_cast<std::size_t>(width)*4);
        return true;
    }
    constexpr int cells=32, side=cells+1;
    std::array<detail::Vertex,side*side> mesh;
    for (int y=0;y<=cells;++y) for (int x=0;x<=cells;++x) {
        // A mesh edge lies exactly on the ear-root boundary. Interpolation may
        // therefore never carry an ear displacement into the lower body. The
        // head/chest and supporting-paw seams likewise keep crouching local.
        const double v=shape==Shape::Sitting
            ? (y<=12 ? .36*y/12 : y<=21 ? .36+.29*(y-12)/9 : y<=31 ? .65+.33*(y-21)/10 : 1.0)
            : shape==Shape::Airborne ? (y<=20 ? .64*y/20 : .64+.36*(y-20)/12)
            : shape==Shape::Eating ? (y<=28 ? .86*y/28 : .86+.14*(y-28)/4)
            : shape==Shape::Grooming || shape==Shape::Treat ? (y<=28 ? .90*y/28 : .90+.10*(y-28)/4)
            : (y<=30 ? .94*y/30 : .94+.06*(y-30)/2);
        const Point uv{static_cast<double>(x)/cells,v};
        const auto p=DeformPoint(uv,j,shape);
        mesh[y*side+x]={{p.x*width,p.y*height},uv};
    }
    for (int y=0;y<height;++y)
        std::memset(reinterpret_cast<unsigned char*>(dst)+static_cast<std::size_t>(y)*dstStrideBytes,0,static_cast<std::size_t>(width)*4);
    for (int y=0;y<cells;++y) for (int x=0;x<cells;++x) {
        const auto& a=mesh[y*side+x]; const auto& b=mesh[y*side+x+1];
        const auto& c=mesh[(y+1)*side+x]; const auto& d=mesh[(y+1)*side+x+1];
        detail::Triangle(a,b,d,src,width,height,strideBytes,dst,dstStrideBytes);
        detail::Triangle(a,d,c,src,width,height,strideBytes,dst,dstStrideBytes);
    }
    return true;
}
} // namespace cat::deform
