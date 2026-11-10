#include "../src/cat_idle_proxy.hpp"
#include <iostream>
#include <limits>
#include <stdexcept>

int main() {
    unsigned checks=0;
    const auto check=[&](bool value){++checks;if(!value)throw std::runtime_error("idle proxy plan check "+std::to_string(checks));};
    try {
        const auto ordinary=cat::idle_proxy::Plan(329,367,110);
        check(ordinary.width==151&&ordinary.height==168);
        // A small growth change reuses the same bucket. Real monitor DPI
        // changes select a new resolution without changing source geometry.
        for(double height=108;height<112;height+=.01) {
            const auto plan=cat::idle_proxy::Plan(329,367,height);
            check(plan.width==ordinary.width&&plan.height==ordinary.height);
        }
        const auto large=cat::idle_proxy::Plan(329,367,165);
        check(large.height==248&&large.width==222);
        for(int sourceWidth:{1,16,329,510})for(int sourceHeight:{1,27,367,512})for(int visible=1;visible<1024;visible+=7) {
            const auto plan=cat::idle_proxy::Plan(sourceWidth,sourceHeight,visible);
            check(plan.width>=1&&plan.width<=sourceWidth&&plan.height>=1&&plan.height<=sourceHeight);
            check(plan.width==1||std::abs(plan.width-static_cast<double>(sourceWidth)*plan.height/sourceHeight)<=.500001);
            // The reference/destination size is deliberately not a result of
            // this planner; it only chooses a bounded sampling texture.
            if(visible*1.5>=sourceHeight)check(plan.height==sourceHeight&&plan.width==sourceWidth);
        }
        check(cat::idle_proxy::Plan(329,367,0).width==0);
        check(cat::idle_proxy::Plan(0,367,110).width==0);
        check(cat::idle_proxy::Plan(329,0,110).width==0);
        check(cat::idle_proxy::Plan(329,367,std::numeric_limits<double>::quiet_NaN()).width==0);
        std::cout<<"Idle proxy: "<<checks<<" checks passed\n";return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
