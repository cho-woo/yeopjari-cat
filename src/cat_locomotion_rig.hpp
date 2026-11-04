#pragma once
#include "cat_preferences.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

namespace cat::walk {
struct Profile {
    double rearRoot=.74,middleRoot=.82,frontRoot=.72;
    double belly=.82,frontDrop=.12,rearDrop=.10,frontPartition=.47,frontSlope=.08;
};
inline Profile ProfileFor(Character c) {
    if(c==Character::Cream||c==Character::SilverTabby)return {.72,.81,.72,.82,.12,.10,.53,.085};
    if(c==Character::Rabbit)return {.77,.82,.76,.86,.10};
    if(c==Character::Munchkin)return {.74,.82,.72,.84};
    return {};
}
inline double Smooth(double t) {t=std::clamp(t,0.0,1.0);return t*t*(3-2*t);}
struct Foot { double x=0,y=0; bool stance=true; };
// Body-space distance is independent of nutrition width. Stance reverses the
// real root displacement exactly; the return arc has continuous end velocity.
inline Foot FootAt(double phase,double stride=28,double duty=.62,double lift=5.5) {
    if(!std::isfinite(phase)||!std::isfinite(stride)||!std::isfinite(duty)||!std::isfinite(lift))return {};
    phase-=std::floor(phase);duty=std::clamp(duty,.5,.85);
    const double half=stride*duty/2;
    if(phase<duty)return {half-phase*stride,0,true};
    const double t=(phase-duty)/(1-duty);
    const double x=-half+2*half*Smooth(t)-stride*(1-duty)*(2*t*t*t-3*t*t+t);
    const double sine=std::sin(3.14159265358979323846*t);
    return {x,-lift*sine*sine,false};
}

class Rig {
public:
    bool Prepare(const std::uint32_t* source,int width,int height,int strideBytes,Profile profile) {
        ready_=false;
        if(!source||width<8||height<8||width>2048||height>2048||strideBytes<width*4||strideBytes>65536||
            !std::isfinite(profile.rearRoot)||!std::isfinite(profile.middleRoot)||!std::isfinite(profile.frontRoot))return false;
        for(double root:{profile.rearRoot,profile.middleRoot,profile.frontRoot})if(root<.65||root>.87)return false;
        for(double value:{profile.belly,profile.frontDrop,profile.rearDrop,profile.frontPartition,profile.frontSlope})
            if(!std::isfinite(value))return false;
        if(profile.belly<.78||profile.belly>.90||profile.frontDrop<0||profile.frontDrop>.14||
            profile.rearDrop<0||profile.rearDrop>.14||profile.frontPartition<.40||profile.frontPartition>.60||
            profile.frontSlope<0||profile.frontSlope>.15||profile.frontPartition+profile.frontSlope>.70||
            profile.belly-std::max(profile.frontDrop,profile.rearDrop)<.695)return false;
        width_=width;height_=height;profile_=profile;pad_=static_cast<int>(std::ceil(width*.20))+2;
        const auto size=static_cast<std::size_t>(width)*height;
        body_.resize(size);bodyCover_.resize(size);output_.resize(static_cast<std::size_t>(Width())*height);
        const std::array<double,3> roots{{profile.rearRoot,profile.middleRoot,profile.frontRoot}};
        const std::array<double,3> phases{{.5,.25,0}};
        for(int i=0;i<3;++i){auto& l=legs_[i];l.pixels.assign(size,0);l.root=roots[i];l.phase=phases[i];
            l.left=width;l.top=height;l.right=l.bottom=0;l.sole=0;}
        for(int y=0;y<height;++y)for(int x=0;x<width;++x){
            std::uint32_t pixel;std::memcpy(&pixel,reinterpret_cast<const unsigned char*>(source)+y*strideBytes+x*4,4);
            const auto masks=Masks((x+.5)/width,(y+.5)/height,profile);
            for(int i=0;i<3;++i){
                // Mask fractions are desired contributions, not independent
                // opacities. Compensate for the layers drawn afterwards so an
                // opaque source does not acquire a translucent split seam.
                double later=0;for(int k=i+1;k<3;++k)later+=masks[k];
                const double remain=1-(pixel>>24)/255.*later;
                auto& l=legs_[i];const auto part=Multiply(pixel,remain>1e-12?masks[i]/remain:0);l.pixels[y*width+x]=part;
                const auto a=part>>24;
                if(a){l.left=std::min(l.left,x);l.top=std::min(l.top,y);l.right=std::max(l.right,x+1);l.bottom=std::max(l.bottom,y+1);}
                if(a>153)l.sole=std::max(l.sole,y+1);
            }
            const double curve=TorsoEdge((x+.5)/width,profile);
            const auto cover=static_cast<unsigned char>(std::lround(255*(1-Smooth(((y+.5)/height-(curve-.015))/.03))));
            bodyCover_[y*width+x]=cover;
            body_[y*width+x]=Multiply(pixel,cover/255.);
        }
        ready_=true;
        for(const auto& l:legs_)if(l.sole==0||l.left>=l.right)ready_=false;
        return ready_;
    }

