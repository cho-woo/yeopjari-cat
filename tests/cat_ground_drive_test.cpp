#include "../src/cat_motion_gate.hpp"
#include "../src/cat_physics.hpp"
#include "../src/care_sprites.generated.hpp"
#include <iostream>
#include <stdexcept>
#define CHECK(x) do {if(!(x))throw std::runtime_error(std::to_string(__LINE__)+": " #x);}while(false)
using cat::animation::Motion;

// Reproduce the reported 4.45–4.55s gap at real Box2D/render sample rates.
// The legacy predicate is observed, not used to manufacture an expected pose:
// Evaluate(Idle) would have selected front neutral before Standing() was ready.
void StopPresentationRegression() {
 for(double hz:{30.,60.,120.})for(int stopCase:{0,1,2}) {
  cat::physics::Controller physics;physics.Reset(400,96,1800);
  cat::motion::GroundDrive drive;cat::animation::Locomotion gait;
  const auto* walk=cat::sprite_assets::Find(cat::sprite_sequence::Clip::Walk,0,cat::sprite_assets::kCareSprites);
  gait.ConfigureAuthoredStrides(walk->stridePixels,150,walk->standingFrame);
  Motion requested=Motion::Walk;bool waiting=false,requestedStop=false,sawLegacyGap=false,sawWait=false;
  std::uint64_t started=0,deadline=5000,firstLegacyGap=0,lastLegacyGap=0;
  for(unsigned i=0;i<hz*5.1;++i) {
   const double time=i/hz,dt=1/hz;const auto ticks=static_cast<std::uint64_t>(std::llround(time*1000));
   const auto a=physics.CurrentPose(),before=physics.RenderPose();
   const bool lateReady=std::abs(a.velocityX)<=.5 && !drive.Settled();
   if(!requestedStop && time>=4 && (stopCase!=2 || lateReady)) {
    requestedStop=true;requested=Motion::Idle;started=ticks;deadline=ticks+5000;
    if(stopCase!=0)waiting=cat::motion::BeginGroundStop(requested,a.grounded,a.velocityX,before.velocityX,drive);
    if(stopCase==2) {
     CHECK(!cat::motion::NeedsStop(requested,a.grounded,a.velocityX));
     CHECK(waiting); // The old physical-speed-only entry missed this case.
    }
   }
   cat::motion::AdvanceGroundStop(waiting,a.grounded,a.velocityX,drive,ticks,started,deadline);
   if(waiting){CHECK(started==ticks);CHECK(deadline-started==5000);sawWait=true;}
   const double desired=time<3?22:time<4?42:0;
   physics.SetTargetVelocity(drive.Update(waiting?0:desired,a.velocityX,before.velocityX,a.grounded,dt));
   physics.Update(dt);const auto b=physics.RenderPose();
   const auto displayed=cat::motion::DisplayedMotion(requested,Motion::Walk,b.grounded,waiting,b.velocityX);
   CHECK(gait.Advance(displayed,b.relativeTravelX-before.relativeTravelX,dt,b.grounded));
   auto pose=cat::animation::Evaluate(displayed,ticks>=started?ticks-started:0,ticks,drive.Facing(),gait);
   if(displayed==Motion::Idle && gait.RecentAuthoredWalk() && !gait.AuthoredStanding()) {
    sawLegacyGap=true;if(!firstLegacyGap)firstLegacyGap=ticks;lastLegacyGap=ticks;
   }
   cat::animation::ApplyAuthoredLocomotion(pose,displayed,gait,true);
   if(time>=4 && time<4.9) {
    CHECK(pose.spriteClip==cat::sprite_sequence::Clip::Walk);
    CHECK(pose.blendFrame<0);
   }
  }
  CHECK(requestedStop);
  if(stopCase==0) {
   CHECK(sawLegacyGap); // The old diagnostic did reach the bad interval.
   if(hz==60)std::cout<<"Legacy no-wait front-pose interval reproduced: "<<firstLegacyGap<<".."<<lastLegacyGap
       <<"ms; fixed presentation remains side-view\n";
  }
  else CHECK(sawWait);
 }
 cat::motion::GroundDrive drive;drive.Update(22,22,22,true,1./60);
 CHECK(cat::motion::BeginGroundStop(Motion::Idle,true,.4,.6,drive));
 CHECK(!cat::motion::BeginGroundStop(Motion::Walk,true,.4,.6,drive));
 CHECK(!cat::motion::BeginGroundStop(Motion::Idle,false,.4,.6,drive));
 CHECK(!cat::motion::BeginGroundStop(Motion::Idle,true,.4,.6,drive,true));
}

