#include "cat_running_turn.hpp"
#include <cstdlib>
#include <iostream>
#include <stdexcept>
using Turn=cat::running_turn::Turn;
int main(){try{
    unsigned checks=0;
    const auto check=[&](bool value){++checks;if(!value)throw std::runtime_error("running turn contract");};
    for(int direction:{-1,1})for(int fps:{30,60,120}){
        Turn turn;check(turn.Begin(direction,-direction));check(!turn.Begin(-direction,direction));
        for(int i=0;i<fps*2;++i){turn.Advance(1./fps,true,direction*50,direction*50);check(!turn.Released());check(turn.Motor(180)==0);}
        check(turn.SpriteMs()==Turn::BrakeMs); // Full pivot waits for both physical samples.
        turn.Advance(1./fps,true,0,1);check(!turn.Released());
        turn.Advance(1./fps,true,0,0);check(turn.CurrentPhase()==Turn::Phase::Pivot);
        unsigned releases=0;double elapsed=0,prior=0;
        while(turn.Active()&&elapsed<3){
            releases+=turn.Advance(1./fps,true,0,0);elapsed+=1./fps;
            if(turn.Active()&&turn.Released()){
                const auto motor=turn.Motor(180);check(motor*(-direction)>=prior-1e-8);check(std::abs(motor)<=180);prior=motor*(-direction);
            } else if(turn.Active())check(turn.Motor(180)==0);
        }
        check(!turn.Active()&&releases==1);check(std::abs(elapsed-(Turn::PivotMs+Turn::LaunchMs)/1000.)<2./fps);
        turn.Begin(direction,-direction);const auto frozen=turn.SpriteMs();turn.Advance(0,true,0,0);check(turn.SpriteMs()==frozen);
        turn.Advance(.5,true,0,0);check(turn.SpriteMs()==frozen);turn.Advance(.01,false,0,0);check(!turn.Active());
    }
    std::cout<<"Running turn: "<<checks<<" checks passed\n";return EXIT_SUCCESS;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return EXIT_FAILURE;}}
