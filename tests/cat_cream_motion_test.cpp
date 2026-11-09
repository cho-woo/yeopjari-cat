#include "cat_animation.hpp"
#include "cat_cream_rig.hpp"
#include <cassert>
#include <iostream>
#include <vector>

int main() {
    using cat::cream::Motion;
    constexpr double dt=1./60;
    Motion motion;
    assert(motion.Sit()==1 && !motion.Ready());
    for(unsigned i=0;i<120;++i){motion.Prepare(dt,false,true,true);motion.Travel(0,dt,true);assert(motion.Sit()==1);}
    // The motor must wait for standing. Rendering is a read-only snapshot.
    unsigned gated=0;
    for(unsigned i=0;i<90;++i){
        const bool ready=motion.Prepare(dt,true,true,true);
        if(!ready)++gated;
        motion.Travel(ready?22*dt:0,dt,true);
        if(!ready)assert(motion.Distance()==0);
    }
    assert(gated>25&&gated<65&&motion.Ready()&&motion.Distance()>0);
    // Every continuing contact cancels actual ground-relative root travel.
    unsigned contacts=0;
    for(unsigned i=0;i<240;++i){
        const auto before=motion.Feet();
        motion.Prepare(dt,true,true,true);motion.Travel(22*dt,dt,true);
        const auto after=motion.Feet();
        for(unsigned leg=0;leg<4;++leg)if(before[leg].stance&&after[leg].stance&&before[leg].contact==after[leg].contact){
            assert(std::abs(after[leg].x-before[leg].x+22*dt)<1e-6);++contacts;
        }
    }
    assert(contacts>300);
    // A wall blocks stepping; a stopped actor settles and then sits.
    const auto blocked=motion.Feet();const double travel=motion.Distance();
    for(unsigned i=0;i<20;++i){motion.Prepare(dt,true,true,true);motion.Travel(0,dt,true);}
    assert(cat::gait::SameFeet(blocked,motion.Feet())&&travel==motion.Distance());
    for(unsigned i=0;i<120;++i){motion.Prepare(dt,false,true,true);motion.Travel(0,dt,true);}
    assert(motion.Sit()==1&&!motion.Active());
    for(const auto& foot:motion.Feet())assert(foot.x==0&&foot.y==0);
    // A reversal is continuous; pause/time gaps do not advance the posture.
    motion.Prepare(.1,true,true,true);const double middle=motion.Sit();
    motion.Prepare(0,false,true,true);motion.Travel(0,0,true);assert(motion.Sit()==middle);
    motion.Prepare(5,false,true,true);assert(motion.Sit()==middle);
    motion.Prepare(dt,false,true,true);assert(std::abs(motion.Sit()-middle)<.08);
    motion.Prepare(dt,false,false,false);assert(motion.Ready());
    // Physical/care overrides are never converted to a grounded walking rig.
    auto pose=cat::animation::AirbornePose(30,1);
    assert(!cat::animation::ApplyCreamLocomotion(pose,cat::animation::Motion::Walk,motion));
    pose=cat::animation::Evaluate(cat::animation::Motion::Eat,700,700,1);
    assert(!cat::animation::ApplyCreamLocomotion(pose,cat::animation::Motion::Eat,motion));
    pose=cat::animation::Evaluate(cat::animation::Motion::Idle,700,700,1);
    assert(cat::animation::ApplyCreamLocomotion(pose,cat::animation::Motion::Idle,motion)&&pose.creamRig);
    // Preallocated raster buffers and premultiplied alpha across the complete
    // posture range. Grounded front paws keep identical support coordinates.
    cat::cream::Rig rig;
    std::vector<std::uint32_t> parts(96*64,0xffefc99f);assert(rig.PrepareParts(parts.data(),96,64));
    const auto* pixels=rig.Pixels();
    for(int n=0;n<=60;++n){
        const double sit=n/60.;assert(rig.Raster({},sit)&&rig.Pixels()==pixels);
        auto foot=rig.PawGoals()[2];assert(foot.x==218&&foot.y==275);
        for(int p=0;p<cat::cream::Rig::Width*cat::cream::Rig::Height;++p){
            const auto color=pixels[p],alpha=color>>24;
            for(unsigned s=0;s<24;s+=8)assert(((color>>s)&255)<=alpha);
        }
    }
    assert(!rig.Raster({},NAN));
    std::cout<<"Cream rig: stand gate, planted feet, blocked motion, sitting, interruption, alpha and stable buffers passed\n";
}
