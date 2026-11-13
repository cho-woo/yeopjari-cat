#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

namespace cat::raster_transition {
// Registered, premultiplied images only. A small boundary correction follows
// local image motion before mixing colour; a plain dissolve duplicates eyes
// and paws. This has no authority over source clocks or physical contacts.
class RegisteredMorph {
    struct Point { float x=0,y=0; };
    static constexpr int Grid=17,Limit=96;
    using Field=std::array<Point,Grid*Grid>;
    struct Image { int w=0,h=0; std::vector<std::array<float,4>> p; };
    Field forward_{},backward_{};
    int width_=0,height_=0,left_=0,top_=0,right_=0,bottom_=0;
    bool ready_=false;
    static std::array<float,4> Pixel(const unsigned char* p,int w,int h,float x,float y) {
        if(x<0||y<0||x>w-1||y>h-1)return {};
        const int ix=static_cast<int>(x),iy=static_cast<int>(y);
        const int jx=std::min(ix+1,w-1),jy=std::min(iy+1,h-1);
        const float fx=x-ix,fy=y-iy;
        std::array<float,4> out{};
        for(int c=0;c<4;++c)out[c]=
            (p[(iy*w+ix)*4+c]*(1-fx)+p[(iy*w+jx)*4+c]*fx)*(1-fy)+
            (p[(jy*w+ix)*4+c]*(1-fx)+p[(jy*w+jx)*4+c]*fx)*fy;
        return out;
    }
    Image Reduce(const unsigned char* p) const {
        Image out;
        const float scale=std::min(1.f,float(Limit)/std::max(right_-left_,bottom_-top_));
        out.w=std::max(8,int(std::ceil((right_-left_)*scale)));
        out.h=std::max(8,int(std::ceil((bottom_-top_)*scale)));
        out.p.resize(out.w*out.h);
        for(int y=0;y<out.h;++y)for(int x=0;x<out.w;++x)
            out.p[y*out.w+x]=Pixel(p,width_,height_,left_+(x+.5f)*(right_-left_)/out.w-.5f,
                top_+(y+.5f)*(bottom_-top_)/out.h-.5f);
        return out;
    }
    static float Cost(const Image& a,const Image& b,int x,int y,int dx,int dy) {
        float score=0;
        for(int py=-3;py<=3;py+=2)for(int px=-3;px<=3;px+=2) {
            const int ax=std::clamp(x+px,0,a.w-1),ay=std::clamp(y+py,0,a.h-1);
            const int bx=std::clamp(ax+dx,0,b.w-1),by=std::clamp(ay+dy,0,b.h-1);
            const auto& u=a.p[ay*a.w+ax];const auto& v=b.p[by*b.w+bx];
            for(int c=0;c<4;++c)score+=std::abs(u[c]-v[c])*(c==3?1.5f:1.f);
        }
        return score;
    }
    static Field Match(const Image& a,const Image& b) {
        Field field{};
        for(int gy=0;gy<Grid;++gy)for(int gx=0;gx<Grid;++gx) {
            const int x=gx*(a.w-1)/(Grid-1),y=gy*(a.h-1)/(Grid-1);
            float best=Cost(a,b,x,y,0,0);Point move{};
            // Penalise gratuitous displacement in a flat patch. The small
            // search radius cannot turn a head or invent another limb.
            for(int dy=-8;dy<=8;dy+=2)for(int dx=-8;dx<=8;dx+=2) {
                const float value=Cost(a,b,x,y,dx,dy)+1.5f*(dx*dx+dy*dy);
                if(value<best){best=value;move={float(dx),float(dy)};}
            }
            const int mx=int(move.x),my=int(move.y);
            for(int dy=my-1;dy<=my+1;++dy)for(int dx=mx-1;dx<=mx+1;++dx) {
                const float value=Cost(a,b,x,y,dx,dy)+1.5f*(dx*dx+dy*dy);
                if(value<best){best=value;move={float(dx),float(dy)};}
            }
            field[gy*Grid+gx]=move;
        }
        // A continuous field keeps adjacent patches attached, including the
        // muzzle, ears and paw roots. Preserve a coherent translation exactly.
        for(int pass=0;pass<2;++pass) {
            auto source=field;
            for(int y=1;y<Grid-1;++y)for(int x=1;x<Grid-1;++x) {
                Point p=source[y*Grid+x];p.x*=4;p.y*=4;
                for(int offset:{-1,1,-Grid,Grid}) {p.x+=source[y*Grid+x+offset].x;p.y+=source[y*Grid+x+offset].y;}
                field[y*Grid+x]={p.x/8,p.y/8};
            }
        }
        return field;
    }
    Point Flow(const Field& f,float x,float y) const {
        const float gx=std::clamp((x-left_)/std::max(1,right_-left_-1)*(Grid-1),0.f,float(Grid-1));
        const float gy=std::clamp((y-top_)/std::max(1,bottom_-top_-1)*(Grid-1),0.f,float(Grid-1));
        const int ix=int(gx),iy=int(gy),jx=std::min(ix+1,Grid-1),jy=std::min(iy+1,Grid-1);
        const float tx=gx-ix,ty=gy-iy;
        Point p;
        for(int c=0;c<2;++c) {
            const auto value=[&](int x,int y){return c?f[y*Grid+x].y:f[y*Grid+x].x;};
            const float v=(value(ix,iy)*(1-tx)+value(jx,iy)*tx)*(1-ty)+(value(ix,jy)*(1-tx)+value(jx,jy)*tx)*ty;
            if(c)p.y=v;else p.x=v;
        }
        return p;
    }
public:
    static void Translate(const unsigned char* from,unsigned char* to,int w,int h,float dx,float dy) {
        for(int y=0;y<h;++y)for(int x=0;x<w;++x) {
            const auto p=Pixel(from,w,h,x-dx,y-dy);
            for(int c=0;c<4;++c)to[(y*w+x)*4+c]=static_cast<unsigned char>(std::clamp(std::lround(p[c]),0L,255L));
        }
    }
    void Reset(){ready_=false;}
    bool Prepare(const unsigned char* from,const unsigned char* to,int w,int h) {
        Reset();if(!from||!to||w<1||h<1||w>4096||h>4096)return false;
        width_=w;height_=h;left_=w;top_=h;right_=bottom_=0;
        for(int y=0;y<h;++y)for(int x=0;x<w;++x)
            if(std::max(from[(y*w+x)*4+3],to[(y*w+x)*4+3])>4) {
                left_=std::min(left_,x);right_=std::max(right_,x+1);top_=std::min(top_,y);bottom_=std::max(bottom_,y+1);
            }
        if(right_<=left_||bottom_<=top_)return false;
        left_=std::max(0,left_-12);top_=std::max(0,top_-12);right_=std::min(w,right_+12);bottom_=std::min(h,bottom_+12);
        const auto a=Reduce(from),b=Reduce(to);forward_=Match(a,b);backward_=Match(b,a);
        for(auto* field:{&forward_,&backward_})for(auto& p:*field) {
            p.x*=float(right_-left_)/a.w;p.y*=float(bottom_-top_)/a.h;
        }
        ready_=true;return true;
    }
    bool Render(const unsigned char* from,const unsigned char* to,unsigned char* output,int w,int h,double fraction) const {
        if(!ready_||w!=width_||h!=height_||!from||!to||!output)return false;
        const float t=float(std::clamp(fraction,0.,1.));
        std::fill(output,output+std::size_t(w)*h*4,0);
        for(int y=top_;y<bottom_;++y)for(int x=left_;x<right_;++x) {
            const auto f=Flow(forward_,float(x),float(y)),b=Flow(backward_,float(x),float(y));
            const auto aPixel=Pixel(from,w,h,x-f.x*t,y-f.y*t);
            const auto bPixel=Pixel(to,w,h,x-b.x*(1-t),y-b.y*(1-t));
            auto* p=output+(y*w+x)*4;
            for(int c=0;c<4;++c)p[c]=static_cast<unsigned char>(std::clamp(std::lround(aPixel[c]*(1-t)+bPixel[c]*t),0L,255L));
        }
        return true;
    }
    bool RenderTarget(const unsigned char* from,const unsigned char* to,unsigned char* output,int w,int h,double fraction) const {
        if(!ready_||w!=width_||h!=height_||!from||!to||!output)return false;
        const float t=float(std::clamp(fraction,0.,1.));
        // A moving head must remain one drawing. Register the current target
        // toward the prior supported pose, without superimposing two faces.
        if(t==0){std::copy(from,from+std::size_t(w)*h*4,output);return true;}
        std::fill(output,output+std::size_t(w)*h*4,0);
        for(int y=top_;y<bottom_;++y)for(int x=left_;x<right_;++x) {
            const auto motion=Flow(backward_,float(x),float(y));
            const auto pixel=Pixel(to,w,h,x-motion.x*(1-t),y-motion.y*(1-t));
            for(int c=0;c<4;++c)output[(y*w+x)*4+c]=static_cast<unsigned char>(std::clamp(std::lround(pixel[c]),0L,255L));
        }
        return true;
    }
};
} // namespace cat::raster_transition