int main(){
 StopPresentationRegression();
 for(double hz:{30.,60.,120.}) {
  cat::physics::Controller p;p.Reset(400,96,1800);
  cat::motion::GroundDrive drive;cat::animation::Locomotion gait;
  const auto* walk=cat::sprite_assets::Find(cat::sprite_sequence::Clip::Walk,0,cat::sprite_assets::kCareSprites);
  gait.ConfigureAuthoredStrides(walk->stridePixels,150,walk->standingFrame);
  int flips=0,prior=1;double held=0;bool stood=false,movedLeft=false;
  for(int i=0;i<hz*10;++i){
   const double time=i/hz,dt=1/hz;
   const double requested=time<.5?0:time<3?22:time<4?42:time<5?0:time<7?-28:time<9?28:0;
   const auto a=p.CurrentPose(),before=p.RenderPose();
   const double target=drive.Update(requested,a.velocityX,before.velocityX,a.grounded,dt);
   if(drive.Facing()!=prior){CHECK(std::abs(a.velocityX)<=.5);CHECK(std::abs(before.velocityX)<=.5);CHECK(held>=.15);++flips;prior=drive.Facing();held=0;}
   if(std::abs(a.velocityX)<=.5 && target==0)held+=dt;else if(target!=0)held=0;
   p.SetTargetVelocity(target);p.Update(dt);const auto after=p.RenderPose();
   const auto motion=cat::motion::DisplayedMotion(requested==0?Motion::Idle:Motion::Walk,Motion::Walk,after.grounded,false,after.velocityX);
   CHECK(gait.Advance(motion,after.relativeTravelX-before.relativeTravelX,dt,after.grounded));
   auto pose=cat::animation::Evaluate(motion,0,1000,drive.Facing(),gait);
   cat::animation::ApplyAuthoredLocomotion(pose,motion,gait,true);
   CHECK(pose.blendFrame<0);CHECK(pose.scaleX==1&&pose.scaleY==1&&pose.x==0&&pose.lift==0);
   if(time>4.7&&time<4.9){CHECK(pose.spriteClip==cat::sprite_sequence::Clip::Walk);CHECK(pose.spriteFrame==walk->standingFrame);stood=true;}
   if(time>6 && after.velocityX<-20)movedLeft=true;
  }
  CHECK(flips==2&&stood&&movedLeft);
  // One coarse tick can span the final airborne substep and a landing. Only
  // contact-to-contact travel can be consumed, never the preceding flight.
  p.Reset(400,96,1800);CHECK(p.Pounce(1,60,150));p.SetTargetVelocity(150);
  bool landed=false;double lastTravel=p.CurrentPose().relativeTravelX;
  for(int i=0;i<120;++i){p.Update(1./60);const auto a=p.CurrentPose();
   if(!a.grounded)CHECK(a.relativeTravelX==lastTravel);
   if(a.landed){CHECK(a.relativeTravelX==lastTravel);landed=true;break;}
   lastTravel=a.relativeTravelX;
  }CHECK(landed);
  const auto before=p.RenderPose();p.Update(30);CHECK(p.RenderPose().x==before.x);CHECK(p.RenderPose().relativeTravelX==before.relativeTravelX);
  p.RelocateBounds(96,1800,100);CHECK(p.RenderPose().relativeTravelX==before.relativeTravelX);
  drive.Reset();CHECK(drive.Update(-30,0,0,true,.016)==0);CHECK(drive.Facing()==-1);
  for(int i=0;i<6;++i){CHECK(drive.Update(i%2?30:-30,0,0,true,.016)==0);CHECK(drive.Facing()==-1);}
  drive.Reset();drive.Update(-30,30,30,true,.016);const int facing=drive.Facing();
  for(int i=0;i<100;++i)drive.Update(-30,0,0,true,0);
  CHECK(drive.Facing()==facing); // suspended clock
 }
 std::cout<<"Ground drive: actual-physics start/speed/brake/reversal at 30/60/120Hz, planted holds, airborne exclusion, stall and relocation passed\n";
}