    // All buffers are prepared once. width/heightBodyPx include nutrition;
    // exclude growth and DPI, which are already divided out of gait distance.
    bool Raster(double phase,double strideBodyPx,double widthBodyPx,double heightBodyPx,
                double duty=.62,double swingLiftBodyPx=5.5) {
        if(!ready_||!std::isfinite(phase)||!std::isfinite(strideBodyPx)||!std::isfinite(widthBodyPx)||
            !std::isfinite(heightBodyPx)||!std::isfinite(duty)||!std::isfinite(swingLiftBodyPx)||
            strideBodyPx<=0||strideBodyPx>60||widthBodyPx<10||heightBodyPx<10||duty<.5||duty>.85||
            swingLiftBodyPx<0||swingLiftBodyPx>12)return false;
        phase-=std::floor(phase);
        struct Frame {double dx,dy,reach;};std::array<Frame,3> frames{};
        for(int i=0;i<3;++i){const auto& l=legs_[i];
            const auto foot=FootAt(phase+l.phase,strideBodyPx,duty,swingLiftBodyPx);
            const double dx=foot.x/widthBodyPx;
            const double dy=(height_-2-l.sole)/static_cast<double>(height_)+foot.y/heightBodyPx;
            const double reach=.96-l.root;
            // A monotone vertical map cannot fold or open cracks in a leg.
            if(1+std::min(0.0,dy)*1.5/reach<=.1)return false;
            if(std::abs(dx*width_)+2>pad_)return false;
            frames[i]={dx,dy,reach};
        }
        // Validate every leg before changing the persistent output bitmap.
        std::fill(output_.begin(),output_.end(),0);
        for(int i=0;i<3;++i){const auto& l=legs_[i];
            const auto dx=frames[i].dx,dy=frames[i].dy,reach=frames[i].reach;
            const int left=std::max(0,pad_+static_cast<int>(std::floor(l.left+std::min(0.0,dx*width_)))-2);
            const int right=std::min(Width(),pad_+static_cast<int>(std::ceil(l.right+std::max(0.0,dx*width_)))+2);
            const int top=std::max(0,static_cast<int>(std::floor(l.top+std::min(0.0,dy*height_)))-2);
            const int bottom=std::min(height_,static_cast<int>(std::ceil(l.bottom+std::max(0.0,dy*height_)))+2);
            for(int y=top;y<bottom;++y){
                const double destY=(y+.5)/height_;double sourceY=destY;
                for(int k=0;k<6;++k){
                    const double t=std::clamp((sourceY-l.root)/reach,0.0,1.0);
                    const double derivative=t>0&&t<1?6*t*(1-t)/reach:0;
                    sourceY-=(sourceY+dy*t*t*(3-2*t)-destY)/(1+dy*derivative);
                }
                const double shift=dx*Smooth((sourceY-l.root)/reach);
                for(int x=left;x<right;++x){
                    const auto pixel=Sample(l.pixels,(x-pad_+.5)/width_-shift,sourceY);
                    if(pixel>>24)output_[y*Width()+x]=Over(pixel,output_[y*Width()+x]);
                }
            }
        }
        // Partition the original torso and warped limb union by coverage.
        // Source-over here would duplicate alpha in semitransparent fur.
        for(int y=0;y<height_;++y)for(int x=0;x<width_;++x){
            const auto coverage=bodyCover_[y*width_+x];if(coverage){const auto index=y*Width()+x+pad_;
                const auto torso=body_[y*width_+x],limbs=Multiply(output_[index],(255-coverage)/255.);
                std::uint32_t pixel=0;for(int shift=0;shift<32;shift+=8)
                    pixel|=std::min(255u,((torso>>shift)&255)+((limbs>>shift)&255))<<shift;
                output_[index]=pixel;}
        }
        return true;
    }
    std::uint32_t* Pixels(){return output_.data();}
    const std::uint32_t* Pixels()const{return output_.data();}
    int Width()const{return width_+2*pad_;}
    int SourceWidth()const{return width_;}
    int PaddingX()const{return pad_;}
    int Height()const{return height_;}
    double GroundInsetPixels()const{return 2;}
    bool Ready()const{return ready_;}
    int Sole(int leg)const{return leg>=0&&leg<3?legs_[leg].sole:0;}
    Profile GetProfile()const{return profile_;}

