#pragma once
#include "cat_deformation.hpp"

namespace cat::expression {
struct Face { double blink=0, wide=0, mouth=0, smile=0, brow=0; };
struct Landmarks {
    deform::Point leftEye{.40,.49}, rightEye{.72,.46}, mouth{.57,.60};
};
struct ClosedEyes { const std::uint32_t* pixels=nullptr; int width=0,height=0; Landmarks landmarks; };
inline Face Bounded(Face face) {
    auto bound=[](double x,double lo,double hi) {return std::isfinite(x)?std::clamp(x,lo,hi):0.0;};
    face.blink=bound(face.blink,0,1); face.wide=bound(face.wide,0,1);
    face.mouth=bound(face.mouth,0,1); face.smile=bound(face.smile,0,1); face.brow=bound(face.brow,-1,1);
    return face;
}
inline bool Zero(const Face& f) {return f.blink==0&&f.wide==0&&f.mouth==0&&f.smile==0&&f.brow==0;}
// Only facial pixels are remapped. All sampling stays on the same registered
// head drawing, so changing expression cannot change the silhouette or size.
inline deform::Point SourcePoint(deform::Point uv,Face face,const Landmarks& landmarks={}) {
    face=Bounded(face);
    auto result=uv;
    // A monotone inverse map opens the eyes slightly when startled. Blinks use
    // registered closed-eye texture below: inverse compression would repeat
    // iris edges or smear fur during full closure.
    const auto eye=std::abs(uv.x-landmarks.leftEye.x)<std::abs(uv.x-landmarks.rightEye.x)?landmarks.leftEye:landmarks.rightEye;
    const double horizontal=deform::Smooth((.14-std::abs(uv.x-eye.x))/.045);
    constexpr double inner=.070,outer=.120;
    const double dy=uv.y-eye.y,distance=std::abs(dy);
    if(horizontal>0&&distance<outer){
        const double aperture=1+.10*face.wide*horizontal;
        const double destinationInner=inner*aperture;
        const double sampled=distance<destinationInner ? distance/aperture :
            inner+(distance-destinationInner)*(outer-inner)/(outer-destinationInner);
        result.y=eye.y+std::copysign(sampled,dy);
    }
    // Lift the two corners of the existing mouth, retaining its own fur,
    // lips and muzzle texture instead of pasting another face over the head.
    const double corner=std::abs(uv.x-landmarks.mouth.x)/.085;
    result.y+=.018*face.smile*corner*deform::Bump(uv.x,landmarks.mouth.x,.10)*deform::Bump(uv.y,landmarks.mouth.y,.065);
    return result;
}
inline std::uint32_t Over(std::uint32_t pixel,unsigned red,unsigned green,unsigned blue,double alpha) {
    const unsigned a=static_cast<unsigned>(std::lround(std::clamp(alpha,0.0,1.0)*255));
    auto channel=[&](unsigned color,int shift) {return std::min(255u,(color*a+((pixel>>shift)&255u)*(255-a)+127)/255);};
    return (std::min(255u,a+(((pixel>>24)*(255-a)+127)/255))<<24)|
        (channel(red,16)<<16)|(channel(green,8)<<8)|channel(blue,0);
}
inline bool Apply(const std::uint32_t* source,int width,int height,std::uint32_t* destination,
                  Face face,const Landmarks& landmarks={},const deform::Joints& joints={},const ClosedEyes& closed={}) {
    if (!source||!destination||source==destination||width<1||height<1||width>4096||height>4096) return false;
    face=Bounded(face);
    std::memcpy(destination,source,static_cast<std::size_t>(width)*height*4);
    if(Zero(face))return true;
    Landmarks warped{deform::SittingPoint(landmarks.leftEye,joints),deform::SittingPoint(landmarks.rightEye,joints),deform::SittingPoint(landmarks.mouth,joints)};
    std::array<std::array<std::uint32_t,16>,2> lidColors{};
    std::array<deform::Point,16> ring{};
    const std::array<deform::Point,2> eyes{{warped.leftEye,warped.rightEye}};
    for(int i=0;i<16;++i){const double angle=i*6.283185307179586/16;ring[i]={std::cos(angle),std::sin(angle)};}
    for(int e=0;e<2;++e)for(int i=0;i<16;++i){
        lidColors[e][i]=deform::detail::Sample(source,width,height,width*4,
            {eyes[e].x+ring[i].x*.155,eyes[e].y+ring[i].y*.145});}
    // The body was copied verbatim above; visit only the small facial region.
    const double minX=std::min({warped.leftEye.x,warped.rightEye.x,warped.mouth.x})-.15;
    const double maxX=std::max({warped.leftEye.x,warped.rightEye.x,warped.mouth.x})+.15;
    const double minY=std::min({warped.leftEye.y,warped.rightEye.y,warped.mouth.y})-.13;
    const double maxY=std::max({warped.leftEye.y+.13,warped.rightEye.y+.13,warped.mouth.y+.07});
    const int left=std::clamp(static_cast<int>(std::floor(minX*width)),0,width),right=std::clamp(static_cast<int>(std::ceil(maxX*width)),0,width);
    const int top=std::clamp(static_cast<int>(std::floor(minY*height)),0,height),bottom=std::clamp(static_cast<int>(std::ceil(maxY*height)),0,height);
    for(int y=top;y<bottom;++y) for(int x=left;x<right;++x) {
        const deform::Point uv{(x+.5)/width,(y+.5)/height};
        const auto sourcePoint=SourcePoint(uv,face,warped);
        auto pixel=(sourcePoint.x==uv.x&&sourcePoint.y==uv.y)?source[y*width+x]:
            deform::detail::Sample(source,width,height,width*4,sourcePoint);
        if(face.blink>0 && ((pixel>>24)&255)>240) {
            const int eyeIndex=std::abs(uv.x-warped.leftEye.x)<std::abs(uv.x-warped.rightEye.x)?0:1;
            const auto eye=eyes[eyeIndex];
            const double dx=(uv.x-eye.x)/.125,dy=(uv.y-eye.y)/.115;
            const double ellipse=dx*dx+dy*dy;
            if(ellipse<1.0){
                const double edge=deform::Smooth((1-ellipse)/.16);
                const double aperture=(1-face.blink)*.93;
                const double curve=.15*(dx*dx-1);
                const double cover=(face.blink>.98?1:deform::Smooth((std::abs(dy-curve)-aperture)/.15))*edge;
                // Adjacent fur supplies the actual species color/markings.
                // Covering iris pixels avoids duplicating them during a blink.
                double red=0,green=0,blue=0,total=0;
                for(int i=0;i<16;++i){
                    const double sx=1.24*ring[i].x-dx,sy=1.26*ring[i].y-dy;
                    const double distance=.08+sx*sx+sy*sy,weight=1/(distance*distance);
                    const auto fur=lidColors[eyeIndex][i];const unsigned alpha=fur>>24;
                    if(alpha>240){red+=((fur>>16)&255)*weight;green+=((fur>>8)&255)*weight;blue+=(fur&255)*weight;total+=weight;}}
                if(closed.pixels&&closed.width>0&&closed.height>0){
                    const auto closedEye=eyeIndex==0?closed.landmarks.leftEye:closed.landmarks.rightEye;
                    const double ratio=(closed.landmarks.rightEye.x-closed.landmarks.leftEye.x)/std::max(.05,landmarks.rightEye.x-landmarks.leftEye.x);
                    const auto lid=deform::detail::Sample(closed.pixels,closed.width,closed.height,closed.width*4,
                        {closedEye.x+(uv.x-eye.x)*ratio,closedEye.y+(uv.y-eye.y)*ratio*(static_cast<double>(closed.width)/closed.height)/(static_cast<double>(width)/height)});
                    const unsigned a=lid>>24;
                    if(a>240)pixel=Over(pixel,((lid>>16)&255)*255/a,((lid>>8)&255)*255/a,(lid&255)*255/a,cover);
                    else if(total>0)pixel=Over(pixel,static_cast<unsigned>(red/total),static_cast<unsigned>(green/total),static_cast<unsigned>(blue/total),cover);
                }else{
                    if(total>0)pixel=Over(pixel,static_cast<unsigned>(red/total),static_cast<unsigned>(green/total),static_cast<unsigned>(blue/total),cover);
                    const double crease=deform::Bump(dy,curve,.045)*deform::Smooth((face.blink-.65)/.35)*deform::Smooth((1-std::abs(dx))/.25);
                    pixel=Over(pixel,45,30,26,crease);
                }
            }
        }
        // Open the existing mouth locally for a yawn/sleep mumble. The dark
        // inner mouth and tongue remain bounded inside the original muzzle.
        if(face.mouth>.01 && ((pixel>>24)&255)>240) {
            const double dx=(uv.x-warped.mouth.x)/(.018+.026*face.mouth);
            const double dy=(uv.y-warped.mouth.y)/(.010+.052*face.mouth);
            const double coverage=deform::Smooth((1-std::sqrt(dx*dx+dy*dy))/.16)*deform::Smooth(face.mouth/.16);
            pixel=Over(pixel,68,28,31,coverage);
            const double tongue=deform::Bump(dx,0,.7)*deform::Bump(dy,.55,.38)*coverage;
            pixel=Over(pixel,228,124,137,tongue);
        }
        if(std::abs(face.brow)>.01 && ((pixel>>24)&255)>240) for(auto eye:{warped.leftEye,warped.rightEye}) {
            const double dx=(uv.x-eye.x)/.075;
            const double line=eye.y-.094+face.brow*dx*.017;
            const double coverage=deform::Bump(dx,0,1)*deform::Bump(uv.y,line,.005)*std::abs(face.brow)*.75;
            pixel=Over(pixel,60,42,35,coverage);
        }
        destination[y*width+x]=pixel;
    }
    return true;
}
} // namespace cat::expression
