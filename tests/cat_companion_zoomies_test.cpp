#include "../src/care_sprites.generated.hpp"
#include "../src/cat_companion.hpp"
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {
using namespace cat::companion;
using Motion=cat::animation::Motion;
constexpr std::int64_t Born=1700000000;
unsigned checks=0;
#define CHECK(value) do {++checks;if(!(value))throw std::runtime_error(std::string(__FILE__)+":"+std::to_string(__LINE__)+" " #value);}while(false)
Context Frame() {
    Context c;c.ticks=100;c.nowUtc=Born;c.elapsedSeconds=.05;c.primary.grounded=true;
    c.primary.x=800;c.primaryVisible=false;return c;
}
void Step(Model& model,Context& c) {
    c.ticks+=50;c.nowUtc=Born+static_cast<std::int64_t>(c.ticks/1000);model.Tick(c);
}
void StartDeferred(Model& model,Context& c) {
    model.Tick(c);
    CHECK(model.RandomZoomiesWaitMs()>=60000&&model.RandomZoomiesWaitMs()<=300000);
    while(c.ticks<310100) {
        Step(model,c);
        CHECK(!model.RandomZoomiesActive());
        CHECK(model.View(c.ticks).interaction==Interaction::None);
    }
    CHECK(model.RandomZoomiesWaitMs()==0); // One pending event waits for a safe boundary.
    c.primaryVisible=true;
    while(!model.RandomZoomiesActive()&&c.ticks<650000)Step(model,c);
    CHECK(model.RandomZoomiesActive());
    CHECK(model.RandomZoomiesRemainingRunMs()>=5000&&model.RandomZoomiesRemainingRunMs()<=15000);
}
void CompleteRealRunAndTurns() {
    auto state=NewState(Born);Model model;model.Bind(state);model.Reset(0,1600,700);auto c=Frame();
    StartDeferred(model,c);
    const auto duration=model.RandomZoomiesRemainingRunMs(),started=c.ticks;
    double distance=0;unsigned turns=0;int priorDirection=0;
    while(model.RandomZoomiesActive()&&c.ticks-started<duration+60000) {
        const auto previous=model.Physical();Step(model,c);const auto physical=model.Physical();
        CHECK(std::isfinite(physical.x)&&physical.grounded);
        CHECK(std::abs(physical.x-previous.x)<15); // Motor motion, no coordinate teleport.
        distance+=std::abs(physical.x-previous.x);
        if(std::abs(physical.velocityX)>30) {
            const int direction=physical.velocityX<0?-1:1;
            if(priorDirection&&direction!=priorDirection)++turns;
            priorDirection=direction;
        }
        if(model.RandomZoomiesActive())CHECK(model.View(c.ticks).motion==Motion::Zoomies ||
            cat::animation::Walking(model.View(c.ticks).motion));
    }
    CHECK(!model.RandomZoomiesActive());
    CHECK(turns>=1&&distance>300);
    CHECK(c.ticks-started>=duration); // Start/plant/turn overhead does not spend the chosen running budget.
    CHECK(model.RandomZoomiesWaitMs()>=60000&&model.RandomZoomiesWaitMs()<=300000);
    CHECK(model.View(c.ticks).interaction==Interaction::None);
    const auto wait=model.RandomZoomiesWaitMs();c.paused=true;c.ticks+=300000;c.elapsedSeconds=0;model.Tick(c);
    CHECK(model.RandomZoomiesWaitMs()==wait&&!model.RandomZoomiesActive());
}
void ExplicitCareWinsAndPauseCancels() {
    auto state=NewState(Born);Model model;model.Bind(state);model.Reset(0,1600,700);auto c=Frame();
    StartDeferred(model,c);
    state.nutrition.fullness=state.pet.hunger=45;
    CHECK(model.Feed(c.nowUtc,c.ticks,false)==cat::nutrition::FeedResult::Accepted);
    Step(model,c);
    CHECK(!model.RandomZoomiesActive());
    CHECK(model.RandomZoomiesWaitMs()>=60000&&model.RandomZoomiesWaitMs()<=300000);
    CHECK(state.pet.feedCount==1);
    // A fresh model runs the identical pending opportunity; the saved pause
    // preference cancels it and keeps the cat still without changing that save.
    auto paused=NewState(Born);Model second;second.Bind(paused);second.Reset(0,1600,700);auto d=Frame();
    StartDeferred(second,d);paused.pet.roamingPaused=true;Step(second,d);
    CHECK(!second.RandomZoomiesActive()&&paused.pet.roamingPaused);
    for(unsigned i=0;i<100;++i)Step(second,d);
    CHECK(std::abs(second.Physical().velocityX)<.1);
}
}
int main() {
    try {CompleteRealRunAndTurns();ExplicitCareWinsAndPauseCancels();
        std::cout<<"Companion random zoomies: "<<checks<<" checks passed\n";return EXIT_SUCCESS;
    } catch(const std::exception& error) {std::cerr<<error.what()<<'\n';return EXIT_FAILURE;}
}
