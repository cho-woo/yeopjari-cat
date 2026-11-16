#include "cat_furniture.hpp"
#include "cat_environment.hpp"
#include <cassert>
#include <iostream>
int main() {
    using namespace cat::furniture;
    for(unsigned coat:{0u,8u,10u})for(auto kind:{Kind::Tower,Kind::Scratcher,Kind::Box}) {
        Session s;assert(s.Request(kind));assert(!s.Request(Kind::Box));
        assert(!s.Owned());assert(s.Begin(850,96,864,1,coat));
        assert(s.State()==Phase::Approach&&s.Velocity(850,40)<0);
        double x=850,v=0;bool decoded=false,side=false;
        for(int i=0;i<2500&&s.State()!=Phase::Settle;++i) {
            v=s.Velocity(x,40);x+=v/60;s.Advance(1./60,x,v,side,decoded);
        }
        assert(s.State()==Phase::Settle&&x<736&&x>=96);
        s.Advance(.1,x,0,true,false);assert(!s.Playing());
        s.Advance(.1,x,0,false,true);assert(!s.Playing());
        s.Advance(.1,x,0,true,true);assert(s.Playing());
        const auto duration=s.Selected()->durationMs;
        for(std::uint64_t ms=0;ms+10<duration;ms+=10) {
            assert(s.Playing()&&s.Velocity(x,40)==0);s.Advance(.01,x,0,true,true);
        }
        const auto before=s.Milliseconds();s.Advance(0,x,0,true,true);s.Advance(1,x,0,true,true);
        assert(s.Milliseconds()==before); // pause and long scheduler stalls
        while(s.Playing())s.Advance(.01,x,0,true,true);
        assert(s.State()==Phase::Recover&&s.Owned()&&!s.Request(Kind::Tower));
        bool done=false;for(int i=0;i<80&&!done;++i)done=s.Advance(.01,x,0,true,true);
        assert(done&&!s.Requested());
    }
    Session small;assert(small.Request(Kind::Tower));assert(!small.Begin(100,96,110,1.2,0));
    RandomSchedule random;
    for(int i=0;i<10000;++i)assert(!random.Advance(.1,false,0));
    int events=0;for(int i=0;i<1000;++i)events+=random.Advance(.1,true,0);
    assert(events==1);random.Completed(0);
    for(int i=0;i<1700;++i)assert(!random.Advance(.1,true,0));
    RandomSchedule overdue;
    for(int i=0;i<1000;++i)assert(!overdue.Advance(.1,true,0,false));
    assert(overdue.Remaining()==0);
    for(int i=0;i<500;++i)assert(!overdue.Advance(.1,true,1234,false));
    assert(overdue.Remaining()==0); // Due work is retained, never re-rolled or rushed.
    assert(overdue.Advance(.016,true,1234,true));
    assert(overdue.Remaining()>=180000);
    // A menu command must not starve behind an indefinitely repeated pointer
    // game. Drain a real authored paw, preserving its complete source clock.
    using A=cat::environment::CursorAction;
    cat::environment::CursorReaction pointer;
    cat::environment::CursorInput input;
    input.available=input.enabled=input.canStart=input.canContinue=true;
    input.repeatNearby=input.pursuePointer=input.pawEndsPlanted=true;
    input.catX=400;input.cursorX=425;input.cursorHeight=12;input.maxX=1200;
    input.pawDurationMs=input.punchDurationMs=4458;
    for(int i=0;i<100&&!((pointer.Action()==A::Paw)||(pointer.Action()==A::Punch));++i) {
        input.nowMs+=16;pointer.Step(input);
    }
    assert(pointer.Action()==A::Paw||pointer.Action()==A::Punch);
    const auto started=input.nowMs;const auto attack=pointer.Action();
    input.finishAfterRecovery=true;input.canStart=false;
    while(input.nowMs-started+16<input.pawDurationMs) {
        input.nowMs+=16;pointer.Step(input);assert(pointer.Action()==attack);
    }
    for(int i=0;i<10&&pointer.Active();++i){input.nowMs+=16;pointer.Step(input);}
    assert(!pointer.Active());
    for(int i=0;i<1000;++i){input.nowMs+=16;pointer.Step(input);assert(!pointer.Active());}
    std::cout<<"Furniture: 9 complete timelines, safe approach, decode gate, pause, recovery and random spacing passed\n";
}
