// Isolated renderer diagnosis. This exposes internals only in this executable;
// it never changes application source or uploaded artwork.
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>
#include <iostream>
#include <fstream>
#include <string>
static bool traceActive=false;
[[maybe_unused]] static void TraceFootSample(double u,double v,unsigned sa,unsigned before,unsigned after){if(traceActive)std::cout<<"fragment source="<<u<<","<<v<<" alpha="<<sa<<" before="<<before<<" after="<<after<<"\n";}
#define private public
#ifdef TRACE_FOOT_BOUNDS
#include "cat_articulated_walk_trace.hpp"
#else
#include "cat_articulated_walk.hpp"
#endif
#undef private
struct Worst {double overshoot=-1;int frame=0,mode=0,leg=0,x=0,y=0;double alpha=0,phase=0,feetx=0,feety=0,bw=0,bh=0; bool clamped=false;double modelPawY=0,targetPawY=0;};
int main(int argc,char**argv){
 if(argc<5)return 1;
 std::ifstream input(argv[1],std::ios::binary);int w=std::stoi(argv[2]),h=std::stoi(argv[3]);double reference=std::stod(argv[4]);
 std::vector<std::uint32_t> source(w*h);input.read(reinterpret_cast<char*>(source.data()),source.size()*4);if(!input)return 2;
 cat::articulated::Rig rig;if(!rig.Prepare(source.data(),w,h,w*4))return 3;Worst worst;
 for(int i=0;i<2;++i){
  std::cout<<"source_mask "<<i<<" sole="<<rig.soles_[i].x<<","<<rig.soles_[i].y;
  for(unsigned threshold:{1u,32u,64u,128u,153u,230u}){int bottom=0;for(int y=0;y<h;++y)for(int x=0;x<w;++x)if((rig.leg_[i].pixels[y*w+x]>>24)>=threshold)bottom=std::max(bottom,y+1);std::cout<<" alpha"<<threshold<<"_bottom="<<bottom;}std::cout<<"\n";
 }
 for(auto dimensions:{std::array<double,2>{.78,.96},std::array<double,2>{1,1},std::array<double,2>{1.26,.98}})
 for(int mode:{0,2}){
  const auto selected=mode==2?cat::walk_dynamics::Mode::Gallop:cat::walk_dynamics::Mode::Walk;
  cat::gait::Cycle cycle(cat::walk_dynamics::ParametersFor(selected),false,0);double width=w*reference*dimensions[0],height=h*reference*dimensions[1],speed=mode==2?165:22;
  Worst caseWorst;double maxGlobal=-1000;int globalFrame=0,globalLeg=0,globalY=0;
  for(int frame=0;frame<120;++frame){
   cycle.Advance(speed/60,1./60,true,false);auto feet=cycle.Targets();for(int i=0;i<4;++i)feet[i].x+=cat::walk_dynamics::PawBias(selected,i);
   rig.dynamics_=cat::walk_dynamics::Evaluate(cycle.Phase(),cycle.Speed(),selected,&feet);
   for(int leg=0;leg<4;++leg){
    traceActive=(mode==0&&dimensions[0]==.78&&frame==100&&leg==1);
    std::fill(rig.out_.begin(),rig.out_.end(),0);if(!rig.DrawLeg(leg,feet[leg],width,height))return 4;
    for(int y=h-15;y<rig.Height();++y)for(int x=0;x<rig.Width();++x){unsigned alpha=rig.out_[y*rig.Width()+x]>>24;if(alpha<153)continue;
     double global=(y+1-(h-2))*height/h;
     if(global>maxGlobal){maxGlobal=global;globalFrame=frame;globalLeg=leg;globalY=y;}
     double over=(y+1-(h-2+feet[leg].y/height*h))*height/h;
     if(over>caseWorst.overshoot)caseWorst={over,frame,mode,leg,x,y,static_cast<double>(alpha),cycle.Phase(),feet[leg].x,feet[leg].y,dimensions[0],dimensions[1],rig.solutions_[leg].clamped,rig.solutions_[leg].paw.y,rig.solutions_[leg].targetPaw.y};
    }
   }
  }
  if(caseWorst.overshoot>worst.overshoot)worst=caseWorst;
  std::cout<<"case mode="<<mode<<" body="<<dimensions[0]<<","<<dimensions[1]<<" overshoot="<<caseWorst.overshoot<<" frame="<<caseWorst.frame<<" leg="<<caseWorst.leg<<" pixel="<<caseWorst.x<<","<<caseWorst.y<<" alpha="<<caseWorst.alpha<<" phase="<<caseWorst.phase<<" feet="<<caseWorst.feetx<<","<<caseWorst.feety<<" modelPawY="<<caseWorst.modelPawY<<" targetPawY="<<caseWorst.targetPawY<<" clamped="<<caseWorst.clamped<<" globalBelowFloor="<<maxGlobal<<" globalFrame="<<globalFrame<<" globalLeg="<<globalLeg<<" globalY="<<globalY<<"\n";
 }
 std::cout<<"WORST "<<worst.overshoot<<" frame="<<worst.frame<<" mode="<<worst.mode<<" leg="<<worst.leg<<" body="<<worst.bw<<","<<worst.bh<<" pixel="<<worst.x<<","<<worst.y<<"\n";
}
