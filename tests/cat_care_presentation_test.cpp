#include "cat_care_presentation.hpp"
#include <array>
#include <cassert>
#include <cmath>
#include <iostream>

int main() {
    using namespace cat::care_presentation;
    const Tag idle{0,0,0,true,false,false,true},groom{1,0,0,true,true,false,true};
    Transition primary,companion;
    assert(!primary.Update(idle,1000).blend);
    auto start=primary.Update(groom,1016);assert(start.capture&&start.blend&&start.weight==0);
    auto mid=primary.Update(groom,1126);assert(mid.blend&&std::abs(mid.weight-.5)<1e-12);
    assert(!primary.Update(groom,1236).blend);
    // A different authored frame is deliberately the same representation.
    assert(!primary.Update(groom,1250).capture);
    // A different owner can use the same neutral drawing, but must still join
    // smoothly when its joints/expression differ. Source-frame advances inside
    // that owner are never retimed.
    auto nextAction=groom;nextAction.action=1;
    assert(primary.Update(nextAction,1251).capture);
    assert(!primary.Update(nextAction,1252).capture);
    primary.Reset();primary.Update(groom,1250);
    auto bridge=primary.Update(idle,1260);assert(bridge.capture&&bridge.bridge&&bridge.weight==0);
    assert(primary.Update(idle,1480).bridge); // Fully drawn neutral bridge.
    auto exit=primary.Update(idle,1496);assert(exit.capture&&exit.blend&&!exit.bridge&&exit.weight==0);
    assert(!primary.Update(idle,1716).blend);
    primary.Reset();primary.Update(idle,2000);primary.Update(groom,2016);
    auto beforePause=primary.Update(groom,2071);primary.Pause(9000);
    auto afterPause=primary.Update(groom,11071);assert(beforePause.weight==afterPause.weight);
    auto paused=primary.Update(groom,16071,true);assert(paused.weight==afterPause.weight);
    // Hide gaps and independent actor clocks do not advance the other cat.
    auto hidden=primary.Update(groom,21071);assert(hidden.weight==paused.weight);
    companion.Update(groom,2);assert(!companion.Update(groom,200).blend);
    assert(primary.Update(groom,21087).blend);
    for(unsigned change=0;change<5;++change) {
        primary.Reset();primary.Update(idle,0);auto target=groom;
        if(change==0)target.identity=1;
        if(change==1)target.generation=1;
        if(change==2)target.flip=true;
        if(change==3)target.eligible=false; // Gait, flight and physical contacts.
        if(change==4)primary.Reset();
        assert(!primary.Update(target,16).blend);
    }
    primary.Reset();primary.Update(idle,1000);primary.Update(groom,1016);
    assert(!primary.Update(groom,900).blend); // Clock rollback resets history.
    std::array<unsigned char,8> from{{30,80,100,255,0,0,0,0}},to{{80,30,120,255,20,40,60,100}},out{};
    BlendPremultiplied(from.data(),to.data(),out.data(),out.size(),.5);
    assert(out[3]==255&&out[7]==50);
    for(unsigned pixel=0;pixel<2;++pixel)for(unsigned c=0;c<3;++c)assert(out[pixel*4+c]<=out[pixel*4+3]);
    std::cout<<"Care presentation: anchored boundaries, neutral bridge, no intra-clip blend, pause/reset/actor isolation and premultiplied opacity passed\n";
}
