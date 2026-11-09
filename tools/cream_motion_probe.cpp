#include "cat_cream_motion.hpp"
#include "cat_cream_rig.hpp"
#include "cat_physics.hpp"
#include <cstdint>
struct Probe {cat::cream::Rig rig;cat::cream::Motion motion;cat::physics::Controller physical;};
extern "C" {
void* cream_create(const std::uint32_t* parts,int w,int h){auto* v=new Probe;if(!v->rig.PrepareParts(parts,w,h)){delete v;return nullptr;}v->physical.Reset(300,40,2000);return v;}
void cream_destroy(Probe* p){delete p;}
int cream_step(Probe* p,double seconds,double speed,int wantsWalk){
 const auto before=p->physical.CurrentPose();
 const bool ready=p->motion.Prepare(seconds,wantsWalk||std::abs(before.velocityX)>2,true,before.grounded);
 p->physical.SetTargetVelocity(ready?speed:0);
 const auto previous=p->physical.RenderPose();p->physical.Update(seconds);
 const auto now=p->physical.RenderPose();
 p->motion.Travel(now.relativeTravelX-previous.relativeTravelX,seconds,now.grounded);
 return p->rig.Raster(p->motion.Feet(),p->motion.Sit());
}
int cream_pose(Probe* p,double sit){return p->rig.Raster({},sit);}
const std::uint32_t* cream_pixels(Probe* p){return p->rig.Pixels();}
double cream_sit(Probe* p){return p->motion.Sit();}
double cream_distance(Probe* p){return p->physical.RenderPose().x-300;}
}
