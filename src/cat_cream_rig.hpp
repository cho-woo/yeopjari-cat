#pragma once
#include "cat_gait.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

namespace cat::cream {
struct Point {double x=0,y=0;};
inline Point operator+(Point a,Point b){return {a.x+b.x,a.y+b.y};}
inline Point operator-(Point a,Point b){return {a.x-b.x,a.y-b.y};}
inline Point operator*(Point a,double t){return {a.x*t,a.y*t};}
inline double Length(Point p){return std::hypot(p.x,p.y);}
inline double Ease(double t){t=std::clamp(t,0.,1.);return t*t*(3-2*t);}
inline Point Rotate(Point p,double angle){return {p.x*std::cos(angle)-p.y*std::sin(angle),p.x*std::sin(angle)+p.y*std::cos(angle)};}
// Separate textures cover the occluded chest/hips. A rigid head retains its
// proportions. Leg articulation follows planted paw goals in the ground frame.
// Textures/output are allocated at startup, never during animation.
class Rig {
public:
    static constexpr int Width=377,Height=337;
    static constexpr double AnchorX=193,AnchorY=299,ReferenceHeight=221;
    bool PrepareParts(const std::uint32_t* pixels,int width,int height) {
        if(!pixels||width%3||height%2||width<96||height<64||width>4096||height>4096)return false;
        output_.resize(Width*Height);
        const int cw=width/3,ch=height/2;
        for(unsigned n=0;n<5;++n){
            int l=cw,t=ch,r=0,b=0;const int ox=(n%3)*cw,oy=(n/3)*ch;
            for(int y=0;y<ch;++y)for(int x=0;x<cw;++x)if((pixels[(oy+y)*width+ox+x]>>24)>32){l=std::min(l,x);t=std::min(t,y);r=std::max(r,x+1);b=std::max(b,y+1);}
            if(l>=r||t>=b)return false;
            auto& texture=parts_[n];texture.w=256;texture.h=256;texture.pixels.resize(256*256);
            // One bounded startup resize per part; no source atlas retained.
            for(int y=0;y<256;++y)for(int x=0;x<256;++x){
                int sx=ox+std::clamp(l+(r-l)*x/255,0,cw-1),sy=oy+std::clamp(t+(b-t)*y/255,0,ch-1);
                auto p=pixels[sy*width+sx];texture.pixels[y*256+x]=(p>>24)>16?p:0;
            }
        }
        ready_=true;return true;
    }
    bool Raster(const cat::gait::Feet& feet,double sit) {
        if(!ready_||!std::isfinite(sit)||sit<0||sit>1)return false;
        for(const auto& f:feet)if(!std::isfinite(f.x)||!std::isfinite(f.y)||std::abs(f.x)>30||f.y>0||f.y< -15)return false;
        std::fill(output_.begin(),output_.end(),0);
        const Point shoulder{205-8*sit,203-3*sit},hip{111+20*sit,210+34*sit};
        Quad(parts_[3],{111+20*sit,209+40*sit},86,100,-.12*sit,{.94,.87});
        Leg(1,feet[1],hip+Point{6,-1},sit,true);
        Leg(3,feet[3],shoulder+Point{-7,2},sit,true);
        Leg(0,feet[0],hip,sit,false);
        Quad(parts_[1],{151+18*sit,203+20*sit},137-32*sit,74+8*sit,-.60*sit,{.5,.5});
        Leg(2,feet[2],shoulder,sit,false);
        const Point headShift{-8*sit,-3*sit};
        // A single rigid head keeps eye/ear proportions identical through the cycle.
        Quad(parts_[0],{223+headShift.x,136+headShift.y},150,166,0,{.5,.5});
        return true;
    }
    const std::uint32_t* Pixels()const{return output_.data();}
    const std::array<Point,4>& PawGoals()const{return goals_;}
private:
    struct Texture {int w=0,h=0;std::vector<std::uint32_t> pixels;};
    std::array<Texture,5> parts_;
    std::vector<std::uint32_t> output_;std::array<Point,4> goals_{};bool ready_=false;
    static std::uint32_t Multiply(std::uint32_t p,double a){std::uint32_t q=0;for(unsigned s=0;s<32;s+=8)q|=static_cast<unsigned>(((p>>s)&255)*a+.5)<<s;return q;}
    void OverAt(int x,int y,std::uint32_t p){
        if(x<0||y<0||x>=Width||y>=Height||!p)return;
        auto& b=output_[y*Width+x];const unsigned inv=255-(p>>24);std::uint32_t q=0;
        for(unsigned s=0;s<32;s+=8)q|=std::min(255u,((p>>s)&255)+(((b>>s)&255)*inv+127)/255)<<s;
        b=q;
    }
    static std::uint32_t Sample(const Texture& texture,double x,double y){
        x-=.5;y-=.5;int ix=int(std::floor(x)),iy=int(std::floor(y));double fx=x-ix,fy=y-iy;
        const std::array<double,4> w{{(1-fx)*(1-fy),fx*(1-fy),(1-fx)*fy,fx*fy}};std::array<std::uint32_t,4> p{};
        for(int i=0;i<4;++i){int xx=ix+i%2,yy=iy+i/2;if(xx>=0&&yy>=0&&xx<texture.w&&yy<texture.h)p[i]=texture.pixels[yy*texture.w+xx];}
        std::uint32_t q=0;for(unsigned s=0;s<32;s+=8){double v=0;for(int i=0;i<4;++i)v+=((p[i]>>s)&255)*w[i];q|=static_cast<unsigned>(v+.5)<<s;}return q;
    }
    void Triangle(const Texture& texture,Point a,Point b,Point c,Point sa,Point sb,Point sc,double tint=1){
        a=a+Point{32,24};b=b+Point{32,24};c=c+Point{32,24};
        const double den=(b.y-c.y)*(a.x-c.x)+(c.x-b.x)*(a.y-c.y);if(den<1e-6)return;
        int l=std::max(0,int(std::floor(std::min({a.x,b.x,c.x})))),r=std::min(Width,int(std::ceil(std::max({a.x,b.x,c.x}))));
        int t=std::max(0,int(std::floor(std::min({a.y,b.y,c.y})))),bot=std::min(Height,int(std::ceil(std::max({a.y,b.y,c.y}))));
        for(int y=t;y<bot;++y)for(int x=l;x<r;++x){Point p{x+.5,y+.5};
            double u=((b.y-c.y)*(p.x-c.x)+(c.x-b.x)*(p.y-c.y))/den,v=((c.y-a.y)*(p.x-c.x)+(a.x-c.x)*(p.y-c.y))/den,z=1-u-v;
            if(u<0||v<0||z<0)continue;
            const Point s=sa*u+sb*v+sc*z;auto q=Sample(texture,s.x,s.y);
            if(tint<1){auto a=q&0xff000000u;q=Multiply(q,tint);q=(q&0xffffffu)|a;}OverAt(x,y,q);
        }
    }
    void Quad(const Texture& texture,Point position,double w,double h,double angle,Point pivot){
        auto map=[&](double u,double v){return position+Rotate({(u-pivot.x)*w,(v-pivot.y)*h},angle);};
        const Point a=map(0,0),b=map(1,0),c=map(1,1),d=map(0,1);
        Triangle(texture,a,b,c,{0,0},{double(texture.w),0},{double(texture.w),double(texture.h)});
        Triangle(texture,a,c,d,{0,0},{double(texture.w),double(texture.h)},{0,double(texture.h)});
    }
    static Point Bone(Point p,Point a,Point b,Point ta,Point tb){
        const Point v=b-a,t=tb-ta;const double len=Length(v),angle=std::atan2(t.y,t.x)-std::atan2(v.y,v.x);
        // Longitudinal stretch only; fur thickness does not pulse with a step.
        const Point unit=v*(1/len),d=p-a;const double along=(d.x*unit.x+d.y*unit.y)*Length(t)/len;
        const double across=-d.x*unit.y+d.y*unit.x;
        return ta+Rotate(unit*along+Point{-unit.y,unit.x}*across,angle);
    }
    void Leg(unsigned index,const cat::gait::Foot& foot,Point root,double sit,bool farSide){
        const bool hind=index<2;const auto& texture=parts_[hind?4:2];
        const Point sourceRoot=hind?Point{38,13}:Point{18,9};
        const Point sourceKnee=hind?Point{19,53}:Point{18,46};
        const Point sourceAnkle=hind?Point{23,74}:Point{23,70};
        const Point sourceSole=hind?Point{28,87}:Point{25,80};
        const double w=hind?59:40,h=hind?88:81;
        const double px=ReferenceHeight/110;
        Point sole{(hind?(farSide?125:113):(farSide?206:218))+foot.x*px+(hind?26*sit:0),275+foot.y*px-(hind?9*std::sin(3.141592653589793*sit):0)};
        goals_[index]=sole;
        const Point ankle=sole-(sourceSole-sourceAnkle);
        Point direction=ankle-root;const double distance=Length(direction),upper=hind?43-18*sit:35,lower=hind?37-12*sit:30;
        const double reach=std::clamp(distance,std::abs(upper-lower)+.01,upper+lower-.01);
        const Point unit=direction*(1/std::max(.01,distance));
        const double projection=(upper*upper-lower*lower+reach*reach)/(2*reach);
        const double bend=std::sqrt(std::max(0.,upper*upper-projection*projection));
        const Point knee=root+unit*projection+Point{-unit.y,unit.x}*((hind?-1:1)*bend);
        const auto map=[&](Point p){
            if(hind) {
                // Compress the hock above a rigid planted paw. A continuous,
                // monotone map cannot fold the texture as the pelvis lowers.
                const double t=Ease((p.y-sourceRoot.y)/(sourceAnkle.y-sourceRoot.y));
                const double x=p.x+(root.x-sourceRoot.x)*(1-t)+(sole.x-sourceSole.x)*t;
                const double y=p.y>=sourceAnkle.y?sole.y+p.y-sourceSole.y:
                    root.y+(p.y-sourceRoot.y)*(ankle.y-root.y)/(sourceAnkle.y-sourceRoot.y);
                return Point{x,y};
            }
            const Point top=Bone(p,sourceRoot,sourceKnee,root,knee);
            const Point bottom=Bone(p,sourceKnee,sourceAnkle,knee,ankle);
            const double mix=Ease((p.y-sourceKnee.y+6)/12);
            Point result=top*(1-mix)+bottom*mix;
            const double paw=Ease((p.y-sourceAnkle.y+2)/7);
            return result*(1-paw)+(sole+p-sourceSole)*paw;
        };
        constexpr int nx=9,ny=16;
        std::array<Point,(nx+1)*(ny+1)> grid;
        for(int y=0;y<=ny;++y)for(int x=0;x<=nx;++x)grid[y*(nx+1)+x]=map({w*x/nx,h*y/ny});
        for(int y=0;y<ny;++y)for(int x=0;x<nx;++x){
            const int a=y*(nx+1)+x,b=a+1,c=b+nx+1,d=a+nx+1;
            const Point sa{double(texture.w)*x/nx,double(texture.h)*y/ny};
            const Point sb{double(texture.w)*(x+1)/nx,sa.y};
            const Point sc{sb.x,double(texture.h)*(y+1)/ny},sd{sa.x,sc.y};
            Triangle(texture,grid[a],grid[b],grid[c],sa,sb,sc,farSide?.89:1);
            Triangle(texture,grid[a],grid[c],grid[d],sa,sc,sd,farSide?.89:1);
        }
    }
};
} // namespace cat::cream
