#include "../src/cat_belly_rig.hpp"
#include <array>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>

namespace {
int checks=0;
#define CHECK(x) do { ++checks; if (!(x)) throw std::runtime_error(std::string(__FILE__)+":"+std::to_string(__LINE__)+" " #x); }while(false)
using namespace cat;
void FacesAreRemovedOnceAndSupportRemains() {
    // Both eye locations and the nose lie inside each actual face mask.
    for(auto p:{deform::Point{.40,.47},deform::Point{.72,.45},deform::Point{.57,.54}})
        CHECK(belly::HeadCoverage(4,p)==1);
    for(int frame:{24,25}) {
        for(auto p:{deform::Point{.69,.43},deform::Point{.86,.51},deform::Point{.79,.55}}) {
            const double coverage=belly::HeadCoverage(frame,p);
            CHECK(coverage==1);
            CHECK(belly::MaskPixel(0xffa08060,coverage,false)==0);
        }
        for(int x=0;x<=100;++x)for(double y:{.95,.98,1.0}) {
            const double coverage=belly::HeadCoverage(frame,{x/100.,y});
            CHECK(coverage==0);
            CHECK(belly::MaskPixel(0xffa08060,coverage,false)==0xffa08060);
        }
    }
    CHECK(belly::HeadCoverage(4,{.10,.80})==0); // canonical tail stays on body
    CHECK(belly::HeadCoverage(25,{.20,.70})==0); // belly stays on body
    for(int frame:{4,24,25}) for(int y=0;y<=100;++y)for(int x=0;x<=100;++x) {
        const double c=belly::HeadCoverage(frame,{x/100.,y/100.});
        CHECK(std::isfinite(c) && c>=0 && c<=1);
    }
}
void PremultipliedSourceOverHasNoNeckSeam() {
    for(unsigned a=0;a<=255;++a)for(unsigned coverage=0;coverage<=100;++coverage) {
        const unsigned pixel=(a<<24)|((a*3/4)<<16)|((a/2)<<8)|(a/4);
        const auto head=belly::MaskPixel(pixel,coverage/100.,true);
        const auto body=belly::MaskPixel(pixel,coverage/100.,false);
        for(unsigned shift:{0U,8U,16U,24U}) {
            const auto hp=(head>>shift)&255,bp=(body>>shift)&255,sp=(pixel>>shift)&255;
            const unsigned composed=hp+(bp*(255-(head>>24))+127)/255;
            CHECK(std::abs(static_cast<int>(composed)-static_cast<int>(sp))<=1);
            CHECK(hp<=(head>>24) && bp<=(body>>24));
        }
    }
    // The exact former failure: opaque fur at half neck coverage must remain
    // opaque after actual source-over, rather than falling to alpha191.
    const auto opaqueHead=belly::MaskPixel(0xff805020,.5,true);
    const auto opaqueBody=belly::MaskPixel(0xff805020,.5,false);
    CHECK((opaqueHead>>24)+((opaqueBody>>24)*(255-(opaqueHead>>24))+127)/255==255);
    // Exhaust each valid premultiplied colour value, including faint alpha.
    for(unsigned a=1;a<=255;++a) for(unsigned c=0;c<=a;++c) for(unsigned cov=0;cov<=100;cov+=5) {
        const auto pixel=(a<<24)|(c<<16)|(c<<8)|c;
        const auto h=belly::MaskPixel(pixel,cov/100.,true),b=belly::MaskPixel(pixel,cov/100.,false);
        const unsigned composed=(h&255)+((b&255)*(255-(h>>24))+127)/255;
        CHECK(std::abs(static_cast<int>(composed)-static_cast<int>(c))<=1);
        CHECK((h&255)<=(h>>24) && (b&255)<=(b>>24));
    }
    CHECK(belly::MaskPixel(0xff805020,0,true)==0);
    CHECK(belly::MaskPixel(0xff805020,1,true)==0xff805020);
    CHECK(belly::MaskPixel(0xff805020,std::numeric_limits<double>::quiet_NaN(),false)==0xff805020);
}
deform::Point World(deform::Point local,belly::HeadBone bone) {
    const double radians=bone.angle*3.14159265358979323846/180;
    return {bone.x+local.x*std::cos(radians)-local.y*std::sin(radians),bone.y+local.x*std::sin(radians)+local.y*std::cos(radians)};
}
void AContinuousBoneKeepsOneRigidFace() {
    const auto standing=belly::BoneFor(4,100,110),side=belly::BoneFor(24,140,85),back=belly::BoneFor(25,145,90);
    CHECK(standing.angle==0 && side.angle==0 && back.angle==12);
    const auto first=belly::BlendBone(4,100,110,25,145,90,0);
    const auto last=belly::BlendBone(4,100,110,25,145,90,1);
    CHECK(first.x==standing.x && first.y==standing.y && first.angle==standing.angle);
    CHECK(last.x==back.x && last.y==back.y && last.angle==back.angle);
    auto previous=first;
    for(int sample=0;sample<=1000;++sample) {
        const double t=sample/1000.;
        const auto bone=belly::BlendBone(4,100,110,25,145,90,t);
        const auto eyeA=World({-13,-1},bone),eyeB=World({13,1},bone),nose=World({0,10},bone);
        CHECK(std::abs(std::hypot(eyeA.x-eyeB.x,eyeA.y-eyeB.y)-std::hypot(26,2))<1e-10);
        CHECK(std::abs(std::hypot(eyeA.x-nose.x,eyeA.y-nose.y)-std::hypot(13,11))<1e-10);
        CHECK(std::abs(bone.x-previous.x)<.05 && std::abs(bone.y-previous.y)<.05 && std::abs(bone.angle-previous.angle)<.02);
        // Eyes remain above the supported lower edge and are never inverted.
        CHECK(eyeA.y<0 && eyeB.y<0 && nose.y<0 && eyeA.x<eyeB.x);
        previous=bone;
    }
    CHECK(belly::BlendBone(4,100,110,25,145,90,.25).angle==3); // input already eased
}
}
int main(){try{FacesAreRemovedOnceAndSupportRemains();PremultipliedSourceOverHasNoNeckSeam();AContinuousBoneKeepsOneRigidFace();std::cout<<"Belly head rig: "<<checks<<" checks passed\n";}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}
