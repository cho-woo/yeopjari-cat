#pragma once
#include "cat_deformation.hpp"
#include <vector>

// Continuous, source-textured forelimbs on the retained reference drawing.
// Coordinates are normalized against that drawing, never an alpha recrop.
namespace cat::cursor_rig {
using deform::Point;
struct Controls { double rise=0,leftLift=0,rightLift=0,aimX=0,groom=0,lowReach=0; };
inline Controls Bounded(Controls c) {
    auto finite=[](double x,double lo,double hi){return std::isfinite(x)?std::clamp(x,lo,hi):0.0;};
    c.rise=finite(c.rise,0,1);c.leftLift=finite(c.leftLift,0,1);c.rightLift=finite(c.rightLift,0,1);c.aimX=finite(c.aimX,-1,1);c.groom=finite(c.groom,0,1);c.lowReach=finite(c.lowReach,0,1);return c;
}
inline bool Active(const Controls& c) {return c.rise!=0||c.leftLift!=0||c.rightLift!=0;}
inline int OutputHeight(int height) {return height+static_cast<int>(std::ceil(height*.12));}
inline Point BodyPoint(Point p,Controls controls) {
    const auto c=Bounded(controls);
    // Fixed hind soles, a moving pelvis/chest, and a rigid unscaled head.
    p.y-=.10*c.rise*deform::Smooth((.975-p.y)/.28);return p;
}
inline Point Shoulder(Controls controls,int side) {return BodyPoint({side==0?.425:.685,.735},controls);}
inline Point PawUV(Controls controls,int side) {
    const auto c=Bounded(controls);const double amount=side==0?c.leftLift:c.rightLift;
    const Point start{side==0?.435:.685,.958};
    const Point catching{(side==0?.30:.79)+.065*c.aimX,.535-.10*c.rise};
    const Point wiping{.58+.060*c.aimX,.610-.035*std::abs(c.aimX)};
    Point target{catching.x+(wiping.x-catching.x)*c.groom,catching.y+(wiping.y-catching.y)*c.groom};
    const Point low{(side==0?.265:.845)+.035*c.aimX,.925};
    target={target.x+(low.x-target.x)*c.lowReach,target.y+(low.y-target.y)*c.lowReach};
    Point paw{start.x+(target.x-start.x)*amount,start.y+(target.y-start.y)*amount};
    const auto shoulder=Shoulder(c,side);
    // Two links retain their lengths; unreachable pointers remain misses.
    const double dx=(paw.x-shoulder.x)*.90,dy=paw.y-shoulder.y,r=std::hypot(dx,dy);
    if(r>.235){paw.x=shoulder.x+dx*.235/r/.90;paw.y=shoulder.y+dy*.235/r;}
    return paw;
}
struct Source {
    int width=0,height=0;
    std::vector<std::uint32_t> original,torso,neutralTorso,bodyBase,leftArm,rightArm,leftPalm,rightPalm,skin,layer;
};
inline std::uint32_t Mask(std::uint32_t p,double coverage) {
    unsigned result=0;const auto a=static_cast<unsigned>(std::lround(std::clamp(coverage,0.0,1.0)*255));
    for(int c=0;c<4;++c)result|=((((p>>(c*8))&255)*a+127)/255)<<(c*8);
    return result;
}
inline std::uint32_t Residual(std::uint32_t pixel,double coverage) {
    const auto amount=static_cast<unsigned>(std::lround(std::clamp(coverage,0.0,1.0)*255));
    const auto alpha=pixel>>24,removed=(alpha*amount+127)/255;
    if(removed==255)return 0;
    const auto denominator=255-removed;std::uint32_t result=0;
    for(int channel=0;channel<4;++channel){
        const auto value=(pixel>>(channel*8))&255,part=(value*amount+127)/255;
        result|=std::min(255u,((value-part)*255+denominator/2)/denominator)<<(channel*8);
    }
    return result;
}
inline std::uint32_t MaterialMix(std::uint32_t a,std::uint32_t b,double amount) {
    std::uint32_t pixel=0;
    for(int channel=0;channel<4;++channel) {
        const auto from=(a>>(channel*8))&255,to=(b>>(channel*8))&255;
        pixel|=static_cast<std::uint32_t>(std::lround(from+(static_cast<double>(to)-from)*amount))<<(channel*8);
    }
    return pixel;
}
inline std::uint32_t Over(std::uint32_t a,std::uint32_t b) {
    const auto inverse=255-(a>>24);unsigned result=0;
    for(int c=0;c<4;++c)result|=std::min(255u,((a>>(c*8))&255)+((((b>>(c*8))&255)*inverse+127)/255))<<(c*8);
    return result;
}
inline double ArmCentreX(double v,int side) {return (side==0?.425:.685)+(side==0?.010:0)*deform::Smooth((v-.735)/.223);}
inline double ArmCoverage(Point p,int side) {
    const double centre=ArmCentreX(p.y,side);
    const double shaft=deform::Smooth((.067-std::abs(p.x-centre))/.012)*
        deform::Smooth((p.y-.716)/.030)*(1-deform::Smooth((p.y-.925)/.025));
    const double paw=1-deform::Smooth((std::hypot((p.x-centre)/.078,(p.y-.950)/.046)-.84)/.16);
    return std::max(shaft,paw);
}
inline bool Prepare(Source& parts,const std::uint32_t* pixels,int width,int height) {
    if(!pixels||width<8||height<8||width>1024||height>1024)return false;
    parts.width=width;parts.height=height;const auto count=static_cast<std::size_t>(width)*height;
    parts.original.assign(pixels,pixels+count);parts.torso.resize(count);parts.neutralTorso.resize(count);parts.leftArm.resize(count);parts.rightArm.resize(count);parts.skin.resize(count);
    parts.layer.resize(static_cast<std::size_t>(width)*OutputHeight(height));
    for(int y=0;y<height;++y)for(int x=0;x<width;++x) {
        const Point uv{(x+.5)/width,(y+.5)/height};const auto i=static_cast<std::size_t>(y)*width+x;
        const double a=ArmCoverage(uv,0),b=ArmCoverage(uv,1);
        const double coverage=deform::Smooth((uv.x-.355)/.018)*deform::Smooth((.775-uv.x)/.018)*
            deform::Smooth((uv.y-.716)/.030);
        parts.leftArm[i]=Mask(pixels[i],a);parts.rightArm[i]=Mask(pixels[i],b);
        parts.neutralTorso[i]=Residual(pixels[i],std::max(a,b));
        // The source has hidden chest behind the resting arms. Register a
        // same-coat fur patch from its exposed sternum, not another cat's art.
        const double furY=.735+std::clamp(uv.y-.70,0.0,.28)*.20;
        auto repair=deform::detail::Sample(pixels,width,height,width*4,{.535+(uv.x-.535)*.10,furY});
        // A rounded belly ends above the two OUTER planted hind soles;
        // forepaw pixels must disappear from the floor when those paws lift.
        const double bellyRadius=std::hypot((uv.x-.565)/.245,(uv.y-.750)/.210);
        repair=Mask(repair,1-deform::Smooth((bellyRadius-.92)/.08));
        parts.torso[i]=Over(Mask(repair,coverage),Mask(pixels[i],1-coverage));
    }
    return true;
}
inline bool SetArms(Source& parts,const std::uint32_t* left,int leftWidth,int leftHeight,
                    const std::uint32_t* right,int rightWidth,int rightHeight) {
    if(!left||!right||leftWidth<1||leftHeight<1||rightWidth<1||rightHeight<1||parts.width<1)return false;
    parts.leftPalm.resize(parts.leftArm.size());parts.rightPalm.resize(parts.rightArm.size());
    for(int y=0;y<parts.height;++y)for(int x=0;x<parts.width;++x){
        const Point uv{(x+.5)/parts.width,(y+.5)/parts.height};const auto i=static_cast<std::size_t>(y)*parts.width+x;
        parts.leftPalm[i]=deform::detail::Sample(left,leftWidth,leftHeight,leftWidth*4,uv);
        parts.rightPalm[i]=deform::detail::Sample(right,rightWidth,rightHeight,rightWidth*4,uv);
    }
    return true;
}
inline bool SetBody(Source& parts,const std::uint32_t* body,int width,int height) {
    if(!body||width<1||height<1||parts.width<1)return false;
    parts.bodyBase.resize(parts.torso.size());
    for(int y=0;y<parts.height;++y)for(int x=0;x<parts.width;++x){
        const auto i=static_cast<std::size_t>(y)*parts.width+x;const Point uv{(x+.5)/parts.width,(y+.5)/parts.height};
        const auto painted=deform::detail::Sample(body,width,height,width*4,uv);
        // Retain the exact original tail and head. The newly visible chest,
        // hips and rear legs have continuous painted fur, not sampled stripes.
        // Keep the actual original outer hips, tail and two supporting hind
        // feet. New fur is revealed ONLY where a foreleg left the chest.
        const double opening=deform::Smooth((uv.x-.350)/.018)*deform::Smooth((.785-uv.x)/.018)*
            deform::Smooth((uv.y-.716)/.030);
        const double support=deform::Smooth((uv.y-.925)/.025);
        const double hindFeet=(1-deform::Smooth((uv.x-.355)/.025))+deform::Smooth((uv.x-.765)/.025);
        const double retain=std::clamp(1-opening+support*hindFeet,0.0,1.0);
        const double cover=opening*(1-deform::Smooth((uv.y-.930)/.025));
        parts.bodyBase[i]=Over(Mask(parts.original[i],retain),Mask(painted,cover));
        parts.torso[i]=parts.bodyBase[i];
    }
    return true;
}
inline void UpdateHead(Source& parts,const std::uint32_t* head) {
    if(!head||parts.bodyBase.size()!=parts.torso.size())return;
    for(int y=0;y<parts.height;++y)for(int x=0;x<parts.width;++x){
        const auto i=static_cast<std::size_t>(y)*parts.width+x;
        const Point uv{(x+.5)/parts.width,(y+.5)/parts.height};
        const double neckline=.64+.065*deform::Bump(uv.x,.57,.45);
        const double coverage=1-deform::Smooth((uv.y-(neckline-.01))/.025);
        parts.torso[i]=MaterialMix(parts.bodyBase[i],head[i],coverage);
    }
}
inline Point Elbow(Controls c,int side) {
    const auto shoulder=Shoulder(c,side),paw=PawUV(c,side);
    const double dx=(paw.x-shoulder.x)*.90,dy=paw.y-shoulder.y;
    const double d=std::clamp(std::hypot(dx,dy),.025,.234999);
    constexpr double upper=.12,lower=.115;
    const double along=(upper*upper-lower*lower+d*d)/(2*d);
    const double across=std::sqrt(std::max(0.0,upper*upper-along*along));
    const double sign=side==0?-1:1;
    return {shoulder.x+(dx*along/d-dy*across/d*sign)/.90,shoulder.y+dy*along/d+dx*across/d*sign};
}
inline Point LimbCentre(Controls c,int side,double s) {
    const auto a=Shoulder(c,side),b=Elbow(c,side),d=PawUV(c,side);
    // Connected curved skin across the elbow; no separate rotated leg pieces.
    const auto mix=[](Point x,Point y,double t){return Point{x.x+(y.x-x.x)*t,x.y+(y.y-x.y)*t};};
    if(s<.40)return mix(a,b,s/.50);
    if(s>.60)return mix(b,d,(s-.50)/.50);
    const auto left=mix(a,b,.8),right=mix(b,d,.2);const double t=(s-.40)/.20;
    return {(1-t)*(1-t)*left.x+2*t*(1-t)*b.x+t*t*right.x,
            (1-t)*(1-t)*left.y+2*t*(1-t)*b.y+t*t*right.y};
}
// Unlike the generic same-sized warp, reach has explicit top headroom.
inline void Triangle(deform::detail::Vertex a,deform::detail::Vertex b,deform::detail::Vertex c,
                     const std::uint32_t* source,int width,int height,std::uint32_t* destination,int outputHeight) {
    double area=deform::detail::Cross(a.position,b.position,c.position);
    if(std::abs(area)<1e-12)return;
    if(area<0){std::swap(b,c);area=-area;}
    const int left=std::max(0,static_cast<int>(std::floor(std::min({a.position.x,b.position.x,c.position.x}))));
    const int right=std::min(width-1,static_cast<int>(std::ceil(std::max({a.position.x,b.position.x,c.position.x}))));
    const int top=std::max(0,static_cast<int>(std::floor(std::min({a.position.y,b.position.y,c.position.y}))));
    const int bottom=std::min(outputHeight-1,static_cast<int>(std::ceil(std::max({a.position.y,b.position.y,c.position.y}))));
    for(int y=top;y<=bottom;++y)for(int x=left;x<=right;++x){
        const Point p{x+.5,y+.5};
        const double u=deform::detail::Cross(b.position,c.position,p)/area,
                     v=deform::detail::Cross(c.position,a.position,p)/area,w=1-u-v;
        if(u< -1e-10||v< -1e-10||w< -1e-10)continue;
        destination[static_cast<std::size_t>(y)*width+x]=deform::detail::Sample(source,width,height,width*4,
            {a.uv.x*u+b.uv.x*v+c.uv.x*w,a.uv.y*u+b.uv.y*v+c.uv.y*w});
    }
}
inline bool Render(Source& parts,std::uint32_t* destination,int outputWidth,int outputHeight,Controls controls,
                   const std::uint32_t* headSource=nullptr) {
    const int width=parts.width,height=parts.height;
    if(!destination||outputWidth!=width||outputHeight!=OutputHeight(height)||parts.torso.empty())return false;
    const auto c=Bounded(controls);const double pad=outputHeight-height;
    const auto count=static_cast<std::size_t>(outputWidth)*outputHeight;
    std::fill(destination,destination+count,0u);
    if(!Active(c)) {
        for(int y=0;y<height;++y)std::copy(parts.original.data()+static_cast<std::size_t>(y)*width,
            parts.original.data()+static_cast<std::size_t>(y+1)*width,destination+static_cast<std::size_t>(y+outputHeight-height)*width);
        return true;
    }
    // A local visibility/material mask follows the leaving forearms. At rest
    // the original forelimbs exactly cover their residual source torso. This
    // reveals repaired underarm fur only after a paw lifts, and restores the
    // original body before the action ends; no two cat silhouettes overlap.
    if(parts.bodyBase.size()==parts.torso.size()) {
        const double exposure=deform::Smooth(std::max(c.leftLift,c.rightLift)/.15);
        for(int y=static_cast<int>(std::ceil(height*.716));y<height;++y)for(int x=0;x<width;++x) {
            const auto i=static_cast<std::size_t>(y)*width+x;
            parts.torso[i]=MaterialMix(parts.neutralTorso[i],parts.bodyBase[i],exposure);
        }
    }
    auto vertex=[&](Point uv,Point p){return deform::detail::Vertex{{p.x*width,p.y*height+pad},uv};};
    constexpr int cells=32;
    for(int y=0;y<cells;++y)for(int x=0;x<cells;++x){
        const Point a{x/double(cells),y/double(cells)},b{(x+1)/double(cells),y/double(cells)},
            d{(x+1)/double(cells),(y+1)/double(cells)},e{x/double(cells),(y+1)/double(cells)};
        const auto A=vertex(a,BodyPoint(a,c)),B=vertex(b,BodyPoint(b,c)),D=vertex(d,BodyPoint(d,c)),E=vertex(e,BodyPoint(e,c));
        Triangle(A,B,D,parts.torso.data(),width,height,destination,outputHeight);
        Triangle(A,D,E,parts.torso.data(),width,height,destination,outputHeight);
    }
    for(int side=0;side<2;++side){
        std::fill(parts.layer.begin(),parts.layer.end(),0u);
        const auto& original=side==0?parts.leftArm:parts.rightArm;
        const auto& painted=side==0?parts.leftPalm:parts.rightPalm;
        const double lift=side==0?c.leftLift:c.rightLift;
        const double palmTurn=deform::Smooth((lift-.35)/.35);
        const std::uint32_t* texture=original.data();
        if(!painted.empty()&&palmTurn>0) {
            for(int y=0;y<height;++y)for(int x=0;x<width;++x){
                const auto i=static_cast<std::size_t>(y)*width+x;
                const double mix=palmTurn*deform::Smooth(((y+.5)/height-.895)/.04);
                std::uint32_t pixel=0;
                for(int channel=0;channel<4;++channel) {
                    const auto a=(original[i]>>(channel*8))&255,b=(painted[i]>>(channel*8))&255;
                    pixel|=static_cast<std::uint32_t>(std::lround(a+(static_cast<double>(b)-a)*mix))<<(channel*8);
                }
                parts.skin[i]=pixel;
            }
            texture=parts.skin.data();
        }
        auto point=[&](double s,double transverse){
            const double v=.716+s*.281,u=ArmCentreX(v,side)+transverse*.088;
            const Point uv{u,v};
            // Source skin includes shoulder/hand overlap beyond the bones.
            const double bone=std::clamp((v-.735)/.223,0.0,1.0);
            const auto centre=LimbCentre(c,side,bone);
            const auto before=LimbCentre(c,side,std::max(0.0,bone-.01)),after=LimbCentre(c,side,std::min(1.0,bone+.01));
            const double dx=(after.x-before.x)*.90,dy=after.y-before.y,len=std::max(.001,std::hypot(dx,dy));
            const double extension=v<.735?v-.735:v>.958?v-.958:0;
            Point moved{centre.x+dy/len*transverse*.088+dx/len*extension/.90,
                        centre.y-dx/len*transverse*.088*.90+dy/len*extension};
            // At rest, exactly register the original limb. The IK skin assumes
            // full responsibility only as the forepaw leaves the floor.
            const double active=deform::Smooth(lift/.20);
            const auto rest=BodyPoint(uv,c);
            moved={rest.x+(moved.x-rest.x)*active,rest.y+(moved.y-rest.y)*active};
            return vertex(uv,moved);
        };
        for(int y=0;y<32;++y)for(int x=0;x<10;++x){
            const double a=y/32.,b=(y+1)/32.,u=-1+x*.2,v=u+.2;
            const auto A=point(a,u),B=point(a,v),D=point(b,v),E=point(b,u);
            Triangle(A,B,D,texture,width,height,parts.layer.data(),outputHeight);
            Triangle(A,D,E,texture,width,height,parts.layer.data(),outputHeight);
        }
        for(std::size_t i=0;i<count;++i)destination[i]=Over(parts.layer[i],destination[i]);
    }
    // Expressive head samples can be supplied without ever repainting the
    // body or recomputing its cached source segmentation.
    (void)headSource;
    return true;
}
// Convenience offline proof entry point; the product uses cached Source.
// Render is defined below. Destination has width columns and OutputHeight(height) rows.
inline bool Render(const std::uint32_t* source,int width,int height,std::uint32_t* destination,
                   int outputWidth,int outputHeight,Controls controls) {
    Source parts;if(!Prepare(parts,source,width,height))return false;
    return Render(parts,destination,outputWidth,outputHeight,controls);
}
} // namespace cat::cursor_rig
