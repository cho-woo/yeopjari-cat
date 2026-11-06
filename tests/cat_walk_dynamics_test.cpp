#include "../src/cat_walk_dynamics.hpp"
#include "../src/cat_articulated_walk.hpp"
#include "../src/cat_tail_pose.hpp"
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
namespace motion=cat::walk_dynamics;
constexpr int sourceWidth=192,sourceHeight=171;
bool Near(double a,double b,double tolerance=1e-8){return std::abs(a-b)<=tolerance;}
double Distance(motion::Point a,motion::Point b){return std::hypot(a.x-b.x,a.y-b.y);}
cat::gait::Feet BiasedFeet(const cat::gait::Cycle& cycle,motion::Mode mode){
    auto feet=cycle.Targets();
    for(unsigned leg=0;leg<4;++leg)feet[leg].x+=motion::PawBias(mode,leg);
    return feet;
}
std::vector<std::uint32_t> Artwork(){
    std::vector<std::uint32_t> pixels(sourceWidth*sourceHeight);
    for(int y=0;y<sourceHeight;++y)for(int x=0;x<sourceWidth;++x){
        if(y>=static_cast<int>(sourceHeight*.68)&&(x<sourceWidth*.09||x>sourceWidth*.85))continue;
        if(y>sourceHeight*.92){
            const double u=(x+.5)/sourceWidth,v=(y+.5)/sourceHeight;
            if(std::pow((u-.211)/.06,2)+std::pow((v-.94)/.031,2)>1&&
               std::pow((u-.704)/.06,2)+std::pow((v-.96)/.031,2)>1)continue;
        }
        pixels[y*sourceWidth+x]=0xff000000u|((60+x%150)<<16)|((50+y%160)<<8)|(30+(x+y)%180);
    }
    return pixels;
}
void RigidHeadAndInvertibleLowerSpine(){
    for(auto mode:{motion::Mode::Walk,motion::Mode::Trot,motion::Mode::Gallop})
    for(int sample=0;sample<48;++sample){
        const auto frame=motion::Evaluate(sample/48.,165,mode);
        CHECK(motion::Finite(frame));
        const motion::Point a{70,25},b{92,27},c{81,44};
        const auto aa=motion::BodyPoint(a,frame,110,110);
        const auto bb=motion::BodyPoint(b,frame,110,110);
        const auto cc=motion::BodyPoint(c,frame,110,110);
        CHECK(Near(Distance(aa,bb),Distance(a,b)));
        CHECK(Near(Distance(aa,cc),Distance(a,c)));
        CHECK(Near(Distance(bb,cc),Distance(b,c)));
        // Undo the public rigid part and its lower-spine-only stretch. This
        // catches torso/head scale pulses and an inconsistent inverse raster.
        const double angle=frame.pitchDegrees*motion::kPi/180;
        for(const motion::Point original:{a,b,c,motion::Point{25,86},motion::Point{65,92}}){
            const auto rendered=motion::BodyPoint(original,frame,110,110);
            const double dx=rendered.x-110*frame.pivotU-frame.torsoX;
            const double dy=rendered.y-110*frame.pivotV-frame.torsoY;
            const double preX=110*frame.pivotU+dx*std::cos(angle)+dy*std::sin(angle);
            const double preY=110*frame.pivotV-dx*std::sin(angle)+dy*std::cos(angle);
            CHECK(Near(motion::UnstretchX(preX,preY,frame,110,110),original.x));
            CHECK(Near(preY,original.y));
        }
    }
}
void EveryMotionParameterParticipatesInCacheIdentity(){
    const auto initial=motion::Evaluate(.17,165,motion::Mode::Gallop);
    CHECK(motion::Same(initial,initial));
    const std::array<double motion::Frame::*,8> fields{{&motion::Frame::torsoX,&motion::Frame::torsoY,
        &motion::Frame::pitchDegrees,&motion::Frame::spineStretch,&motion::Frame::tailDegrees,
        &motion::Frame::pivotU,&motion::Frame::pivotV,&motion::Frame::airborneWeight}};
    for(const auto field:fields){
        auto changed=initial;changed.*field+=.001;CHECK(!motion::Same(initial,changed));
        changed=initial;changed.*field=std::numeric_limits<double>::quiet_NaN();CHECK(!motion::Finite(changed));
    }
    for(unsigned leg=0;leg<4;++leg){
        auto changed=initial;changed.rootOffsets[leg].x+=.001;CHECK(!motion::Same(initial,changed));
        changed=initial;changed.rootOffsets[leg].y+=.001;CHECK(!motion::Same(initial,changed));
        changed=initial;changed.rootOffsets[leg].x=std::numeric_limits<double>::infinity();CHECK(!motion::Finite(changed));
    }
}
void PhaseWrapAndStoppedBodyRemainStable(){
    for(auto mode:{motion::Mode::Walk,motion::Mode::Trot,motion::Mode::Gallop}){
        for(double phase:{-.2,0.,.2,.42,.46,.5,.92,.96,1.,1.2}){
            const auto stopped=motion::Evaluate(phase,0,mode);
            CHECK(motion::Neutral(stopped) && motion::Finite(stopped));
            const auto frame=motion::Evaluate(phase,165,mode),wrapped=motion::Evaluate(phase+1,165,mode);
            CHECK(Near(frame.torsoX,wrapped.torsoX));CHECK(Near(frame.torsoY,wrapped.torsoY));
            CHECK(Near(frame.pitchDegrees,wrapped.pitchDegrees));CHECK(Near(frame.spineStretch,wrapped.spineStretch));
            CHECK(Near(frame.tailDegrees,wrapped.tailDegrees));CHECK(Near(frame.airborneWeight,wrapped.airborneWeight));
        }
        const auto before=motion::Evaluate(1-1e-7,165,mode),after=motion::Evaluate(1e-7,165,mode);
        CHECK(std::abs(before.torsoY-after.torsoY)<1e-4);
        CHECK(std::abs(before.pitchDegrees-after.pitchDegrees)<1e-4);
        CHECK(std::abs(before.tailDegrees-after.tailDegrees)<1e-4);
    }
    // The physical-speed controller changes gait in the zero-blend range.
    // No source-pose or attachment point may jump at that mode boundary.
    for(int step=0;step<100;++step)for(double speed:{12.,30.,35.})
        CHECK(motion::Same(motion::Evaluate(step/100.,speed,motion::Mode::Walk),
            motion::Evaluate(step/100.,speed,motion::Mode::Gallop)));
    CHECK(motion::Neutral(motion::Evaluate(std::numeric_limits<double>::quiet_NaN(),22)));
    CHECK(motion::Neutral(motion::Evaluate(.2,std::numeric_limits<double>::infinity())));
}
void MovingBodyKeepsSolvedPawsOnTheirGroundContacts(){
    const auto source=Artwork();
    for(auto mode:{motion::Mode::Walk,motion::Mode::Gallop})
    // Production dimensions use one common source scale, then nutrition.
    // Do not invent an independent square-body normalization before applying
    // the thin/wide factors: that would vertically stretch the actual art.
    for(const auto& dimensions:{std::pair<double,double>{114.2,114.2*sourceHeight/sourceWidth},
            {114.2*.82,114.2*sourceHeight/sourceWidth*1.04},
            {114.2*1.25,114.2*sourceHeight/sourceWidth*.96}}){
        cat::articulated::Rig rig;CHECK(rig.Prepare(source.data(),sourceWidth,sourceHeight,sourceWidth*4));
        const auto parameters=motion::ParametersFor(mode);const double speed=mode==motion::Mode::Walk?22:165;
        cat::gait::Cycle cycle(parameters,false,0);auto previous=BiasedFeet(cycle,mode);
        std::array<double,4> previousWorld{};bool first=true;
        int plantedChecks=0,flightSamples=0,movingRoots=0;double root=0;
        std::array<cat::articulated::Point,4> firstRoots{};
        for(int sample=0;sample<192;++sample){
            const double travel=parameters.stride/96;root+=travel;
            cycle.Advance(travel,travel/speed,true,false);
            const auto feet=BiasedFeet(cycle,mode);
            const auto frame=motion::Evaluate(cycle.Phase(),speed,mode,&feet);
            CHECK(rig.Raster(feet,dimensions.first,dimensions.second,frame));
            // A recovery arc may be projected onto an anatomical joint limit.
            // Ground contacts must remain exact; allow only subpixel recovery
            // projection in this synthetic, pattern-painted anatomy.
            CHECK(rig.ReachError()<1.0);
            const auto bones=rig.Legs();int supports=0;
            for(unsigned leg=0;leg<4;++leg){
                const auto& bone=bones[leg];
                CHECK(Near(cat::articulated::Length(bone.knee-bone.root),bone.upperLength));
                CHECK(Near(cat::articulated::Length(bone.ankle-bone.knee),bone.lowerLength));
                const double goalError=std::hypot((bone.paw.x-bone.targetPaw.x)*dimensions.first/sourceWidth,
                    (bone.paw.y-bone.targetPaw.y)*dimensions.second/sourceHeight);
                if(feet[leg].stance)CHECK(!bone.clamped && goalError<1e-7);
                else CHECK(goalError<1.0);
                const double world=root+bone.paw.x*dimensions.first/sourceWidth;
                if(!first&&feet[leg].stance&&previous[leg].stance&&feet[leg].contact==previous[leg].contact){
                    CHECK(Near(world,previousWorld[leg],1e-7));++plantedChecks;
                }
                if(feet[leg].stance)CHECK(Near(bone.paw.y,rig.Height()-rig.GroundInsetPixels()));
                supports+=feet[leg].stance;
                if(first)firstRoots[leg]=bone.root;
                else movingRoots+=cat::articulated::Length(bone.root-firstRoots[leg])>.05;
                previousWorld[leg]=world;
            }
            if(frame.airborneWeight>1e-7)CHECK(supports==0);
            flightSamples+=supports==0;previous=feet;first=false;
        }
        CHECK(plantedChecks>50 && movingRoots>100);
        CHECK(mode==motion::Mode::Walk?flightSamples==0:flightSamples>0);
    }
}
void ModeTransitionKeepsExistingSupportLoaded() {
    const auto source=Artwork();cat::articulated::Rig rig;
    CHECK(rig.Prepare(source.data(),sourceWidth,sourceHeight,sourceWidth*4));
    cat::gait::Cycle cycle(motion::ParametersFor(motion::Mode::Walk),false,0);
    cycle.Configure(motion::ParametersFor(motion::Mode::Walk));
    auto previous=cycle.Targets();std::array<double,4> previousWorld{};
    double root=0,speed=22;int anchors=0;
    for(int sample=0;sample<720;++sample){
        const auto mode=sample>=240&&sample<480?motion::Mode::Gallop:motion::Mode::Walk;
        std::array<double,4> bias{};
        for(unsigned leg=0;leg<4;++leg)bias[leg]=motion::PawBias(mode,leg);
        CHECK(cycle.Configure(motion::ParametersFor(mode),bias));
        speed+=((mode==motion::Mode::Gallop?165.:22.)-speed)*(-std::expm1(-1./120/.075));
        const double travel=speed/120;root+=travel;
        cycle.Advance(travel,1./120,true,false);const auto feet=cycle.Targets();
        const auto frame=motion::Evaluate(cycle.Phase(),cycle.Speed(),mode,&feet);
        int supports=0;for(const auto& foot:feet)supports+=foot.stance;
        if(supports)CHECK(frame.airborneWeight==0);
        CHECK(rig.Raster(feet,114.2,114.2*sourceHeight/sourceWidth,frame));
        const auto& bones=rig.Legs();
        for(unsigned leg=0;leg<4;++leg){
            const auto& bone=bones[leg];const double world=root+bone.paw.x*114.2/sourceWidth;
            if(feet[leg].stance) {
                CHECK(!bone.clamped);
                CHECK(cat::articulated::Length(bone.paw-bone.targetPaw)<1e-7);
                if(sample&&previous[leg].stance&&previous[leg].contact==feet[leg].contact){
                    CHECK(Near(world,previousWorld[leg],1e-7));++anchors;
                }
            }
            previousWorld[leg]=world;
        }
        previous=feet;
    }
    CHECK(anchors>500);
}

