#include "cat_articulated_walk.hpp"
#include "cat_gait.hpp"
struct Review{cat::articulated::Rig rig;cat::gait::Cycle gait{{18,.72,2.6},false,0};};
extern "C"{
void* articulated_create(const std::uint32_t* data,int w,int h){auto p=new Review;if(!p->rig.Prepare(data,w,h,w*4)){delete p;return nullptr;}return p;}
void articulated_destroy(void* p){delete static_cast<Review*>(p);}
int articulated_step(void* p,double distance,double dt,double width,double height){auto&r=*static_cast<Review*>(p);r.gait.Advance(distance,dt,true,false);return r.rig.Raster(r.gait.Targets(),width,height);}
const std::uint32_t* articulated_pixels(void* p){return static_cast<Review*>(p)->rig.Pixels();}
int articulated_width(void*p){return static_cast<Review*>(p)->rig.Width();}
int articulated_height(void*p){return static_cast<Review*>(p)->rig.Height();}
double articulated_reach_error(void*p){return static_cast<Review*>(p)->rig.ReachError();}
void articulated_joint(void*p,int leg,double* o){const auto&r=*static_cast<Review*>(p);const auto&s=r.rig.Legs()[leg];o[0]=s.root.x;o[1]=s.root.y;o[2]=s.knee.x;o[3]=s.knee.y;o[4]=s.ankle.x;o[5]=s.ankle.y;o[6]=s.paw.x;o[7]=s.paw.y;o[8]=s.targetPaw.x;o[9]=s.targetPaw.y;o[10]=r.gait.Targets()[leg].stance;o[11]=s.clamped; }
}
