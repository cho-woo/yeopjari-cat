#include "../src/cat_species_belly.hpp"
#include <array>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
void Require(bool value,const char* reason) { if(!value)throw std::runtime_error(reason); }
bool Near(double a,double b) { return std::abs(a-b)<1e-10; }
}
int main() {
    try {
        using namespace cat;
        for (auto character:{Character::Cream,Character::SilverTabby}) {
            for (int frame:{4,24,25}) {
                for (deform::Point point:std::array<deform::Point,4>{{{.15,.55},{.5,.1},{.7,.45},{.9,.95}}})
                    Require(belly::HeadCoverage(character,frame,point)==belly::HeadCoverage(frame,point),"legacy mask changed");
                const auto a=belly::BoneFor(character,frame,150,110),b=belly::BoneFor(frame,150,110);
                Require(a.x==b.x&&a.y==b.y&&a.angle==b.angle,"legacy registration changed");
            }
        }
        for (int index=2;index<kCharacterCount;++index) {
            const auto character=static_cast<Character>(index);
            Require(belly::HeadCoverage(character,4,{.4,.08})>.99,"canonical ear tip left in body");
            Require(belly::HeadCoverage(character,4,{.72,.45})>.99,"canonical face clipped");
            Require(belly::HeadCoverage(character,24,{.5,.10})>.99,"side ear left behind");
            Require(belly::HeadCoverage(character,25,{.94,.90})>.99,"back chin strip left behind");
            Require(belly::HeadCoverage(character,25,{.20,.80})<.01,"belly body removed");
            if(character!=Character::Dog&&character!=Character::Rabbit)
                Require(belly::HeadCoverage(character,4,{.15,.55})==0,"tail travels with canonical head");
            const auto first=belly::BoneFor(character,4,100,110),last=belly::BoneFor(character,25,150,85);
            const auto middle=belly::BlendBone(character,4,100,110,25,150,85,.5);
            Require(Near(middle.x,(first.x+last.x)/2)&&Near(middle.y,(first.y+last.y)/2)&&Near(middle.angle,6),"bone transition discontinuity");
            const auto doubled=belly::BoneFor(character,25,300,170);
            Require(Near(doubled.x,last.x*2)&&Near(doubled.y,last.y*2),"DPI/growth registration drift");
            Require(belly::HeadCoverage(character,4,{-1,.5})==0,"invalid uv accepted");
            Require(belly::HeadCoverage(character,4,{std::numeric_limits<double>::quiet_NaN(),.5})==0,"nonfinite uv accepted");
            Require(belly::BoneFor(character,25,0,85).x==0,"invalid size accepted");
            for (const auto point:std::array<deform::Point,3>{{{.2,.3},{.5,.43},{.8,.65}}})
                Require(belly::HeadCoverage(character,26,point)==belly::HeadCoverage(26,point),"hanging grip mask diverged");
        }
        Require(belly::HeadCoverage(Character::Rabbit,24,{.40,.20})>.99,"rabbit third ear regression");
        Require(belly::HeadCoverage(Character::Dog,25,{.96,.86})>.99,"puppy chin fragment regression");
        Require(belly::HeadCoverage(Character::Rabbit,26,{.90,.425})==1,"hanging rabbit cheek strip retained");
        Require(belly::HeadCoverage(Character::Rabbit,26,{.90,.465})==1,"outer hanging rabbit cheek retained");
        for (int x=0;x<=100;++x) for (int y=455+static_cast<int>(std::ceil(.25*std::max(0,x-80)*10));y<=600;++y) {
            const deform::Point uv{x/100.0,y/1000.0};
            Require(belly::HeadCoverage(Character::Rabbit,26,uv)==belly::HeadCoverage(26,uv),"hanging rabbit gripping paw changed");
        }
        std::cout<<"Species belly: legacy preservation, head-only masks and continuous registration passed\n";
    } catch(const std::exception& error) { std::cerr<<error.what()<<'\n';return 1; }
}
