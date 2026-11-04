#include "../src/cat_expression.hpp"
#include "../src/cat_animation.hpp"
#include "../src/cat_species.hpp"
#include "../src/cat_hanging_rig.hpp"
#include <iostream>
#include <stdexcept>
#include <vector>
#define CHECK(x) do{++checks;if(!(x))throw std::runtime_error(#x);}while(false)
int main(){int checks=0;try{
    constexpr int w=160,h=180;
    std::vector<std::uint32_t> source(w*h),output(w*h);
    for(int y=0;y<h;++y)for(int x=0;x<w;++x){const unsigned a=x<4||y<4||x>w-5||y>h-5?0:255;source[y*w+x]=(a<<24)|((x*a/255)<<16)|((y*a/255)<<8)|(x*y%200*a/255);}
    CHECK(cat::expression::Apply(source.data(),w,h,output.data(),{}));CHECK(source==output);
    for(auto face:{cat::expression::Face{1,0,0,0,0},cat::expression::Face{0,1,0,0,0},cat::expression::Face{0,0,1,0,0},cat::expression::Face{0,0,0,1,0},cat::expression::Face{0,0,0,0,1}}){
        CHECK(cat::expression::Apply(source.data(),w,h,output.data(),face));CHECK(output!=source);
        for(int y=0;y<h;++y)for(int x=0;x<w;++x){const auto pixel=output[y*w+x];const unsigned a=pixel>>24;CHECK((pixel&255)<=a&&((pixel>>8)&255)<=a&&((pixel>>16)&255)<=a);
            if(y>h*.78||x<4||x>w-5||y<4)CHECK(pixel==source[y*w+x]);
            if(face.smile>0 && y<h*.52)CHECK(pixel==source[y*w+x]);
        }
    }
    CHECK(!cat::expression::Apply(source.data(),w,h,source.data(),{}));
    for(int c=0;c<cat::kCharacterCount;++c){const auto profile=cat::species::ProfileFor(static_cast<cat::Character>(c));CHECK(profile.jumpHeightMultiplier>=.7&&profile.jumpHeightMultiplier<=1.25);CHECK(profile.movementSpeedMultiplier>=.8&&profile.movementSpeedMultiplier<=1.2);}
    CHECK(cat::species::ProfileFor(cat::Character::Munchkin).jumpHeightMultiplier<1);
    CHECK(cat::species::ProfileFor(cat::Character::Rabbit).jumpHeightMultiplier>1);
    // Continuous body registration must not teleport the sole canonical head.
    // Adult footprints cover the sitting, side and back atlas proportions;
    // at 60Hz the head travels less than eight logical pixels, with the same
    // scale/angle of its face throughout every turn.
    for(int c=0;c<cat::kCharacterCount;++c){
        const auto character=static_cast<cat::Character>(c);
        auto head=[&](unsigned t){const auto p=cat::animation::Evaluate(cat::animation::Motion::BellyRoll,t,t,1);
            auto size=[](int f){return f==4?std::pair<double,double>{100,110}:f==24?std::pair<double,double>{140,60}:std::pair<double,double>{150,65};};
            const int second=p.blendFrame<0?p.frame:p.blendFrame;const auto a=size(p.frame),b=size(second);
            return cat::belly::BlendBone(character,p.frame,a.first,a.second,second,b.first,b.second,p.blendFrame<0?0:p.blend);};
        for(unsigned t=16;t<=4320;++t){const auto a=head(t-16),b=head(t);CHECK(std::hypot(a.x-b.x,a.y-b.y)<8.0);CHECK(std::abs(a.angle-b.angle)<1.7);}
    }
    for(int c=0;c<cat::kCharacterCount;++c)for(double height:{70.0,110.0,160.0}){
        const auto character=static_cast<cat::Character>(c);
        const auto bone=cat::hanging::BoneFor(character,height,height);
        const auto anchor=cat::belly::HeadAnchor(character);
        const double neck=bone.y+(cat::belly::Neckline(character)-anchor.y)*height;
        CHECK(std::abs(neck+.57*height)<1e-9);CHECK(bone.x==0&&bone.angle==0);
        CHECK(cat::belly::HeadCoverage(character,26,{.50,.28})==1);
        CHECK(cat::belly::HeadCoverage(character,26,{.30,.47})==0);
        CHECK(cat::hanging::BodyWidthScale(character)>=1&&cat::hanging::BodyWidthScale(character)<=1.5);
    }
    for(auto motion:{cat::animation::Motion::Yawn,cat::animation::Motion::SleepTalk,cat::animation::Motion::Pride,cat::animation::Motion::Startled}){
        bool varied=false;auto first=cat::animation::Evaluate(motion,0,0,1);
        for(unsigned t=0;t<3000;t+=17){const auto p=cat::animation::Evaluate(motion,t,t,1);CHECK(p.frame==4&&p.blendFrame==-1&&p.scaleX==1&&p.scaleY==1&&p.lift==0);CHECK(cat::deform::Finite(p.joints));varied|=p.face.blink!=first.face.blink||p.face.mouth!=first.face.mouth||p.joints.headNod!=first.joints.headNod;}
        CHECK(varied);
    }
    const int sleepFrames[]{6,24,4,25,27,28};
    for(unsigned v=0;v<6;++v){const auto p=cat::animation::EvaluateSleep(v,1000,1000,1);CHECK(p.lift==0&&p.groundContact);CHECK(p.frame==sleepFrames[v]);
        const auto perched=cat::animation::EvaluateSleep(v,1000,1000,1,true);CHECK(perched.frame==4&&perched.lift==0&&perched.face.blink>.7&&perched.joints.headNod>0);}
    // The real renderer supplies a registered, premultiplied local closed-eye
    // texture. Covering the iris must keep the same body/silhouette pixels.
    std::vector<std::uint32_t> closed(source.size(),0xff665544u);
    for(int c=0;c<cat::kCharacterCount;++c){
        const auto character=static_cast<cat::Character>(c);
        CHECK(cat::expression::Apply(source.data(),w,h,output.data(),{1,0,0,0,0},cat::species::FaceFor(character),{},
            {closed.data(),w,h,cat::species::ClosedFaceFor(character)}));
        CHECK(output!=source);
        for(int y=static_cast<int>(h*.78);y<h;++y)for(int x=0;x<w;++x)CHECK(output[y*w+x]==source[y*w+x]);
    }
    for(unsigned t=660;t<1200;++t)CHECK(cat::animation::Evaluate(cat::animation::Motion::ToyPaw,t,t,1).frame!=13);
    CHECK(cat::OutfitAvailable(cat::Outfit::Santa,12,false));CHECK(!cat::OutfitAvailable(cat::Outfit::Santa,11,false));CHECK(cat::OutfitAvailable(cat::Outfit::Birthday,2,true));
    std::cout<<"PASS: local expressions, species profiles and pose identity; "<<checks<<" checks\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
