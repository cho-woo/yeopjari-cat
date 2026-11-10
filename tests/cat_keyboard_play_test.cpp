#include "cat_keyboard_play.hpp"
#include <cassert>
#include <iostream>
using namespace cat::keyboard_play;
int main(){
    Input input;
    assert(!input.Active()&&!input.Event(Left,true).consume);
    input.Start();assert(input.Active()&&input.Direction()==0);
    assert(!input.Event(Left,false).consume); // Opt-in never steals a prior key-up.
    assert(input.Event(Right,true).consume&&input.Direction()==1);
    input.Event('A',true);assert(input.Direction()==0);
    input.Event('A',false);assert(input.Direction()==1);
    input.Event(Shift,true);assert(input.Running());
    assert(input.Event(Space,true).jump&&input.TakeJump());
    for(int repeat=0;repeat<100;++repeat)assert(!input.Event(Space,true).jump&&!input.TakeJump());
    input.Event(Space,false);assert(input.Event(Space,true).jump&&input.TakeJump());
    assert(input.Event('X',true).transform&&input.TakeTransform());
    assert(!input.Event('X',true).transform&&!input.TakeTransform());
    assert(!input.Event('Q',true).consume); // Unmapped typing remains desktop-owned.
    assert(input.Event(Left,true,true).exit&&!input.Active());
    assert(!input.Running()&&input.Direction()==0&&!input.TakeJump()&&!input.TakeTransform());
    input.Start();input.Event(Right,true);
    const auto escape=input.Event(Escape,true);
    assert(escape.consume&&escape.exit&&!input.Active()&&input.Direction()==0);
    assert(!input.Event(Right,false).consume);
    Safety safe;safe.initialized=safe.groundReady=true;assert(safe.Allowed());
    for(unsigned state=0;state<8;++state){
        auto blocked=safe;
        switch(state){case 0:blocked.hidden=true;break;case 1:blocked.menu=true;break;
        case 2:blocked.drag=true;break;case 3:blocked.closing=true;break;
        case 4:blocked.updating=true;break;case 5:blocked.foregroundChanged=true;break;
        case 6:blocked.owned=false;break;default:blocked.groundReady=false;break;}
        assert(!blocked.Allowed());
    }
    assert(RunBoost>1&&RunBoost<1.5&&JumpBoost>1&&JumpBoost<1.5);
    for(std::uint64_t t=0;t<100000;t+=17){assert(Charge(t)>=0&&Charge(t)<=1);assert(Pulse(t)>=.67&&Pulse(t)<=1.01);}
    std::cout<<"Keyboard opt-in, edge/repeat, modifier/escape, key release and ownership policy passed\n";
}
