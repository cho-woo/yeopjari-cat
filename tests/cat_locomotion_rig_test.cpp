#include "../src/cat_locomotion_rig.hpp"
#include <cstdlib>
#include <iostream>
#include <limits>
#include <new>
#include <stdexcept>
#include <string>

namespace allocation_probe { bool enabled=false; std::size_t count=0; }
void* operator new(std::size_t bytes) {
    if(allocation_probe::enabled) ++allocation_probe::count;
    if(void* memory=std::malloc(bytes?bytes:1))return memory;
    throw std::bad_alloc();
}
void* operator new[](std::size_t bytes) {return ::operator new(bytes);}
// Keep replacement deallocators out of callers so GCC does not mistake this
// intentional malloc-backed new/delete probe for a mismatched API at callsites.
[[gnu::noinline]] void operator delete(void* memory) noexcept {std::free(memory);}
[[gnu::noinline]] void operator delete[](void* memory) noexcept {std::free(memory);}
[[gnu::noinline]] void operator delete(void* memory,std::size_t) noexcept {std::free(memory);}
[[gnu::noinline]] void operator delete[](void* memory,std::size_t) noexcept {std::free(memory);}

namespace {
int checks=0;
#define CHECK(c) do{++checks;if(!(c))throw std::runtime_error(std::string(__FILE__)+":"+std::to_string(__LINE__)+" " #c);}while(false)
bool Near(double a,double b,double tolerance=1e-10){return std::abs(a-b)<=tolerance;}

void PlantedFeetCancelActualRootDistance() {
    for(double stride:{24.,28.,38.})for(double duty:{.54,.62,.72})
    for(double growth:{.55,1.,1.1})for(double dpi:{1.,1.25,2.})for(int direction:{-1,1}) {
        double previous=0;
        for(int sample=0;sample<80;++sample) {
            const double phase=duty*sample/80;
            const auto foot=cat::walk::FootAt(phase,stride,duty,5.5);
            const double root=direction*phase*stride*growth*dpi;
            const double screenFoot=root+direction*foot.x*growth*dpi;
            CHECK(foot.stance && foot.y==0);
            if(sample)CHECK(Near(screenFoot,previous));
            previous=screenFoot;
        }
    }
}

void CompleteCyclesAreFiniteAndC1AtLiftAndPlant() {
    constexpr double epsilon=1e-6;
    for(double stride:{24.,28.,38.})for(double duty:{.54,.62,.72})for(double lift:{0.,3.5,5.5,7.}) {
        bool lifted=false;
        for(int sample=0;sample<1000;++sample) {
            const double phase=sample/1000.;
            const auto foot=cat::walk::FootAt(phase,stride,duty,lift);
            const auto nextCycle=cat::walk::FootAt(phase+1,stride,duty,lift);
            CHECK(std::isfinite(foot.x)&&std::isfinite(foot.y));
            CHECK(Near(foot.x,nextCycle.x)&&Near(foot.y,nextCycle.y));
            CHECK(foot.y<=0 && foot.y>=-lift);
            if(phase<duty)CHECK(foot.stance && foot.y==0);
            else {CHECK(!foot.stance);lifted=lifted||foot.y<-.8*lift;}
        }
        if(lift>0)CHECK(lifted);
        for(double boundary:{0.,duty,1.}) {
            const auto a=cat::walk::FootAt(boundary-epsilon,stride,duty,lift);
            const auto b=cat::walk::FootAt(boundary,stride,duty,lift);
            const auto c=cat::walk::FootAt(boundary+epsilon,stride,duty,lift);
            CHECK(std::abs(a.x-b.x)<.001 && std::abs(c.x-b.x)<.001);
            CHECK(std::abs((b.x-a.x)/epsilon-(c.x-b.x)/epsilon)<.01);
            CHECK(std::abs((b.y-a.y)/epsilon-(c.y-b.y)/epsilon)<.01);
            CHECK(Near((c.x-a.x)/(2*epsilon),-stride,.01));
        }
    }
    for(double invalid:{std::numeric_limits<double>::quiet_NaN(),std::numeric_limits<double>::infinity()})
    for(int field=0;field<4;++field) {
        double values[]={.2,28,.62,5.5};values[field]=invalid;
        const auto foot=cat::walk::FootAt(values[0],values[1],values[2],values[3]);
        CHECK(foot.x==0 && foot.y==0 && foot.stance);
    }
}

struct Anatomy {
    static constexpr int width=127,height=137,pitch=132;
    std::vector<std::uint32_t> pixels=std::vector<std::uint32_t>(pitch*height,0x5a123456);
    Anatomy(bool variedAlpha=false) {
        for(int y=0;y<height;++y)for(int x=0;x<width;++x) {
            const double u=(x+.5)/width,v=(y+.5)/height;
            const bool head=std::pow((u-.68)/.22,2)+std::pow((v-.32)/.25,2)<=1;
            const bool torso=std::pow((u-.48)/.43,2)+std::pow((v-.66)/.22,2)<=1;
            const bool rear=u>.10&&u<.25&&v>.70;
            const bool middle=u>.39&&u<.51&&v>.79;
            const bool front=u>.84&&u<.98&&v>.70;
            unsigned a=0;
            if((head||torso||rear||middle||front)&&y<height-2)
                a=variedAlpha && !(rear||middle||front)?96+((x+3*y)%5)*31:255;
            const unsigned r=a*(40+3*x%170)/255,g=a*(30+5*y%190)/255,b=a*(20+(x+y)%150)/255;
            pixels[y*pitch+x]=(a<<24)|(r<<16)|(g<<8)|b;
        }
    }
};

void MasksPartitionAndNeutralSourceOverReconstructsAnatomy() {
    Anatomy source(true);
    const auto original=source.pixels;
    cat::walk::Rig rig;
    const auto profile=cat::walk::ProfileFor(cat::Character::Cream);
    CHECK(rig.Prepare(source.pixels.data(),source.width,source.height,source.pitch*4,profile));
    CHECK(rig.Ready() && rig.SourceWidth()==source.width && rig.Height()==source.height);
    CHECK(rig.Width()==source.width+2*rig.PaddingX() && rig.PaddingX()>0);
    // Near-zero horizontal displacement and zero swing lift isolate the real
    // body/leg alpha composition. All three opaque soles are already aligned.
    CHECK(rig.Raster(.2,1e-5,127,137,.62,0));
    int fractionalMaskPixels=0,nonempty=0;
    for(int y=0;y<source.height;++y)for(int x=0;x<source.width;++x) {
        const auto masks=cat::walk::Rig::Masks((x+.5)/source.width,(y+.5)/source.height,profile);
        double total=0;for(double mask:masks){CHECK(std::isfinite(mask)&&mask>=0&&mask<=1);total+=mask;}
        CHECK(total<=1+1e-12);
        const auto wanted=source.pixels[y*source.pitch+x];
        const auto actual=rig.Pixels()[y*rig.Width()+x+rig.PaddingX()];
        if(wanted>>24){++nonempty;if(total>.01&&total<.99)++fractionalMaskPixels;}
        for(int shift=0;shift<32;shift+=8)
            CHECK(std::abs(static_cast<int>((actual>>shift)&255)-static_cast<int>((wanted>>shift)&255))<=2);
    }
    CHECK(nonempty>5000 && fractionalMaskPixels>100);
    CHECK(source.pixels==original); // Includes source row-padding sentinels.
}

void RealLegMotionKeepsHeadPixelsAndUsesHorizontalPadding() {
    Anatomy source;
    cat::walk::Rig rig;
    CHECK(rig.Prepare(source.pixels.data(),source.width,source.height,source.pitch*4,cat::walk::ProfileFor(cat::Character::Cream)));
    CHECK(rig.Raster(0,38,110,110,.54,7));
    const auto* pointer=rig.Pixels();
    const std::vector<std::uint32_t> first(pointer,pointer+rig.Width()*rig.Height());
    std::size_t changed=0,headOpaque=0,outsideSource=0;
    for(int frame=0;frame<24;++frame) {
        CHECK(rig.Raster(frame/24.,38,110,110,.54,7));
        CHECK(rig.Pixels()==pointer);
        for(int y=0;y<rig.Height();++y)for(int x=0;x<rig.Width();++x) {
            const auto pixel=rig.Pixels()[y*rig.Width()+x],a=pixel>>24;
            CHECK((pixel&255)<=a&&((pixel>>8)&255)<=a&&((pixel>>16)&255)<=a);
            if(y<static_cast<int>(source.height*.68)) {
                const int sx=x-rig.PaddingX();
                const auto wanted=sx>=0&&sx<source.width?source.pixels[y*source.pitch+sx]:0;
                CHECK(pixel==wanted);
                headOpaque+=a>200;
            }
            if(frame)changed+=pixel!=first[y*rig.Width()+x];
            if(x<rig.PaddingX() || x>=rig.PaddingX()+source.width)outsideSource+=a>64;
        }
    }
    CHECK(headOpaque>1000 && changed>1000 && outsideSource>50);
}

void NaturalCadenceAndWeightTransferKeepSupport() {
    // Near fore/hind alternate in a trot; a walk staggers their lift-offs.
    // The far hind moves with the opposite foreleg only in the trot.
    CHECK(Near(cat::walk::LegPhase(0,.54)-cat::walk::LegPhase(2,.54),.5));
    CHECK(Near(cat::walk::LegPhase(1,.54),cat::walk::LegPhase(2,.54)));
    CHECK(std::abs(cat::walk::LegPhase(0,.68)-cat::walk::LegPhase(2,.68))<.4);
    CHECK(std::abs(cat::walk::LegPhase(1,.68)-cat::walk::LegPhase(2,.68))>.4);
    Anatomy source;
    cat::walk::Rig rig;
    CHECK(rig.Prepare(source.pixels.data(),source.width,source.height,source.pitch*4,
        cat::walk::ProfileFor(cat::Character::Cream)));
    for(const auto gait:{std::array<double,3>{24,.68,3.2},{20,.78,1.8},{32,.54,4.8}})
    for(const auto body:{std::array<double,2>{110,110},{90,114.4},{137.5,105.6}}) {
        for(int frame=0;frame<=32;++frame) {
            const double phase=frame/32.;
            CHECK(rig.Raster(phase,gait[0],body[0],body[1],gait[1],gait[2]));
            CHECK(rig.BodyOffset()>=0 && rig.BodyOffset()<=1.4+1e-12);
            int bottom=0;
            for(int y=0;y<rig.Height();++y)for(int x=0;x<rig.Width();++x)
                if((rig.Pixels()[y*rig.Width()+x]>>24)>153)bottom=std::max(bottom,y+1);
            const double pawY=(bottom-(rig.Height()-2))*body[1]/rig.Height()+rig.BodyOffset();
            CHECK(pawY>=-1.3 && pawY<=1.2);
            // Compression is a smooth translation, with zero velocity at the
            // cycle seam. It cannot create a face size/scale pulse.
            CHECK(Near(cat::walk::BodyCompression(phase,gait[0],gait[1]),
                       cat::walk::BodyCompression(phase+1,gait[0],gait[1])));
        }
    }
}

void RasterReusesPreparedMemoryAndRejectsBadInputs() {
    Anatomy source;
    cat::walk::Rig rig;
    CHECK(!rig.Raster(0,28,110,110));
    CHECK(!rig.Prepare(nullptr,127,137,132*4,{}));
    CHECK(!rig.Prepare(source.pixels.data(),7,137,132*4,{}));
    CHECK(!rig.Prepare(source.pixels.data(),127,137,126*4,{}));
    CHECK(!rig.Prepare(source.pixels.data(),2049,137,2049*4,{}));
    CHECK(!rig.Prepare(source.pixels.data(),127,137,132*4,{.1,.82,.72}));
    std::array<cat::walk::Profile,8> badProfiles{};
    badProfiles[0].belly=std::numeric_limits<double>::max();
    badProfiles[1].frontDrop=.2;badProfiles[2].rearDrop=-.01;
    badProfiles[3].frontPartition=.8;badProfiles[4].frontSlope=.2;
    badProfiles[5].frontPartition=.6;badProfiles[5].frontSlope=.15;
    badProfiles[6].belly=.78;badProfiles[6].frontDrop=.14;
    badProfiles[7].frontSlope=std::numeric_limits<double>::quiet_NaN();
    for(const auto& profile:badProfiles) {
        CHECK(!rig.Prepare(source.pixels.data(),127,137,132*4,profile));
        CHECK(!rig.Ready());
    }
    std::vector<std::uint32_t> transparent(127*137);
    CHECK(!rig.Prepare(transparent.data(),127,137,127*4,{}));
    CHECK(!rig.Ready());
    CHECK(rig.Prepare(source.pixels.data(),127,137,132*4,cat::walk::ProfileFor(cat::Character::Cream)));
    CHECK(rig.Raster(.3,28,110,110));
    const auto* pixels=rig.Pixels();
    const double beforeOffset=rig.BodyOffset();
    const std::vector<std::uint32_t> before(pixels,pixels+rig.Width()*rig.Height());
    for(int field=0;field<6;++field)for(double invalid:{std::numeric_limits<double>::quiet_NaN(),std::numeric_limits<double>::infinity()}) {
        double args[]={.3,28,110,110,.62,5.5};args[field]=invalid;
        CHECK(!rig.Raster(args[0],args[1],args[2],args[3],args[4],args[5]));
        CHECK(std::equal(before.begin(),before.end(),rig.Pixels()));
        CHECK(rig.BodyOffset()==beforeOffset);
    }
    CHECK(!rig.Raster(.3,0,110,110));CHECK(!rig.Raster(.3,61,110,110));
    CHECK(!rig.Raster(.3,28,9,110));CHECK(!rig.Raster(.3,28,110,9));
    CHECK(!rig.Raster(.3,28,110,110,.49));CHECK(!rig.Raster(.3,28,110,110,.86));
    CHECK(!rig.Raster(.3,28,110,110,.62,-1));CHECK(!rig.Raster(.3,28,110,110,.62,13));
    // This fold is discovered in a later leg, after earlier legs would have
    // been drawn by the old implementation. Failed updates must preserve the
    // cached image, since the next draw may reuse that earlier valid phase.
    CHECK(!rig.Raster(.75,28,110,10,.62,12));
    CHECK(std::equal(before.begin(),before.end(),rig.Pixels()));
    allocation_probe::count=0;allocation_probe::enabled=true;
    bool success=true;
    for(int frame=0;frame<100;++frame)success=rig.Raster(frame/100.,28,110,110)&&success;
    allocation_probe::enabled=false;
    CHECK(success && allocation_probe::count==0);
    CHECK(rig.Pixels()==pixels);
    CHECK(!rig.Prepare(nullptr,127,137,132*4,{}));
    CHECK(!rig.Ready()); // Invalid replacement art cannot keep an old rig active.
    CHECK(!rig.Raster(.3,28,110,110));
    CHECK(rig.Prepare(source.pixels.data(),127,137,132*4,cat::walk::ProfileFor(cat::Character::Cream)));
    CHECK(rig.Ready() && rig.Raster(.3,28,110,110));
}
}

int main() {
    try {
        PlantedFeetCancelActualRootDistance();CompleteCyclesAreFiniteAndC1AtLiftAndPlant();
        MasksPartitionAndNeutralSourceOverReconstructsAnatomy();RealLegMotionKeepsHeadPixelsAndUsesHorizontalPadding();
        NaturalCadenceAndWeightTransferKeepSupport();
        RasterReusesPreparedMemoryAndRejectsBadInputs();
        std::cout<<"Articulated locomotion: "<<checks<<" checks passed\n";
    } catch(const std::exception& error) {std::cerr<<error.what()<<'\n';return 1;}
}
