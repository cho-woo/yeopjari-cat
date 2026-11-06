#include "../src/cat_articulated_walk.hpp"
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
int checks=0;
#define CHECK(condition) do { ++checks; if(!(condition)) throw std::runtime_error( \
    std::string(__FILE__)+":"+std::to_string(__LINE__)+" failed: " #condition); } while(false)
using cat::articulated::Point;
using cat::articulated::Length;
constexpr int width=192,height=171;
std::vector<std::uint32_t> Artwork() {
    // Opaque patterned upper body gives a byte-for-byte source identity
    // reference. The lower body provides continuous synthetic fur; empty
    // corner margins make accidental mesh writes and clipping observable.
    // Aspect and rounded sole positions follow the supported 3/4 kitten art;
    // a solid rectangle is not an anatomical input to this renderer.
    std::vector<std::uint32_t> pixels(width*height);
    for(int y=0;y<height;++y)for(int x=0;x<width;++x) {
        if(y>=static_cast<int>(height*.68)&&(x<width*.09||x>width*.85))continue;
        if(y>height*.92) {
            const double u=(x+.5)/width,v=(y+.5)/height;
            const double hind=std::pow((u-.211)/.06,2)+std::pow((v-.94)/.031,2);
            const double fore=std::pow((u-.704)/.06,2)+std::pow((v-.96)/.031,2);
            if(hind>1&&fore>1)continue;
        }
        const unsigned alpha=(x<4||x>width-5)?128:255;
        pixels[y*width+x]=(alpha<<24)|(((x*7+y*3)%alpha)<<16)|
            (((x*3+y*11)%alpha)<<8)|((x*13+y*5)%alpha);
    }
    return pixels;
}
unsigned AlphaAt(const cat::articulated::Rig& rig,Point point) {
    const int x=static_cast<int>(std::floor(point.x))+rig.PaddingX();
    const int y=static_cast<int>(std::floor(point.y));
    unsigned alpha=0;
    for(int yy=std::max(0,y-1);yy<=std::min(rig.Height()-1,y+1);++yy)
    for(int xx=std::max(0,x-1);xx<=std::min(rig.Width()-1,x+1);++xx)
        alpha=std::max(alpha,rig.Pixels()[yy*rig.Width()+xx]>>24);
    return alpha;
}
void SourceIdentityAndBounds() {
    const auto source=Artwork();cat::articulated::Rig rig;
    CHECK(rig.Prepare(source.data(),width,height,width*4));
    CHECK(rig.Width()==width+2*rig.PaddingX() && rig.SourceHeight()==height);
    CHECK(rig.Height()==height+8 && rig.GroundInsetPixels()==10);
    cat::gait::Cycle gait({18,.72,2.6},false,0);
    for(int sample=0;sample<32;++sample) {
        gait.Advance(22./30,1./30,true,false);
        CHECK(rig.Raster(gait.Targets(),110,110));
        const auto* output=rig.Pixels();
        for(int y=0;y<static_cast<int>(height*.68);++y)for(int x=0;x<width;++x)
            CHECK(output[y*rig.Width()+rig.PaddingX()+x]==source[y*width+x]);
        for(int y=0;y<rig.Height();++y) {
            CHECK((output[y*rig.Width()]>>24)==0);
            CHECK((output[y*rig.Width()+rig.Width()-1]>>24)==0);
            for(int x=0;x<rig.Width();++x) {
                const auto pixel=output[y*rig.Width()+x];const unsigned a=pixel>>24;
                CHECK(((pixel>>16)&255)<=a && ((pixel>>8)&255)<=a && (pixel&255)<=a);
            }
        }
    }
}
void BonesKeepLengthAndPawsKeepWorldContacts() {
    const auto source=Artwork();
    for(const auto& dimensions:{std::pair<double,double>{110,110},{90.2,114.4},{137.5,105.6}}) {
        cat::articulated::Rig rig;CHECK(rig.Prepare(source.data(),width,height,width*4));
        cat::gait::Cycle gait({18,.72,2.6},false,0);
        auto previous=gait.Targets();std::array<double,4> lastWorld{};
        bool first=true;int contacts=0,swingSamples=0;double root=0;
        for(int sample=0;sample<240;++sample) {
            const double travel=22./120;root+=travel;
            gait.Advance(travel,1./120,true,false);
            const auto feet=gait.Targets();
            CHECK(rig.Raster(feet,dimensions.first,dimensions.second));
            if(rig.ReachError()>=1e-7) throw std::runtime_error("reach error="+std::to_string(rig.ReachError())+" width="+std::to_string(dimensions.first)+" height="+std::to_string(dimensions.second)+" sample="+std::to_string(sample));
            const auto& bones=rig.Legs();
            for(int leg=0;leg<4;++leg) {
                const auto& bone=bones[leg];
                CHECK(!bone.clamped);
                CHECK(std::abs(Length(bone.knee-bone.root)-bone.upperLength)<1e-7);
                CHECK(std::abs(Length(bone.ankle-bone.knee)-bone.lowerLength)<1e-7);
                CHECK(Length(bone.paw-bone.targetPaw)<1e-7);
                const auto shin=bone.ankle-bone.root,knee=bone.knee-bone.root;
                const double bendSide=shin.x*knee.y-shin.y*knee.x;
                CHECK(leg>=2 ? bendSide>=-1e-7 : bendSide<=1e-7);
                const double world=root+bone.paw.x*dimensions.first/width;
                if(!first && feet[leg].stance && previous[leg].stance &&
                        feet[leg].contact==previous[leg].contact) {
                    CHECK(std::abs(world-lastWorld[leg])<1e-7);++contacts;
                }
                if(!feet[leg].stance) {
                    CHECK(bone.paw.y<height-2+1e-7);++swingSamples;
                } else CHECK(std::abs(bone.paw.y-(height-2))<1e-7);
                // A solved foot must exist in the raster too. Numeric IK by
                // itself cannot catch holes introduced by the skin triangles.
                CHECK(AlphaAt(rig,bone.paw)>96);
                for(int segment=0;segment<3;++segment) {
                    const Point from=segment==0?bone.root:segment==1?bone.knee:bone.ankle;
                    const Point to=segment==0?bone.knee:segment==1?bone.ankle:bone.paw;
                    for(double t:{.25,.5,.75})CHECK(AlphaAt(rig,from*(1-t)+to*t)>32);
                }
                previous[leg]=feet[leg];lastWorld[leg]=world;
            }
            first=false;
        }
        CHECK(contacts>300 && swingSamples>100);
    }
}
void FourLegOrderAndStoppedContacts() {
    cat::gait::Cycle gait({18,.72,2.6},false,0);
    auto previous=gait.Targets();std::vector<int> touchdowns;
    for(int sample=0;sample<720;++sample) {
        gait.Advance(22./240,1./240,true,false);const auto feet=gait.Targets();
        int supports=0;for(int leg=0;leg<4;++leg) {
            supports+=feet[leg].stance;
            if(feet[leg].stance&&!previous[leg].stance)touchdowns.push_back(leg);
        }
        CHECK(supports>=2 && supports<=3);
        CHECK(feet[0].stance||feet[1].stance);
        CHECK(feet[2].stance||feet[3].stance);previous=feet;
    }
    // Cyclic lateral sequence, allowing the first sample to start anywhere.
    const std::array<int,4> next{{2,3,1,0}};
    CHECK(touchdowns.size()>10);
    for(std::size_t i=1;i<touchdowns.size();++i)CHECK(touchdowns[i]==next[touchdowns[i-1]]);
    const auto held=gait.Targets();const double phase=gait.Phase();
    for(int sample=0;sample<120;++sample)gait.Advance(0,1./60,true,false);
    CHECK(gait.Phase()==phase && cat::gait::SameFeet(held,gait.Targets()));
    gait.Reset();const auto settings=gait.Settings();
    CHECK(settings.stride==18 && settings.duty==.72 && settings.lift==2.6);
}
void InvalidInputs() {
    auto source=Artwork();cat::articulated::Rig rig;cat::gait::Feet feet{};
    CHECK(!rig.Raster(feet,110,110));
    CHECK(!rig.Prepare(nullptr,width,height,width*4));
    CHECK(!rig.Prepare(source.data(),8,height,width*4));
    CHECK(!rig.Prepare(source.data(),width,height,width*4-1));
    CHECK(rig.Prepare(source.data(),width,height,width*4));
    CHECK(!rig.Raster(feet,std::numeric_limits<double>::quiet_NaN(),110));
    CHECK(!rig.Raster(feet,110,std::numeric_limits<double>::infinity()));
    feet[2].x=std::numeric_limits<double>::quiet_NaN();CHECK(!rig.Raster(feet,110,110));
    feet[2]={0,1,true,0};CHECK(!rig.Raster(feet,110,110));
    feet[2]={0,-1,true,0};CHECK(!rig.Raster(feet,110,110));
}
}
int main() {
    try {
        SourceIdentityAndBounds();BonesKeepLengthAndPawsKeepWorldContacts();
        FourLegOrderAndStoppedContacts();InvalidInputs();
        std::cout<<"Articulated walk: "<<checks<<" checks passed\n";
        return EXIT_SUCCESS;
    } catch(const std::exception& error) {std::cerr<<error.what()<<'\n';return EXIT_FAILURE;}
}
