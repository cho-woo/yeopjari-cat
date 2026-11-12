// Give this test a mutable registration table without changing generated
// production art. The real Model still resolves its usual descriptor API.
#define kCareSprites kOriginalCareSprites
#include "../src/care_sprites.generated.hpp"
#undef kCareSprites
namespace cat::sprite_assets { auto kCareSprites=kOriginalCareSprites; }
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
std::uint64_t FinishMealEntrance(Model& model,Context& c) {
    using Clip=cat::sprite_sequence::Clip;
    const auto* walk=cat::sprite_assets::Find(Clip::Walk,0,cat::sprite_assets::kCareSprites);
    CHECK(walk && walk->nominalFramesPerSecond>0 && walk->program.intro.count);
    const auto* meal=cat::sprite_assets::Find(Clip::Eat,0,cat::sprite_assets::kCareSprites);
    const bool embedded=meal&&std::string_view(meal->sourcePath).find("assets/video-actions/")==0;
    const std::uint64_t delay=embedded?0:walk->movementDelayMs,offeredAt=c.ticks;
    // Current meal movies own their entrance. Legacy side-only footage waits
    // for the separately registered front-to-side turn before its food clock.
    while(model.View(c.ticks).motion!=M::Eat && c.ticks-offeredAt<delay+34) {
        const auto view=model.View(c.ticks);
        CHECK(cat::animation::Walking(view.motion));
        CHECK(view.animation.spriteClip==Clip::Walk && !view.animation.spriteTimed);
        CHECK(view.physical.grounded && std::abs(view.physical.velocityX)<.1);
        Step(model,c);
    }
    CHECK(model.View(c.ticks).motion==M::Eat);
    CHECK(c.ticks-offeredAt>=delay && c.ticks-offeredAt<=delay+17);
    return c.ticks;
}
void FinishGroundExit(Model& model,Context& c,M next) {
    using Clip=cat::sprite_sequence::Clip;
    const auto* walk=cat::sprite_assets::Find(Clip::Walk,0,cat::sprite_assets::kCareSprites);
    CHECK(walk && walk->program.outro.count);
    const auto* meal=cat::sprite_assets::Find(Clip::Eat,0,cat::sprite_assets::kCareSprites);
    const bool embedded=meal&&std::string_view(meal->sourcePath).find("assets/video-actions/")==0;
    const auto duration=embedded?0:cat::sprite_sequence::Duration(walk->program,walk->program.outro);
    const auto exitAt=c.ticks;
    while(model.View(c.ticks).motion!=next && c.ticks-exitAt<duration+34) {
        const auto view=model.View(c.ticks);
        CHECK(cat::animation::Walking(view.motion));
        CHECK(view.animation.spriteClip==Clip::Walk && !view.animation.spriteTimed);
        CHECK(view.physical.grounded && std::abs(view.physical.velocityX)<.1);
        Step(model,c);
    }
    CHECK(model.View(c.ticks).motion==next);
    CHECK(c.ticks-exitAt>=duration && c.ticks-exitAt<=duration+17);
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
    FinishMealEntrance(model,c); // Explicit care starts without a quiet-rest delay.
}
void CompletedSocialActionCannotImmediatelyStartAnother() {
    auto state=NewState(born);Model model;model.Bind(state);model.Reset(0,1600,700);
    auto c=Frame(100,model.Physical().x+60);
    CHECK(model.TrySocial(Interaction::Sniff,c));model.Tick(c);
    while(c.ticks<12100)Step(model,c);
    CHECK(model.View(c.ticks).interaction==Interaction::None);
    const auto finishedAt=c.ticks;
    while(c.ticks-finishedAt<cat::behavior::Brain::NeutralRestMinMs-100) {
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
    while(c.ticks<cat::behavior::Brain::NeutralRestMinMs+100) {
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
    const auto mealAt=FinishMealEntrance(model,c);
    const auto* meal=cat::sprite_assets::Find(cat::sprite_sequence::Clip::Eat,0,cat::sprite_assets::kCareSprites);
    const auto* groom=cat::sprite_assets::Find(cat::sprite_sequence::Clip::Groom,0,cat::sprite_assets::kCareSprites);
    CHECK(meal && groom && meal->program.stepCount && groom->program.stepCount);
    const auto mealEnd=mealAt+cat::sprite_sequence::ProgramDuration(meal->program);
    const auto groomDuration=cat::sprite_sequence::ProgramDuration(groom->program);
    while(c.ticks<mealEnd) {
        const auto view=model.View(c.ticks);
        CHECK(view.motion==M::Eat);Step(model,c);
    }
    FinishGroundExit(model,c,M::Groom);
    const auto groomAt=c.ticks;
    const auto groomFamily=model.View(c.ticks).animation.spriteOptions.variant;
    CHECK(groomFamily<3);
    while(model.View(c.ticks).motion==M::Groom&&c.ticks-groomAt<groomDuration+34) {
        CHECK(model.View(c.ticks).animation.spriteOptions.variant==groomFamily);
        CHECK(!model.FoodOffered(c));Step(model,c);
    }
    CHECK(c.ticks-groomAt>=groomDuration&&c.ticks-groomAt<=groomDuration+17);
    CHECK(model.View(c.ticks).motion==M::Idle);
    const auto restedAt=c.ticks;
    while(c.ticks-restedAt<cat::behavior::Brain::NeutralRestMinMs-100) {Step(model,c);CHECK(model.View(c.ticks).motion==M::Idle);}
    CHECK(state.pet.feedCount==1&&state.memory.meals==1);
}
void HeldCareSurvivesAutomaticExpiryButExplicitFoodStillWins() {
    using namespace cat::sprite_sequence;
    auto found=std::find_if(cat::sprite_assets::kCareSprites.begin(),cat::sprite_assets::kCareSprites.end(),
        [](const auto& asset){return asset.character==0&&asset.clip==Clip::Eat;});
    CHECK(found!=cat::sprite_assets::kCareSprites.end());
    struct RestoreFinish {Program& program;Finish original;~RestoreFinish(){program.finish=original;}} restore{found->program,found->program.finish};
    found->program.finish=Finish::Hold;
    auto state=NewState(born);state.preferences.character=cat::Character::Cream;
    state.nutrition.fullness=state.pet.hunger=45;
    Model model;model.Bind(state);model.Reset(0,1600,700);
    auto c=Frame(100,model.Physical().x+60);c.primaryVisible=false;
    CHECK(model.Feed(c.nowUtc,c.ticks,false)==cat::nutrition::FeedResult::Accepted);
    model.Tick(c);
    const auto mealAt=FinishMealEntrance(model,c);
    const auto duration=ProgramDuration(found->program),deadline=mealAt+duration;
    // Honor the same nutrition cooldown as production; the held pose survives
    // many ordinary decisions before a valid explicit feeding overrides it.
    while(c.ticks<deadline+cat::nutrition::FeedingCooldown*1000) {
        Step(model,c);const auto view=model.View(c.ticks);
        CHECK(view.motion==M::Eat&&view.physical.grounded&&std::abs(view.physical.velocityX)<.1);
        CHECK(model.HoldingFinalCarePose(c.ticks)==(c.ticks>=deadline));
        if(c.ticks>=deadline) {
            const auto pose=SampleProgram(Clip::Eat,found->program,view.animation.spriteElapsedMs,view.animation.spriteOptions);
            CHECK(pose.holding&&!pose.finished&&pose.frame==found->program.steps[found->program.stepCount-1].frame);
        }
    }
    CHECK(state.pet.feedCount==1&&state.memory.meals==1);
    state.nutrition.fullness=state.pet.hunger=45;
    CHECK(model.Feed(c.nowUtc,c.ticks,true)==cat::nutrition::FeedResult::Accepted);
    CHECK(!model.HoldingFinalCarePose(c.ticks));
    FinishGroundExit(model,c,M::Treat);
    Step(model,c);CHECK(model.View(c.ticks).motion==M::Treat);
    CHECK(state.pet.feedCount==1&&state.pet.snackCount==1);
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
    const auto* sleep=cat::sprite_assets::Find(cat::sprite_sequence::Clip::Sleep,0,cat::sprite_assets::kCareSprites);
    CHECK(sleep && sleep->program.stepCount);
    const auto wakeDuration=std::max<std::uint64_t>(3200,
        cat::sprite_sequence::Duration(sleep->program,sleep->program.outro)+500);
    while(c.ticks+17<wakeAt+wakeDuration) {
        const auto pose=model.View(c.ticks);
        CHECK(pose.motion==M::Stretch);
        CHECK(pose.animation.spriteClip==cat::sprite_sequence::Clip::Sleep);
        Step(model,c);
    }
    CHECK(model.View(wakeAt+wakeDuration-1).motion==M::Stretch);
    c.ticks=wakeAt+wakeDuration;c.nowUtc=born+static_cast<std::int64_t>(c.ticks/1000);model.Tick(c);
    CHECK(model.View(c.ticks).motion==M::Idle);
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
        const auto* knead=cat::sprite_assets::Find(cat::sprite_sequence::Clip::Knead,0,cat::sprite_assets::kCareSprites);
        const auto duration=kind==Interaction::FoodCompetition&&knead?
            std::max<std::uint64_t>(2000,cat::sprite_sequence::ProgramDuration(knead->program)):2000;
        while(c.ticks<100+duration-50) {Step(model,c);CHECK(model.View(c.ticks).motion==gesture);}
        while(c.ticks<100+duration+50)Step(model,c);
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
        while(c.ticks<120000) {
            c.primary.x=model.Physical().x+60;
            c.nowUtc=birthday+static_cast<std::int64_t>(c.ticks/1000);
            model.Tick(c);
            if(model.View(c.ticks).interaction==Interaction::RunTogether) {paired=true;break;}
            c.ticks+=17;
        }
        if(!paired)continue;
        CHECK(c.ticks>=cat::behavior::Brain::NeutralRestMinMs);++joined;
        const auto* walk=cat::sprite_assets::Find(cat::sprite_sequence::Clip::Walk,0,
            cat::sprite_assets::kCareSprites);
        CHECK(walk && walk->program.intro.count);
        const auto entrance=cat::sprite_sequence::Duration(walk->program,walk->program.intro);
        const auto pairedAt=c.ticks;
        // Keep the original half-second acceleration check after the actual
        // source-video entrance, rather than assuming immediate travel.
        for(unsigned frame=0;frame<30+(entrance+16)/17;++frame) {
            c.ticks+=17;c.primary.x=model.Physical().x+60;
            c.nowUtc=birthday+static_cast<std::int64_t>(c.ticks/1000);model.Tick(c);
            if(c.ticks-pairedAt<walk->movementDelayMs)
                CHECK(std::abs(model.Physical().velocityX)<.1);
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
        if(cat::animation::Walking(pose.motion)) {
            CHECK(pose.animation.spriteClip==cat::sprite_sequence::Clip::Walk &&
                !pose.animation.spriteTimed && std::abs(pose.physical.velocityX)<.1);
        } else CHECK(pose.motion==M::Eat||pose.motion==M::Groom);
        Step(model,c);
    }
}
}
int main() {
    try {StartupHasAQuietInterval();CompletedSocialActionCannotImmediatelyStartAnother();
        ExplicitFoodCanInviteARaceAfterTheMinimumRest();
        AcceptedMealKeepsItsCompleteGroomFollowUp();LongZeroPhysicsStallFreezesActionAndPresentation();
        HeldCareSurvivesAutomaticExpiryButExplicitFoodStillWins();
        SocialSleepWakesBeforeItsNeutralRest();SocialGesturesFinishBeforeFollowingTheNextPosition();
        RenderingGenerationChangesAcrossResetsAndIdentity();
        AutonomousPairRunningRemainsReachableAfterQuietRest();
        std::cout<<"Companion pacing: "<<checks<<" checks passed\n";return EXIT_SUCCESS;
    } catch(const std::exception& error) {std::cerr<<error.what()<<'\n';return EXIT_FAILURE;}
}
