#include "../src/cat_behavior.hpp"

#include <array>
#include <cwchar>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>

namespace {
using cat::behavior::Action;
using cat::behavior::Brain;
using cat::behavior::Context;
int checks = 0;
#define CHECK(condition) do { ++checks; if (!(condition)) throw std::runtime_error( \
    std::string(__FILE__) + ":" + std::to_string(__LINE__) + " failed: " #condition); } while (false)
constexpr std::int64_t Birthday = 1793416455;
constexpr std::uint64_t Hour = 60ULL * 60ULL * 1000ULL;

bool Travels(Action a) {
    return a == Action::Walk || a == Action::Stalk || a == Action::Pounce || a == Action::Zoomies || a == Action::Jump;
}

void StablePersonalityAndName() {
    Brain a(Birthday, 1), b(Birthday, 999), c(Birthday + 1, 1);
    const auto pa=a.GetPersonality(), pb=b.GetPersonality(), pc=c.GetPersonality();
    CHECK(pa.curiosity==pb.curiosity && pa.energy==pb.energy && pa.sociability==pb.sociability && pa.mischief==pb.mischief);
    CHECK(a.GetArchetype()==b.GetArchetype());
    CHECK(std::wcslen(cat::behavior::ArchetypeName(a.GetArchetype())) >= 2);
    CHECK(pa.curiosity!=pc.curiosity || pa.energy!=pc.energy || pa.sociability!=pc.sociability || pa.mischief!=pc.mischief);
    for(double v:{pa.curiosity,pa.energy,pa.sociability,pa.mischief}) CHECK(v>=.18 && v<.82);
}

void DeterministicDailyOrder() {
    Brain a(Birthday,77), b(Birthday,77);
    Context ctx; ctx.cursorNear=true;
    for(int i=0;i<1200;++i) {
        auto x=a.Next(ctx), y=b.Next(ctx);
        CHECK(x.action==y.action && x.durationMs==y.durationMs && x.speed==y.speed);
        CHECK(x.durationMs>=1000 && x.durationMs<=120000);
        CHECK(x.speed>=0 && x.speed<=250);
        ctx.ticks += x.durationMs;
        ctx.localHour = static_cast<int>((ctx.ticks/Hour+8)%24);
    }
}

void PausedCatStaysPut() {
    Brain brain(Birthday,31); Context ctx; ctx.roamingPaused=true; ctx.cursorNear=true;
    for(int i=0;i<4000;++i) {
        auto p=brain.Next(ctx);
        CHECK(!Travels(p.action));
        CHECK(p.speed==0.0);
        ctx.ticks += p.durationMs;
    }
}

void NewActionsAndSequencesAppear() {
    std::array<unsigned, static_cast<unsigned>(Action::Sneeze)+1> seen{};
    bool kneadSequence=false, bellySequence=false, zoomSequence=false;
    for(unsigned seed=1; seed<=24; ++seed) {
        Brain brain(Birthday,seed); Context ctx; ctx.cursorNear=true;
        Action previous=Action::Idle;
        for(int i=0;i<12000;++i) {
            auto p=brain.Next(ctx);
            ++seen[static_cast<unsigned>(p.action)];
            if(previous==Action::Knead && p.action==Action::Loaf) kneadSequence=true;
            if(previous==Action::BellyRoll && p.action==Action::Groom) bellySequence=true;
            if(previous==Action::Zoomies && (p.action==Action::Jump || p.action==Action::Watch)) zoomSequence=true;
            previous=p.action;
            ctx.ticks += p.durationMs;
            ctx.localHour=static_cast<int>((ctx.ticks/Hour+6)%24);
        }
    }
    // Every autonomous action, including all gestures removed from the menu,
    // must remain reachable without invoking a manual action API.
    for(const auto count:seen)CHECK(count>0);
    CHECK(kneadSequence); CHECK(bellySequence); CHECK(zoomSequence);
}

void AutomaticGesturesDoNotRequireCursorOrCommands() {
    std::array<unsigned,static_cast<unsigned>(Action::Sneeze)+1> seen{};
    for(unsigned seed=1;seed<=24;++seed){
        Brain brain(Birthday+seed,seed);Context ctx;
        while(ctx.ticks<48*Hour){
            const auto p=brain.Next(ctx);++seen[static_cast<unsigned>(p.action)];
            ctx.ticks+=p.durationMs;ctx.localHour=static_cast<int>((ctx.ticks/Hour+8)%24);
        }
    }
    CHECK(seen[static_cast<unsigned>(Action::Stalk)]==0);
    CHECK(seen[static_cast<unsigned>(Action::Pounce)]==0);
    for(auto a:{Action::Walk,Action::Zoomies,Action::Jump,Action::Punch,Action::EarFlick,
            Action::TailRaise,Action::BellyRoll,Action::Loaf,Action::Knead})
        CHECK(seen[static_cast<unsigned>(a)]>20);
}

void PausingAnActiveStoryCancelsItsTravel() {
    unsigned stories = 0;
    Brain brain(Birthday, 31); Context ctx; ctx.cursorNear = true;
    for (int i = 0; i < 10000; ++i) {
        const auto action = brain.Next(ctx);
        ctx.ticks += action.durationMs;
        if (action.action == Action::Zoomies || action.action == Action::TailRaise ||
            action.action == Action::Peek) {
            Context paused = ctx; paused.roamingPaused = true;
            auto pausedBrain = brain;
            for (int followup = 0; followup < 3; ++followup) {
                const auto plan = pausedBrain.Next(paused);
                CHECK(!Travels(plan.action));
                CHECK(plan.speed == 0);
                paused.ticks += plan.durationMs;
            }
            ++stories;
        }
    }
    CHECK(stories > 20);
}

void InteractionsCancelStaleStories() {
    unsigned jumpStories = 0;
    Brain brain(Birthday, 13); Context ctx; ctx.cursorNear = true;
    for (int i = 0; i < 10000; ++i) {
        const auto action = brain.Next(ctx);
        ctx.ticks += action.durationMs;
        if (action.action != Action::Zoomies) continue;
        auto preview = brain;
        if (preview.Next(ctx).action != Action::Jump) continue;
        ++jumpStories;
        auto petted = brain;
        petted.Pet(ctx.ticks);
        auto reply = petted.Next(ctx);
        CHECK(reply.action == Action::Watch || reply.action == Action::Groom || reply.action == Action::Knead);
        Context afterPet = ctx; afterPet.ticks += reply.durationMs;
        CHECK(petted.Next(afterPet).action != Action::Jump);

        auto interrupted = brain;
        interrupted.Interrupt(ctx.ticks);
        CHECK(interrupted.Next(ctx).action != Action::Jump);
        CHECK(interrupted.GetArchetype() == brain.GetArchetype());
        CHECK(interrupted.GetPersonality().curiosity == brain.GetPersonality().curiosity);
    }
    CHECK(jumpStories > 10);

    Brain manual(Birthday, 77); Context now;
    manual.Fed(0, false);
    manual.Pet(0);
    manual.Interrupt(0);
    // Compare against an otherwise identical initialized scheduler: neither
    // feeding nor petting may leak a pending response across an interruption.
    Brain expected(Birthday, 77);
    expected.Fed(0, false); // same content state and random initialization
    expected.Interrupt(0);
    const auto actual = manual.Next(now), reference = expected.Next(now);
    CHECK(actual.action == reference.action && actual.durationMs == reference.durationMs);

    Brain forcedPunch(Birthday, 77);
    forcedPunch.Fed(0, false);
    forcedPunch.Pet(0);
    forcedPunch.MarkPunch(0);
    CHECK(forcedPunch.Next(now).action == Action::Idle);
}

void ExperiencePreservesBirthAndBoundsLearning() {
    using cat::behavior::Experience;
    for (int offset = 0; offset < 1000; ++offset) {
        Brain brain(Birthday + offset, 77);
        const auto birth = brain.GetPersonality();
        const auto archetype = brain.GetArchetype();
        Experience migrated{35, 0, .6};
        brain.SetExperience(migrated);
        CHECK(brain.GetArchetype() == archetype);
        CHECK(brain.GetPersonality().energy == birth.energy);
        CHECK(brain.GetPersonality().sociability == birth.sociability);
        migrated.sociabilityShift = .001;
        brain.SetExperience(migrated);
        CHECK(brain.GetArchetype() == archetype); // no rename after the first tiny credited care
        CHECK(std::abs(brain.GetPersonality().sociability - birth.sociability - .001) < 1e-12);
        Experience learned{100, 1, 1, .15, -.15, .15, -.15};
        brain.SetExperience(learned);
        const auto experienced = brain.GetPersonality();
        for (int reload = 0; reload < 10; ++reload) brain.SetExperience(learned);
        CHECK(brain.GetPersonality().sociability == experienced.sociability);
        CHECK(brain.GetPersonality().energy == experienced.energy);
        CHECK(brain.GetBirthPersonality().curiosity == birth.curiosity);
        CHECK(brain.GetBirthPersonality().energy == birth.energy);
        for (auto pair : {std::pair<double,double>{experienced.curiosity, birth.curiosity},
                          {experienced.energy, birth.energy}, {experienced.sociability, birth.sociability},
                          {experienced.mischief, birth.mischief}}) {
            CHECK(pair.first >= .05 && pair.first <= .95);
            CHECK(std::abs(pair.first-pair.second) <= Brain::MaximumExperienceShift + 1e-12);
        }
        Brain restored(Birthday + offset, 12);
        restored.SetExperience(learned);
        CHECK(restored.GetArchetype() == brain.GetArchetype());
        brain.SetExperience({});
        CHECK(brain.GetArchetype() == archetype);
        CHECK(brain.GetPersonality().energy == birth.energy);
        CHECK(brain.GetPersonality().sociability == birth.sociability);
    }
    Brain invalid(Birthday, 77);
    const auto birth = invalid.GetPersonality();
    const auto nan = std::numeric_limits<double>::quiet_NaN();
    invalid.SetExperience({nan,nan,nan,nan,nan,nan,nan});
    CHECK(invalid.GetPersonality().sociability == birth.sociability);
    CHECK(invalid.GetPersonality().energy == birth.energy);
    invalid.SetExperience({1e20,-1e20,1e20,1e20,-1e20,1e20,-1e20});
    CHECK(std::abs(invalid.GetPersonality().energy - birth.energy) <= .15 + 1e-12);
    CHECK(std::abs(invalid.GetPersonality().sociability - birth.sociability) <= .15 + 1e-12);
}

using ActionCounts = std::array<unsigned, static_cast<unsigned>(Action::Sneeze) + 1>;
ActionCounts CountActions(int hour, cat::behavior::Experience experience = {}, bool paused = false) {
    ActionCounts counts{};
    for (unsigned seed = 1; seed <= 16; ++seed) {
        Brain brain(Birthday + seed * 86400, seed);
        brain.SetExperience(experience);
        Context ctx; ctx.localHour = hour; ctx.ageYears = 3;
        ctx.roamingPaused = paused; ctx.cursorNear = true;
        for (int i = 0; i < 6000; ++i) {
            const auto plan = brain.Next(ctx);
            ++counts[static_cast<unsigned>(plan.action)];
            if (paused) CHECK(!Travels(plan.action) && plan.speed == 0);
            ctx.ticks += 10 * 60 * 1000; // allow rare-action cooldowns; compare distributions, not elapsed-time occupancy
        }
    }
    return counts;
}
unsigned Count(const ActionCounts& counts, Action action) { return counts[static_cast<unsigned>(action)]; }

void DailyRoutineChangesActualChoices() {
    const auto morning = CountActions(8), baseline = CountActions(10);
    const auto afternoon = CountActions(16), night = CountActions(23);
    CHECK(Count(morning, Action::Stretch) > Count(baseline, Action::Stretch) * 1.20);
    CHECK(Count(afternoon, Action::Zoomies) > Count(baseline, Action::Zoomies) * 1.50);
    CHECK(Count(night, Action::Sleep) > Count(baseline, Action::Sleep) * 1.50);
    CHECK(Count(night, Action::Zoomies) < Count(baseline, Action::Zoomies));
    CHECK(CountActions(24) == CountActions(0));
    CHECK(CountActions(-1) == CountActions(23));
}

void LearnedCareChangesResponsesWithoutForcingThem() {
    const auto neutral = CountActions(12);
    const auto petted = CountActions(12, {0, 1, 0});
    const auto bonded = CountActions(12, {100, 1, 0, .15, -.10, .05, 0});
    CHECK(Count(petted, Action::BellyRoll) > Count(neutral, Action::BellyRoll) * 1.60);
    CHECK(Count(petted, Action::BellyRoll) < Count(neutral, Action::BellyRoll) * 3.20);
    CHECK(Count(bonded, Action::Knead) > Count(neutral, Action::Knead));
    CHECK(Count(bonded, Action::Walk) > 0);
    CHECK(Count(bonded, Action::Groom) > 0);
    CHECK(Count(bonded, Action::Sleep) > 0);
    CountActions(16, {100, 1, 1, .15, .15, .15, .15}, true);
    unsigned neutralReplies = 0, favoriteReplies = 0;
    for (unsigned seed = 1; seed <= 16; ++seed) {
        Brain normal(Birthday + seed * 86400, seed), favorite = normal;
        favorite.SetExperience({0, 0, 1});
        normal.Interrupt(0); favorite.Interrupt(0);
        Context ctx; ctx.ageYears = 3;
        for (int i = 1; i <= 500; ++i) {
            ctx.ticks = static_cast<std::uint64_t>(i) * 600000;
            normal.Fed(ctx.ticks, true); favorite.Fed(ctx.ticks, true);
            const auto a = normal.Next(ctx), b = favorite.Next(ctx);
            CHECK(a.action == Action::Groom && b.action == Action::Groom);
            Context after = ctx; after.ticks += std::max(a.durationMs, b.durationMs);
            if (normal.Next(after).action == Action::TailRaise) ++neutralReplies;
            if (favorite.Next(after).action == Action::TailRaise) ++favoriteReplies;
        }
    }
    CHECK(favoriteReplies > neutralReplies * 3);
    CHECK(favoriteReplies < 8000); // a preference is not compulsory obedience
}

void FeedingAndPettingCreateCalmFollowups() {
    Brain brain(Birthday,13); Context ctx; ctx.ticks=100000;
    brain.Fed(ctx.ticks,false);
    auto groom=brain.Next(ctx); CHECK(groom.action==Action::Groom);
    ctx.ticks += groom.durationMs;
    brain.Pet(ctx.ticks);
    auto reply=brain.Next(ctx);
    CHECK(reply.action==Action::Watch || reply.action==Action::Groom || reply.action==Action::Knead);
}
}

int main(){
    try{
        StablePersonalityAndName(); DeterministicDailyOrder(); PausedCatStaysPut(); PausingAnActiveStoryCancelsItsTravel(); InteractionsCancelStaleStories(); NewActionsAndSequencesAppear(); AutomaticGesturesDoNotRequireCursorOrCommands(); FeedingAndPettingCreateCalmFollowups(); ExperiencePreservesBirthAndBoundsLearning(); DailyRoutineChangesActualChoices(); LearnedCareChangesResponsesWithoutForcingThem();
        std::cout<<"PASS: personality, new actions and sequences; "<<checks<<" checks\n"; return 0;
    } catch(const std::exception& e){ std::cerr<<e.what()<<'\n'; return 1; }
}
