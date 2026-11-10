#include "../src/cat_video_ground_bridge.hpp"
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>
#include <type_traits>

namespace {
int checks=0;
#define CHECK(value) do {++checks;if(!(value))throw std::runtime_error(std::string(__FILE__)+":"+std::to_string(__LINE__)+" " #value);}while(false)
using Bridge=cat::video_ground_bridge::GroundBridge;
cat::sprite_assets::Descriptor Layout() {
    using namespace cat::sprite_sequence;
    cat::sprite_assets::Descriptor d;
    d.clip=Clip::Walk;d.resourceId=400;d.columns=d.rows=8;d.frameCount=61;
    d.imageWidth=d.imageHeight=128;d.referenceHeight=16;d.stridePixels=68;
    d.standingFrame=20;d.nominalFramesPerSecond=24;d.movementDelayMs=500;
    for(unsigned i=0;i<d.frameCount;++i)d.frames[i]={{static_cast<int>((i%8)*16),static_cast<int>((i/8)*16),16,16},{8,14}};
    auto exposure=[](unsigned i){return ((i+1)*1000+12)/24-(i*1000+12)/24;};
    d.program.stepCount=61;d.program.intro={0,28};d.program.loop={28,20};d.program.outro={48,13};
    for(unsigned i=0;i<28;++i)d.program.steps[i]={20+i,exposure(i)};
    for(unsigned i=0;i<20;++i)d.program.steps[28+i]={i,1000};
    for(unsigned i=0;i<13;++i)d.program.steps[48+i]={48+i,exposure(i)};
    CHECK(cat::sprite_assets::Valid(d));
    return d;
}
void ActualEntranceAndMotorHaveSeparateClocks() {
    const auto d=Layout();Bridge b;
    CHECK(b.Configure(1167,542,d.movementDelayMs));
    CHECK(b.Update(1000,true,true,0));CHECK(b.Starting()&&b.HoldMotor());
    CHECK(b.Sample(d).valid&&b.Sample(d).frame==20);
    b.Update(1499,true,true,0);CHECK(b.Starting()&&b.HoldMotor());
    b.Update(1500,true,true,0);CHECK(b.Starting()&&!b.HoldMotor());
    CHECK(b.Sample(d).frame==32);
    b.Update(1600,true,true,40);CHECK(b.Starting()&&!b.HoldMotor());
    b.Update(2166,true,true,81.6);CHECK(b.Starting()&&b.Sample(d).frame==47);
    b.Update(2167,true,true,81.6);CHECK(b.Side()&&!b.Starting());
    CHECK(!b.Sample(d).valid);CHECK(b.ConsumeLoopReady());CHECK(!b.ConsumeLoopReady());
    b.Update(2167,true,true,81.6);CHECK(!b.ConsumeLoopReady());
}
void StopWaitsForContactThenFinishesTheActualExit() {
    const auto d=Layout();Bridge b;b.Configure(1167,542,500);
    b.Update(0,true,true,22);CHECK(b.Side());
    b.Update(100,false,true,22);CHECK(b.Braking()&&b.Stopping()&&b.HoldMotor());
    CHECK(!b.Sample(d).valid);
    b.Update(200,false,true,.4);CHECK(!b.Braking()&&b.Stopping());CHECK(b.Sample(d).frame==48);
    b.Update(741,false,true,0);CHECK(b.Stopping()&&b.Sample(d).frame==60);
    b.Update(742,false,true,0);CHECK(b.Neutral()&&!b.Stopping());
    CHECK(b.Sample(d).valid&&b.Sample(d).frame==60&&b.Sample(d).holding);
    b.Update(9000,false,true,0);CHECK(b.Neutral()&&b.Sample(d).frame==60);
}
void ResetWithGroundMomentumNeverShowsTheFrontPose() {
    const auto d=Layout();Bridge b;b.Configure(1167,542,500);
    // Busy cursor states reset the bridge, but not the physical body's speed.
    // Watch/Idle must brake with distance-driven paws before the real exit.
    for(const double speed:{32.,-32.}) {
        b.Reset(900);CHECK(b.Sample(d).valid&&b.Sample(d).frame==20);
        CHECK(b.Update(1000,false,true,speed));
        CHECK(b.Braking()&&b.Stopping()&&!b.Starting()&&b.HoldMotor());
        CHECK(!b.Sample(d).valid&&!b.ConsumeLoopReady());
        b.Update(1100,false,true,speed/2);CHECK(b.Braking()&&!b.Sample(d).valid);
        b.Update(1200,false,true,.5);CHECK(b.Stopping()&&!b.Braking());
        CHECK(b.Sample(d).valid&&b.Sample(d).frame==48);
        b.Update(1741,false,true,0);CHECK(b.Stopping()&&b.Sample(d).frame==60);
        b.Update(1742,false,true,0);CHECK(b.Neutral()&&b.Sample(d).frame==60);
    }
    // Existing momentum also bypasses the stationary entrance when travel
    // resumes, even when a host temporarily disallows a new entrance.
    b.Reset();b.Update(2000,true,true,24,false);
    CHECK(b.Side()&&!b.Starting()&&!b.HoldMotor()&&!b.Sample(d).valid);
    CHECK(!b.ConsumeLoopReady());
    // A request can also end during the first moving steps of the entrance.
    b.Reset();b.Update(3000,true,true,0);b.Update(3600,true,true,24);
    CHECK(b.Starting()&&!b.HoldMotor());
    b.Update(3617,false,true,24);
    CHECK(b.Braking()&&!b.Sample(d).valid&&!b.ConsumeLoopReady());
    b.Update(3700,false,true,0);CHECK(b.Stopping()&&b.Sample(d).frame==48);
    b.Update(4242,false,true,0);CHECK(b.Neutral()&&b.Sample(d).frame==60);
}
void ReversalAndSideCareDoNotRepeatTheFrontTurn() {
    Bridge b;b.Configure(1167,542,500);b.Update(0,true,true,22);
    for(unsigned i=0;i<10;++i)b.Update(100+i*20,true,true,i<5?0:-22);
    CHECK(b.Side()&&!b.Starting()&&!b.Stopping()&&!b.ConsumeLoopReady());
    b.Update(400,false,true,20,true,true);CHECK(b.Side()&&b.HoldMotor()&&!b.Stopping());
    b.Update(600,false,true,0,true,true);CHECK(b.Side());
    b.Update(700,true,true,0);CHECK(b.Side()&&!b.HoldMotor()&&!b.Starting());
    b.Reset();b.Update(0,false,true,0,true,true);
    CHECK(b.StartingForSideAction()&&b.HoldMotor());
    b.Update(499,false,true,0,true,true);CHECK(b.Starting());
    b.Update(500,false,true,0,true,true);CHECK(b.Side()&&!b.ConsumeLoopReady());
    b.Update(1000,false,true,0);CHECK(b.Stopping());
    b.Update(1542,false,true,0);CHECK(b.Neutral());
}
void SuspensionInterruptionAndNoAirborneGroundBridge() {
    Bridge b;b.Configure(1167,542,500);b.Update(1000,true,true,0);b.Update(1200,true,true,0);
    b.Freeze(60000);b.Update(61200,true,true,0);CHECK(b.Elapsed()==200&&b.Starting()&&b.HoldMotor());
    b.Update(62167,true,true,81.6);CHECK(b.Side()&&b.ConsumeLoopReady());
    b.Update(62200,false,false,40);CHECK(b.Neutral()&&!b.Starting()&&!b.Stopping());
    b.Update(62300,true,true,40);CHECK(b.Side()&&!b.Starting());
    b.Reset();b.Update(0,true,true,0,false);CHECK(b.Neutral());
    b.Update(100,true,true,0);CHECK(b.Starting());
    b.Update(200,false,true,0);CHECK(b.Neutral()&&!b.ConsumeLoopReady());
    CHECK(!b.Configure(100,50,101));
    b.Update(1000,true,true,0);b.Update(1500,true,true,0);CHECK(b.Starting()&&!b.HoldMotor());
}
void RebasedHostClockRetainsTheWholeEntrance() {
    const auto d=Layout();Bridge b;b.Configure(1167,542,500);
    b.Update(100000,true,true,0);b.Update(100200,true,true,0);
    CHECK(b.Elapsed()==200&&b.Sample(d).frame==24);
    b.RebaseClock(50);CHECK(b.Elapsed()==200&&b.Sample(d).frame==24);
    b.Update(349,true,true,0);CHECK(b.Elapsed()==499&&b.HoldMotor());
    b.Update(350,true,true,0);CHECK(b.Elapsed()==500&&!b.HoldMotor()&&b.Starting());
    b.RebaseClock(800000);CHECK(b.Elapsed()==500);
    b.Update(800667,true,true,81.6);CHECK(b.Side()&&b.ConsumeLoopReady());
    b.Reset();b.Update(100000,false,true,0,true,true);b.Update(100100,false,true,0,true,true);
    b.RebaseClock(0);CHECK(b.Elapsed()==100&&b.StartingForSideAction());
    b.Update(399,false,true,0,true,true);CHECK(b.Starting()&&b.Elapsed()==499);
    b.Update(400,false,true,0,true,true);CHECK(b.Side()&&!b.ConsumeLoopReady());
}
}
int main() {
    try {
        static_assert(std::is_trivially_copyable<Bridge>::value,"No owned allocations in a ground bridge");
        ActualEntranceAndMotorHaveSeparateClocks();StopWaitsForContactThenFinishesTheActualExit();
        ResetWithGroundMomentumNeverShowsTheFrontPose();
        ReversalAndSideCareDoNotRepeatTheFrontTurn();SuspensionInterruptionAndNoAirborneGroundBridge();
        RebasedHostClockRetainsTheWholeEntrance();
        std::cout<<"Video ground bridge: "<<checks<<" checks passed\n";return EXIT_SUCCESS;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return EXIT_FAILURE;}
}
