#include "cat_random_zoomies.hpp"
#include <cstdlib>
#include <iostream>
#include <random>
#include <stdexcept>

namespace {
unsigned checks=0;
#define CHECK(x) do {++checks;if(!(x))throw std::runtime_error(#x);} while(false)
using Scheduler=cat::random_zoomies::Scheduler;
const auto minimum=[](std::uint64_t low,std::uint64_t){return low;};
void QuietGapAndOneDeferredOpportunity() {
    Scheduler s;unsigned draws=0;
    const auto draw=[&](std::uint64_t low,std::uint64_t){++draws;return low;};
    CHECK(!s.Update(0,true,false,false,false,draw).started&&s.WaitMs()==120000&&draws==1);
    for(unsigned i=0;i<4800;++i)CHECK(!s.Update(25,true,false,false,false,draw).started);
    CHECK(s.Due()&&draws==1);
    for(unsigned i=0;i<10000;++i)CHECK(!s.Update(25,true,false,false,false,draw).started);
    CHECK(s.Due()&&draws==1); // Hours of busy time cannot queue multiple runs.
    const auto started=s.Update(25,true,true,false,false,draw);
    CHECK(started.started&&started.runDurationMs==5000&&s.Active()&&draws==3);
    CHECK(!s.Update(25,true,true,false,true,draw).started);
}
void RunningBudgetExcludesSafeTransitions() {
    Scheduler s;s.Update(0,true,false,false,false,minimum);
    for(unsigned i=0;i<480;++i)s.Update(250,true,true,false,false,minimum);
    CHECK(s.Active()&&s.RemainingRunMs()==5000);
    for(unsigned i=0;i<40;++i)CHECK(!s.Update(25,true,false,false,true,minimum).finished);
    CHECK(s.RemainingRunMs()==5000); // The real turn/crouch holds do not spend running time.
    unsigned turns=0;bool ended=false;
    for(unsigned i=0;i<200;++i) {
        const auto event=s.Update(25,true,false,true,true,minimum);
        turns+=event.turn;ended=ended||event.finished;
        if(i<199)CHECK(!event.finished);
    }
    CHECK(ended&&!s.Active()&&turns==0&&s.WaitMs()==120000);
}
void PauseResumeInterruptionAndStalls() {
    Scheduler s;s.Update(0,true,false,false,false,minimum);
    const auto wait=s.WaitMs();
    for(unsigned i=0;i<1000;++i)s.Update(250,false,true,true,false,minimum);
    CHECK(s.WaitMs()==wait&&!s.Active());
    s.Update(31200000,true,true,true,false,minimum); // Safety cap, no hour of catch-up.
    CHECK(s.WaitMs()==wait-250&&!s.Active());
    while(!s.Active())s.Update(250,true,true,false,false,minimum);
    const auto remaining=s.RemainingRunMs();
    for(unsigned i=0;i<1000;++i)s.Update(250,false,false,true,true,minimum);
    CHECK(s.RemainingRunMs()==remaining&&s.Active());
    CHECK(!s.Update(25,true,true,true,false,minimum).finished);
    CHECK(!s.Active()&&s.WaitMs()==120000); // New command owns motion; scheduler must not stop it.
    s.Reset();CHECK(!s.Active()&&!s.Due());
}
void RandomBoundsAndNoFrameRateDependentLottery() {
    std::mt19937 rng(48071);unsigned choices=0;
    const auto draw=[&](std::uint64_t low,std::uint64_t high) {
        ++choices;return std::uniform_int_distribution<std::uint64_t>(low,high)(rng);
    };
    for(unsigned repetition=0;repetition<100;++repetition) {
        Scheduler s;s.Update(0,true,false,false,false,draw);
        CHECK(s.WaitMs()>=120000&&s.WaitMs()<=420000);
        const auto initialChoices=choices;
        while(s.WaitMs())s.Update(250,true,false,false,false,draw);
        CHECK(choices==initialChoices);
        const auto event=s.Update(0,true,true,false,false,draw);
        CHECK(event.started&&event.runDurationMs>=5000&&event.runDurationMs<=15000);
        CHECK(choices==initialChoices+2);
        std::uint64_t consumed=0;
        while(s.Active()) {
            const auto before=s.RemainingRunMs();const auto step=std::min<std::uint64_t>(16,before);
            const auto frame=s.Update(step,true,false,true,true,draw);consumed+=step;
            if(frame.finished)CHECK(consumed==event.runDurationMs);
        }
        CHECK(s.WaitMs()>=120000&&s.WaitMs()<=420000);
    }
}
void ManySafeTurnsDoNotSpendTheRunningBudget() {
    Scheduler s;
    const auto longest=[](std::uint64_t low,std::uint64_t high) {
        return low==Scheduler::MinRunMs?high:low;
    };
    s.Update(0,true,false,false,false,longest);
    while(!s.Active())s.Update(250,true,true,false,false,longest);
    unsigned turns=0;std::uint64_t moving=0,wall=0;
    while(s.Active()) {
        const auto event=s.Update(50,true,false,true,true,longest);moving+=50;wall+=50;
        if(event.turn) {
            ++turns;
            for(unsigned i=0;i<48;++i) {
                CHECK(!s.Update(50,true,false,false,true,longest).finished);wall+=50;
            }
        }
    }
    CHECK(moving==15000&&turns==2&&wall>19000);
    // A body with no usable movement still gives up after a bounded interval.
    s.Reset();s.Update(0,true,false,false,false,longest);
    while(!s.Active())s.Update(250,true,true,false,false,longest);
    for(unsigned i=0;i<31;++i)CHECK(!s.Update(250,true,false,false,true,longest).finished);
    CHECK(s.Update(250,true,false,false,true,longest).finished&&!s.Active());
}
}
int main() {
    try {QuietGapAndOneDeferredOpportunity();RunningBudgetExcludesSafeTransitions();
        PauseResumeInterruptionAndStalls();RandomBoundsAndNoFrameRateDependentLottery();ManySafeTurnsDoNotSpendTheRunningBudget();
        std::cout<<"Random zoomies: "<<checks<<" checks passed\n";return EXIT_SUCCESS;
    } catch(const std::exception& error) {std::cerr<<error.what()<<'\n';return EXIT_FAILURE;}
}
