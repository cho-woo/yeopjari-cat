#include "../src/cat_behavior.hpp"

#include <array>
#include <cwchar>
#include <iostream>
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
    for(auto a:{Action::Loaf,Action::Knead,Action::Peek,Action::Scratch,Action::Sneeze,Action::BellyRoll,Action::TailRaise,Action::Jump})
        CHECK(seen[static_cast<unsigned>(a)]>0);
    CHECK(kneadSequence); CHECK(bellySequence); CHECK(zoomSequence);
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
        StablePersonalityAndName(); DeterministicDailyOrder(); PausedCatStaysPut(); PausingAnActiveStoryCancelsItsTravel(); InteractionsCancelStaleStories(); NewActionsAndSequencesAppear(); FeedingAndPettingCreateCalmFollowups();
        std::cout<<"PASS: personality, new actions and sequences; "<<checks<<" checks\n"; return 0;
    } catch(const std::exception& e){ std::cerr<<e.what()<<'\n'; return 1; }
}
