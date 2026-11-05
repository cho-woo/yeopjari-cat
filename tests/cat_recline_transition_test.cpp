#include "../src/cat_recline_transition.hpp"
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>
namespace {
using namespace cat::recline;
int checks=0;
#define CHECK(condition) do {++checks;if(!(condition))throw std::runtime_error(std::to_string(__LINE__)+": " #condition);}while(false)
// Actual alpha-cropped resource sizes at the renderer's110px reference.
const std::array<std::array<Geometry,3>,10> sizes{{
    {{{329*110./367,366*110./367},{665*.25,362*.25},{703*.25,352*.25}}},
    {{{269*110./297,293*110./297},{665*.25,361*.25},{705*.25,354*.25}}},
    {{{134*110./150,110},{150*110./150,88*110./150},{356*.44,176*.44}}},
    {{{133*110./147,110},{148*110./147,89*110./147},{353*.44,176*.44}}},
    {{{135*110./158,110},{150*110./158,92*110./158},{363*.44,185*.44}}},
    {{{133*110./136,110},{151*110./136,86*110./136},{358*.44,176*.44}}},
    {{{129*110./140,110},{151*110./140,83*110./140},{372*.44,169*.44}}},
    {{{113*110./150,110},{146*110./150,86*110./150},{371*.44,195*.44}}},
    {{{133*110./143,110},{150*110./143,84*110./143},{367*.44,190*.44}}},
    {{{132*110./139,110},{149*110./139,82*110./139},{371*.44,193*.44}}}
}};
Point MapFace(const Mesh& mesh,Point point,bool second) {
    const auto& native=second?mesh.second:mesh.first;
    for(int i=0;i<2;++i){const auto tri=mesh.triangles[i];
        const double area=Area(native[tri.a],native[tri.b],native[tri.c]);
        const double a=Area(native[tri.b],native[tri.c],point)/area,b=Area(native[tri.c],native[tri.a],point)/area,c=1-a-b;
        if(a>=-1e-8&&b>=-1e-8&&c>=-1e-8)return {mesh.common[tri.a].x*a+mesh.common[tri.b].x*b+mesh.common[tri.c].x*c,
            mesh.common[tri.a].y*a+mesh.common[tri.b].y*b+mesh.common[tri.c].y*c};
    }
    throw std::runtime_error("eye region outside affine face patch");
}
Point FacePoint(FaceTriangle f,double u,double v) {
    const Point centre{(f.left.x+f.right.x)*.5,(f.left.y+f.right.y)*.5};
    return {centre.x+u*(f.right.x-f.left.x)+v*(f.mouth.x-centre.x),centre.y+u*(f.right.y-f.left.y)+v*(f.mouth.y-centre.y)};
}
void AllSpeciesKeepFaceConnectedAndFloorFixed() {
    const std::array<int,3> frames{{4,24,25}};
    for(int c=0;c<10;++c)for(const auto shape:{Geometry{1,1},Geometry{.82,1.04},Geometry{1.25,.96}})
    for(double scale:{.65,1.,1.5,2.5})for(const auto& pair:{std::pair<int,int>{0,1},{1,0},{1,2},{2,1}}){
        const auto character=static_cast<cat::Character>(c);
        auto a=sizes[c][pair.first],b=sizes[c][pair.second];
        a.width*=shape.width*scale;a.height*=shape.height*scale;b.width*=shape.width*scale;b.height*=shape.height*scale;
        for(double t:{0.,.001,.125,.25,.375,.5,.625,.75,.875,.999,1.}){
            Mesh mesh;
            if(!Build(character,frames[pair.first],a,frames[pair.second],b,t,mesh))throw std::runtime_error("Build c="+std::to_string(c)+" pair="+std::to_string(pair.first)+">"+std::to_string(pair.second)+" shape="+std::to_string(shape.width)+","+std::to_string(shape.height)+" scale="+std::to_string(scale));
            ++checks;
            CHECK(mesh.vertices<=16&&mesh.count<=25);
            for(int i=0;i<mesh.count;++i){CHECK(MinimumArea(mesh,mesh.triangles[i])>0);}
            for(int i=0;i<5;++i){CHECK(mesh.first[i].x==mesh.second[i].x&&mesh.first[i].y==mesh.second[i].y);
                CHECK(mesh.common[i].x==mesh.first[i].x&&mesh.common[i].y==mesh.first[i].y);}
            for(int i=2;i<5;++i)CHECK(mesh.common[i].y==0);
            const auto fa=FaceTriangleFor(character,frames[pair.first],a),fb=FaceTriangleFor(character,frames[pair.second],b);
            for(double u:{-.9,-.5,0.,.5,.9})for(double v:{-1.2,-.3,0.,.6,1.2}){
                const auto pa=FacePoint(fa,u,v),pb=FacePoint(fb,u,v),expected=Blend(pa,pb,t);
                const auto ma=MapFace(mesh,pa,false),mb=MapFace(mesh,pb,true);
                CHECK(std::hypot(ma.x-expected.x,ma.y-expected.y)<1e-8);
                CHECK(std::hypot(mb.x-expected.x,mb.y-expected.y)<1e-8);
            }
        }
    }
}
void RasterCoverageAndMirroring() {
    constexpr int w=160,h=120,cw=420,ch=320;
    std::vector<std::uint32_t> source(w*h),normal(cw*ch),flipped(cw*ch);
    for(int y=0;y<h;++y)for(int x=0;x<w;++x)source[y*w+x]=0xff000000u|((40+x)<<16)|((30+y)<<8)|70;
    const auto unchanged=source;
    for(auto character:{cat::Character::Cream,cat::Character::Dog,cat::Character::Rabbit}) {
        Mesh mesh;
        CHECK(Build(character,24,{160,100},25,{175,95},.5,mesh));
        CHECK(Raster(source.data(),w,h,normal.data(),cw,ch,{160,100},mesh,false,{210,280}));
        CHECK(Raster(source.data(),w,h,flipped.data(),cw,ch,{160,100},mesh,false,{210,280},true));
        std::size_t visible=0;
        for(int y=0;y<ch;++y)for(int x=0;x<cw;++x){
            const auto pixel=normal[y*cw+x],other=flipped[y*cw+cw-x-1];
            for(int shift=0;shift<32;shift+=8)CHECK(std::abs(static_cast<int>((pixel>>shift)&255)-static_cast<int>((other>>shift)&255))<=1);
            if(pixel>>24)++visible;
            if(y>=280)CHECK(pixel==0);
            CHECK((pixel&255)<=(pixel>>24)&&((pixel>>8)&255)<=(pixel>>24)&&((pixel>>16)&255)<=(pixel>>24));
        }
        CHECK(visible>5000&&source==unchanged);
    }
    Mesh identity;
    CHECK(Build(cat::Character::Cream,24,{w,h},24,{w,h},0,identity));
    std::vector<std::uint32_t> exact(w*h);
    CHECK(Raster(source.data(),w,h,exact.data(),w,h,{w,h},identity,false,{w/2.,h}));
    CHECK(exact==source);
    const auto baseline=normal;
    auto bad=identity;bad.common[0].x=std::numeric_limits<double>::quiet_NaN();
    CHECK(!Raster(source.data(),w,h,normal.data(),cw,ch,{w,h},bad,false,{210,280}));
    CHECK(normal==baseline);
    bad=identity;bad.triangles[0].a=100;
    CHECK(!Raster(source.data(),w,h,normal.data(),cw,ch,{w,h},bad,false,{210,280}));
    CHECK(normal==baseline);
    CHECK(!Build(cat::Character::Cream,24,{0,100},25,{100,100},.5,bad));
}
void RecliningSleepClosesOnlyNativeEyes() {
    constexpr int w=300,h=190;
    std::vector<std::uint32_t> source(w*h,0xff667788u),closed(w*h,0xffbb9988u),output(w*h);
    for(int c=0;c<10;++c)for(int frame:{24,25}){
        const auto character=static_cast<cat::Character>(c);
        const auto landmarks=FaceFor(character,frame);
        const auto original=source;
        CHECK(CloseEyes(source.data(),w,h,output.data(),character,frame,1,
            {closed.data(),w,h,cat::species::ClosedFaceFor(character)}));
        CHECK(source==original);
        std::size_t changed=0;
        for(int y=0;y<h;++y)for(int x=0;x<w;++x){
            if(source[y*w+x]!=output[y*w+x])++changed;
            if(y>h*.98)CHECK(source[y*w+x]==output[y*w+x]);
        }
        CHECK(changed>100);
        for(auto eye:{landmarks.leftEye,landmarks.rightEye})
            CHECK(output[static_cast<int>(eye.y*h)*w+static_cast<int>(eye.x*w)]!=source[0]);
        CHECK(CloseEyes(source.data(),w,h,output.data(),character,frame,0,
            {closed.data(),w,h,cat::species::ClosedFaceFor(character)}));
        CHECK(output==source);
    }
    CHECK(TurnBlink(24,25,0)==0&&TurnBlink(24,25,1)==0&&TurnBlink(24,25,.5)==1);
    CHECK(TurnBlink(4,24,.5)==0&&TurnBlink(25,24,.5)==1);
}
void RecliningEyelidsPreserveEverySourceAlpha() {
    constexpr int w=300,h=190;
    std::vector<std::uint32_t> source(w*h),closed(w*h),output(w*h);
    const auto premultiply=[](unsigned alpha,unsigned red,unsigned green,unsigned blue){
        return (alpha<<24)|(((red*alpha+127)/255)<<16)|(((green*alpha+127)/255)<<8)|((blue*alpha+127)/255);
    };
    // Include every alpha, especially the 245..254 pixels allowed through the
    // eye mask. The old source-over paint made those pixels more opaque.
    for(int y=0;y<h;++y)for(int x=0;x<w;++x)
        source[y*w+x]=premultiply(static_cast<unsigned>((x+y*3)%256),58,112,193);
    const auto untouched=source;
    for(const auto character:{cat::Character::Cream,cat::Character::Dog,cat::Character::Rabbit})
    for(int frame:{24,25})for(double blink:{.20,.65,1.})for(unsigned lidAlpha:{0u,64u,245u,246u,250u,254u,255u,256u}) {
        if(lidAlpha<=255)std::fill(closed.begin(),closed.end(),premultiply(lidAlpha,235,34,128));
        else {
            // Bilinear sampling also crosses differing lid coverage, as it
            // does around antialiased pixels in the real closed-eye texture.
            constexpr std::array<unsigned,7> alpha{{0,128,245,246,250,254,255}};
            for(int y=0;y<h;++y)for(int x=0;x<w;++x)
                closed[y*w+x]=premultiply(alpha[(x/8+y/7)%alpha.size()],235,34,128);
        }
        CHECK(CloseEyes(source.data(),w,h,output.data(),character,frame,blink,
            {closed.data(),w,h,cat::species::ClosedFaceFor(character)}));
        bool exactAlpha=true,premultiplied=true,translucentUnchanged=true;std::size_t changed=0;
        for(std::size_t i=0;i<source.size();++i){
            const unsigned alpha=source[i]>>24;
            exactAlpha=exactAlpha && (output[i]>>24)==alpha;
            premultiplied=premultiplied && (output[i]&255)<=alpha && ((output[i]>>8)&255)<=alpha && ((output[i]>>16)&255)<=alpha;
            if(alpha<245)translucentUnchanged=translucentUnchanged && output[i]==source[i];
            changed+=output[i]!=source[i];
        }
        CHECK(exactAlpha);CHECK(premultiplied);CHECK(translucentUnchanged);
        CHECK(source==untouched);
        if(lidAlpha<=245)CHECK(output==source);
        else if(blink==1)CHECK(changed>(lidAlpha==256?0u:20u));
    }
    // At full closure a lid is normalized to the original coverage, not laid
    // over it as extra opaque paint. Assert colour as well as exact alpha.
    for(unsigned sourceAlpha:{245u,250u,254u,255u})for(unsigned lidAlpha:{246u,250u,254u,255u}) {
        std::fill(source.begin(),source.end(),premultiply(sourceAlpha,35,80,190));
        const auto lid=premultiply(lidAlpha,235,34,128);
        std::fill(closed.begin(),closed.end(),lid);
        CHECK(CloseEyes(source.data(),w,h,output.data(),cat::Character::Cream,24,1,
            {closed.data(),w,h,cat::species::ClosedFaceFor(cat::Character::Cream)}));
        const auto eye=FaceFor(cat::Character::Cream,24).leftEye;
        const auto pixel=output[static_cast<int>(eye.y*h)*w+static_cast<int>(eye.x*w)];
        CHECK((pixel>>24)==sourceAlpha);
        for(int shift:{0,8,16}) {
            const auto expected=static_cast<unsigned>(std::lround(((lid>>shift)&255)*static_cast<double>(sourceAlpha)/lidAlpha));
            CHECK(((pixel>>shift)&255)==expected);
        }
    }
}
}
int main(){try{AllSpeciesKeepFaceConnectedAndFloorFixed();RasterCoverageAndMirroring();RecliningSleepClosesOnlyNativeEyes();RecliningEyelidsPreserveEverySourceAlpha();
    std::cout<<"Recline registration: "<<checks<<" checks passed\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