void InvalidFrameRejectedWithoutDestroyingTheCachedBitmap(){
    const auto source=Artwork();cat::articulated::Rig rig;
    CHECK(rig.Prepare(source.data(),sourceWidth,sourceHeight,sourceWidth*4));
    cat::gait::Cycle cycle({18,.72,3.1},false,0);const auto feet=cycle.Targets();
    CHECK(rig.Raster(feet,110,110));
    const std::vector<std::uint32_t> saved(rig.Pixels(),rig.Pixels()+rig.Width()*rig.Height());
    const std::array<double motion::Frame::*,8> fields{{&motion::Frame::torsoX,&motion::Frame::torsoY,
        &motion::Frame::pitchDegrees,&motion::Frame::spineStretch,&motion::Frame::tailDegrees,
        &motion::Frame::pivotU,&motion::Frame::pivotV,&motion::Frame::airborneWeight}};
    for(const auto field:fields){
        motion::Frame bad;bad.*field=std::numeric_limits<double>::quiet_NaN();
        CHECK(!rig.Raster(feet,110,110,bad));
        CHECK(std::equal(saved.begin(),saved.end(),rig.Pixels()));
    }
    for(const auto& setting:{std::pair{&motion::Frame::spineStretch,-1.0},
            std::pair{&motion::Frame::tailDegrees,-100.0},std::pair{&motion::Frame::airborneWeight,2.0}}){
        motion::Frame bad;bad.*setting.first=setting.second;CHECK(!rig.Raster(feet,110,110,bad));
    }
}
std::uint32_t Over(std::uint32_t front,std::uint32_t back){
    const unsigned inverse=255-(front>>24);std::uint32_t result=0;
    for(int shift=0;shift<32;shift+=8)result|=std::min(255u,((front>>shift)&255)+(((back>>shift)&255)*inverse+127)/255)<<shift;
    return result;
}
void TailRotationKeepsThePaintedShapeRigid(){
    for(double u:{.10,.27,.285,.5})for(double v:{.3,.52,.55,.58,.7})
    for(unsigned alpha:{64u,128u,255u}){
        const auto pixel=(alpha<<24)|((alpha/2)<<16)|((alpha/3)<<8)|(alpha/4);
        const auto split=cat::tail_pose::Split(pixel,u,v);const auto reconstructed=Over(split.body,split.tail);
        for(int shift=0;shift<32;shift+=8)
            CHECK(std::abs(static_cast<int>((pixel>>shift)&255)-static_cast<int>((reconstructed>>shift)&255))<=1);
    }
    for(double width:{90.,110.,140.})for(double angle:{-47.,-45.,-43.,0.,10.}){
        const cat::tail_pose::Point a{.12,.2},b{.22,.4};
        const auto transformedA=cat::tail_pose::SourceUV(a,angle,width,105);
        const auto transformedB=cat::tail_pose::SourceUV(b,angle,width,105);
        const double before=std::hypot((a.x-b.x)*width,(a.y-b.y)*105);
        const double after=std::hypot((transformedA.x-transformedB.x)*width,(transformedA.y-transformedB.y)*105);
        CHECK(Near(before,after));
    }
}
}
int main(){
    try{
        RigidHeadAndInvertibleLowerSpine();EveryMotionParameterParticipatesInCacheIdentity();
        PhaseWrapAndStoppedBodyRemainStable();MovingBodyKeepsSolvedPawsOnTheirGroundContacts();
        ModeTransitionKeepsExistingSupportLoaded();
        InvalidFrameRejectedWithoutDestroyingTheCachedBitmap();TailRotationKeepsThePaintedShapeRigid();
        std::cout<<"Walk/run body dynamics: "<<checks<<" checks passed\n";return EXIT_SUCCESS;
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return EXIT_FAILURE;}
}
