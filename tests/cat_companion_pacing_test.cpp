#include "../src/cat_companion.hpp"
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {
using namespace cat::companion;
using M=cat::animation::Motion;
constexpr std::int64_t born=1700000000;
int checks=0;
#define CHECK(value) do {++checks;if(!(value))throw std::runtime_error(std::string(__FILE__)+":"+std::to_string(__LINE__)+" " #value);}while(false)
Context Frame(std::uint64_t ticks,double x) {
    Context c;c.ticks=ticks;c.nowUtc=born+static_cast<std::int64_t>(ticks/1000);
    c.elapsedSeconds=.017;c.primary.grounded=true;c.primary.x=x;return c;
}
void Step(Model& model,Context& c) {
    c.ticks+=17;c.nowUtc=born+static_cast<std::int64_t>(c.ticks/1000);model.Tick(c);
}
void StartupHasAQuietInterval() {
    auto state=NewState(born);Model model;model.Bind(state);model.Reset(0,1600,700);
    auto c=Frame(100,model.Physical().x+60);model.Tick(c);
    for(unsigned frame=0;frame<1058;++frame) {
        Step(model,c);const auto view=model.View(c.ticks);
        CHECK(view.motion==M::Idle&&view.interaction==Interaction::None);
        CHECK(std::abs(model.Physical().velocityX)<.1);
    }
    CHECK(!model.FoodOffered(c)); // An autonomous food race does not skip rest.
    state.nutrition.fullness=state.pet.hunger=45;
    CHECK(model.Feed(c.nowUtc,c.ticks,false)==cat::nutrition::FeedResult::Accepted);
    CHECK(model.View(c.ticks).motion==M::Eat); // Explicit care remains immediate.
}
void CompletedSocialActionCannotImmediatelyStartAnother() {
    auto state=NewState(born);Model model;model.Bind(state);model.Reset(0,1600,700);
    auto c=Frame(100,model.Physical().x+60);
    CHECK(model.TrySocial(Interaction::Sniff,c));model.Tick(c);
    while(c.ticks<12100)Step(model,c);
    CHECK(model.View(c.ticks).interaction==Interaction::None);
    const auto finishedAt=c.ticks;
    while(c.ticks-finishedAt<17900) {
        Step(model,c);const auto view=model.View(c.ticks);
        CHECK(view.motion==M::Idle&&view.interaction==Interaction::None);
    }
    CHECK(state.sniffs==1&&state.mutualPlay==0&&state.sharedSleeps==0);
}
void ExplicitFoodCanInviteARaceAfterTheMinimumRest() {
    auto state=NewState(born);state.nutrition.fullness=state.pet.hunger=45;
    Model model;model.Bind(state);model.Reset(0,1600,700);
    auto c=Frame(100,model.Physical().x+60);c.primaryMotion=M::Eat;c.primaryBusy=true;
    model.Tick(c);
    while(c.ticks<18100) {
        CHECK(!model.FoodOffered(c));Step(model,c);
    }
    const auto meals=state.pet.feedCount,treats=state.pet.snackCount;
    const auto memoryMeals=state.memory.meals,memoryTreats=state.memory.treats;
    const auto fullness=state.nutrition.fullness;
    CHECK(model.FoodOffered(c));model.Tick(c);
    CHECK(model.View(c.ticks).interaction==Interaction::FoodCompetition);
    CHECK(model.TakeEvent()==Interaction::FoodCompetition&&state.foodRaces==1);
    CHECK(state.pet.feedCount==meals&&state.pet.snackCount==treats);
    CHECK(state.memory.meals==memoryMeals&&state.memory.treats==memoryTreats);
    CHECK(state.nutrition.fullness==fullness);
}
void AcceptedMealKeepsItsCompleteGroomFollowUp() {
    auto state=NewState(born);state.preferences.character=cat::Character::Cream;
    state.nutrition.fullness=state.pet.hunger=45;
    Model model;model.Bind(state);model.Reset(0,1600,700);
    auto c=Frame(100,model.Physical().x+60);c.primaryVisible=false;
    CHECK(model.Feed(c.nowUtc,c.ticks,false)==cat::nutrition::FeedResult::Accepted);
    model.Tick(c);
    while(c.ticks<5100) {
        const auto view=model.View(c.ticks);
        CHECK(view.motion==M::Eat);Step(model,c);
    }
    CHECK(model.View(c.ticks).motion==M::Groom);
    const auto groomAt=c.ticks;
    const auto groomFamily=model.View(c.ticks).animation.spriteOptions.variant;
    CHECK(groomFamily<3);
    while(model.View(c.ticks).motion==M::Groom&&c.ticks-groomAt<8500) {
        CHECK(model.View(c.ticks).animation.spriteOptions.variant==groomFamily);
        CHECK(!model.FoodOffered(c));Step(model,c);
    }
    CHECK(c.ticks-groomAt>=3983&&c.ticks-groomAt<=8017);
    CHECK(model.View(c.ticks).motion==M::Idle);
    const auto restedAt=c.ticks;
    while(c.ticks-restedAt<17900) {Step(model,c);CHECK(model.View(c.ticks).motion==M::Idle);}
    CHECK(state.pet.feedCount==1&&state.memory.meals==1);
}
void LongZeroPhysicsStallFreezesActionAndPresentation() {
    auto state=NewState(born);state.preferences.character=cat::Character::Cream;
    state.nutrition.fullness=state.pet.hunger=45;
    Model model;model.Bind(state);model.Reset(0,1600,700);
    auto c=Frame(100,model.Physical().x+60);c.primaryVisible=false;
    CHECK(model.Feed(c.nowUtc,c.ticks,true)==cat::nutrition::FeedResult::Accepted);
    model.Tick(c);for(unsigned frame=0;frame<40;++frame)Step(model,c);
    const auto before=model.View(c.ticks);const auto body=model.Physical();
    c.ticks+=60000;c.nowUtc+=60;c.elapsedSeconds=0;model.Tick(c);
    const auto paused=model.View(c.ticks);
    CHECK(paused.motion==M::Treat&&paused.animation.spriteElapsedMs==before.animation.spriteElapsedMs);
    CHECK(paused.presentationPaused&&paused.presentationTicks==before.presentationTicks);
    CHECK(model.Physical().x==body.x&&model.Physical().lift==body.lift);
    c.elapsedSeconds=.017;Step(model,c);const auto resumed=model.View(c.ticks);
    CHECK(!resumed.presentationPaused&&resumed.presentationTicks==before.presentationTicks+17);
    CHECK(resumed.animation.spriteElapsedMs==before.animation.spriteElapsedMs+17);
}
void SocialSleepWakesBeforeItsNeutralRest() {
    auto state=NewState(born);state.preferences.character=cat::Character::Cream;
    Model model;model.Bind(state);model.Reset(0,1600,700);
    auto c=Frame(100,model.Physical().x+65);c.primaryMotion=M::Sleep;
    CHECK(model.TrySocial(Interaction::SharedSleep,c));model.Tick(c);
    while(c.ticks<30100)Step(model,c);
    CHECK(model.View(c.ticks).motion==M::Stretch);
    const auto wakeAt=c.ticks;
    while(c.ticks-wakeAt<3490) {
        const auto pose=model.View(c.ticks);
        CHECK(pose.motion==M::Stretch);
        CHECK(pose.animation.spriteClip==cat::sprite_sequence::Clip::Sleep);
        Step(model,c);
    }
    Step(model,c);CHECK(model.View(c.ticks).motion==M::Idle);
}
void SocialGesturesFinishBeforeFollowingTheNextPosition() {
    for(const auto kind:{Interaction::Paw,Interaction::Sniff,Interaction::FoodCompetition}) {
        auto state=NewState(born);state.nutrition.fullness=state.pet.hunger=45;
        Model model;model.Bind(state);model.Reset(0,1600,700);
        auto c=Frame(100,model.Physical().x+(kind==Interaction::Paw?48:60));
        if(kind==Interaction::FoodCompetition)c.primaryMotion=M::Eat;
        CHECK(model.TrySocial(kind,c));model.Tick(c);
        const auto gesture=model.View(c.ticks).motion;
        c.primary.x+=100; // The other cat shifts after the gesture has begun.
        while(c.ticks<2050) {Step(model,c);CHECK(model.View(c.ticks).motion==gesture);}
        while(c.ticks<2150)Step(model,c);
        CHECK(cat::animation::Walking(model.View(c.ticks).motion));
    }
}
void RenderingGenerationChangesAcrossResetsAndIdentity() {
    auto state=NewState(born);Model model;model.Bind(state);model.Reset(0,1600,700);
    auto c=Frame(100,model.Physical().x+60);c.primaryVisible=false;model.Tick(c);
    auto generation=model.View(c.ticks).presentationGeneration;
    model.Relocate(0,1600,700);CHECK(model.View(c.ticks).presentationGeneration>generation);
    generation=model.View(c.ticks).presentationGeneration;
    model.DragTo(500);CHECK(model.View(c.ticks).presentationGeneration>generation);
    generation=model.View(c.ticks).presentationGeneration;
    state.preferences.character=cat::Character::Black;
    CHECK(model.View(c.ticks).presentationGeneration>generation);
    const auto switched=model.View(c.ticks).presentationGeneration;Step(model,c);
    CHECK(model.View(c.ticks).presentationGeneration==switched);
    model.Bind(state);model.Reset(0,1600,700);CHECK(model.View(c.ticks).presentationGeneration>switched);
    CHECK(Valid(state));
}
void AutonomousPairRunningRemainsReachableAfterQuietRest() {
    unsigned joined=0;
    for(unsigned seed=0;seed<24;++seed) {
        const auto birthday=born+seed*86400;
        auto state=NewState(birthday);Model model;model.Bind(state);model.Reset(0,1600,700);
        auto c=Frame(100,model.Physical().x+60);c.nowUtc=birthday;c.primaryMotion=M::Zoomies;
        c.primary.velocityX=70;c.primaryBusy=false;
        bool paired=false;
        while(c.ticks<60000) {
            c.primary.x=model.Physical().x+60;
            c.nowUtc=birthday+static_cast<std::int64_t>(c.ticks/1000);
            model.Tick(c);
            if(model.View(c.ticks).interaction==Interaction::RunTogether) {paired=true;break;}
            c.ticks+=17;
        }
        if(!paired)continue;
        CHECK(c.ticks>=18000);++joined;
        for(unsigned frame=0;frame<30;++frame) {
            c.ticks+=17;c.primary.x=model.Physical().x+60;
            c.nowUtc=birthday+static_cast<std::int64_t>(c.ticks/1000);model.Tick(c);
        }
        CHECK(model.Physical().velocityX>30);
        CHECK(state.mutualPlay==1&&state.pet.feedCount==0&&state.pet.snackCount==0);
    }
    CHECK(joined>=5); // Exercise autonomous selection, not direct TrySocial.

    auto state=NewState(born);state.preferences.character=cat::Character::Cream;
    state.nutrition.fullness=state.pet.hunger=45;
    Model model;model.Bind(state);model.Reset(0,1600,700);
    auto c=Frame(100,model.Physical().x+60);c.primaryMotion=M::Zoomies;c.primary.velocityX=70;
    CHECK(model.Feed(c.nowUtc,c.ticks,false)==cat::nutrition::FeedResult::Accepted);
    while(c.ticks<9000) {
        model.Tick(c);const auto pose=model.View(c.ticks);
        CHECK(pose.interaction==Interaction::None&&state.mutualPlay==0);
        CHECK(pose.motion==M::Eat||pose.motion==M::Groom);Step(model,c);
    }
}
}
int main() {
    try {StartupHasAQuietInterval();CompletedSocialActionCannotImmediatelyStartAnother();
        ExplicitFoodCanInviteARaceAfterTheMinimumRest();
        AcceptedMealKeepsItsCompleteGroomFollowUp();LongZeroPhysicsStallFreezesActionAndPresentation();
        SocialSleepWakesBeforeItsNeutralRest();SocialGesturesFinishBeforeFollowingTheNextPosition();
        RenderingGenerationChangesAcrossResetsAndIdentity();
        AutonomousPairRunningRemainsReachableAfterQuietRest();
        std::cout<<"Companion pacing: "<<checks<<" checks passed\n";return EXIT_SUCCESS;
    } catch(const std::exception& error) {std::cerr<<error.what()<<'\n';return EXIT_FAILURE;}
}