    static double TorsoEdge(double x,Profile p) {
        return p.belly-p.frontDrop*Smooth((x-.38)/.28)-p.rearDrop*Smooth((.35-x)/.15);
    }
    // Curved shoulder boundaries keep an original foreleg strip from being
    // assigned to the far-leg layer. Upper roots overlap beneath the torso;
    // the original head and silhouette above y=.68 remain untouched.
    static std::array<double,3> Masks(double x,double y,Profile p) {
        const double frontBoundary=p.frontPartition+p.frontSlope*Smooth((y-.70)/.24);
        const double rearBoundary=.34-.055*Smooth((y-.73)/.22);
        const double rear=Smooth((rearBoundary-x)/.04),front=Smooth((x-frontBoundary)/.04);
        const double upper=Smooth((y-(TorsoEdge(x,p)-.09))/.04);
        std::array<double,3> m{{rear*upper,(1-rear)*(1-front)*upper,front*upper}};
        const double total=std::max(1.0,m[0]+m[1]+m[2]);for(auto& v:m)v/=total;return m;
    }
private:
    struct Layer {std::vector<std::uint32_t> pixels;double root=0,phase=0;int left=0,top=0,right=0,bottom=0,sole=0;};
    std::array<Layer,3> legs_;
    std::vector<std::uint32_t> body_,output_;
    std::vector<unsigned char> bodyCover_;
    Profile profile_;
    int width_=0,height_=0,pad_=0;bool ready_=false;
    static std::uint32_t Multiply(std::uint32_t p,double amount){
        amount=std::clamp(amount,0.0,1.0);std::uint32_t out=0;
        for(int shift=0;shift<32;shift+=8)out|=static_cast<std::uint32_t>(std::lround(((p>>shift)&255)*amount))<<shift;
        return out;
    }
    static std::uint32_t Over(std::uint32_t front,std::uint32_t back){
        const unsigned inverse=255-(front>>24);std::uint32_t out=0;
        for(int shift=0;shift<32;shift+=8)out|=std::min(255u,((front>>shift)&255)+(((back>>shift)&255)*inverse+127)/255)<<shift;
        return out;
    }
    std::uint32_t Sample(const std::vector<std::uint32_t>& src,double u,double v)const{
        const double x=u*width_-.5,y=v*height_-.5;const int x0=static_cast<int>(std::floor(x)),y0=static_cast<int>(std::floor(y));
        const double fx=x-x0,fy=y-y0;
        const auto read=[&](int a,int b){return a<0||b<0||a>=width_||b>=height_?0u:src[b*width_+a];};
        const std::array<std::uint32_t,4> p{{read(x0,y0),read(x0+1,y0),read(x0,y0+1),read(x0+1,y0+1)}};
        const std::array<double,4> weights{{(1-fx)*(1-fy),fx*(1-fy),(1-fx)*fy,fx*fy}};
        std::uint32_t out=0;
        for(int shift=0;shift<32;shift+=8){double value=0;for(int k=0;k<4;++k)value+=((p[k]>>shift)&255)*weights[k];
            out|=static_cast<std::uint32_t>(std::clamp(std::lround(value),0L,255L))<<shift;}
        return out;
    }
};
} // namespace cat::walk
