#include "../src/cat_toy.hpp"

#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
int checks = 0;
#define CHECK(condition) do { ++checks; if (!(condition)) throw std::runtime_error( \
    std::string(__FILE__) + ":" + std::to_string(__LINE__) + " failed: " #condition); } while (false)
bool Near(double a, double b, double tolerance = 1e-9) { return std::abs(a - b) <= tolerance; }

void ReachableTargets() {
    using namespace cat::toy;
    CHECK(ApproachTarget(Kind::Box, 450, 200, 36, 96, 1500) == 450);
    CHECK(ApproachTarget(Kind::Box, 40, 200, 36, 96, 1500) == 96);
    CHECK(ApproachTarget(Kind::Ball, 450, 200, 36, 96, 1500) == 414);
    CHECK(ApproachTarget(Kind::Ball, 450, 700, 36, 96, 1500) == 486);
    // Near an edge, approach from the other side if the usual side is blocked.
    CHECK(ApproachTarget(Kind::Ball, 100, 96, 36, 96, 1500) == 136);
    CHECK(ApproachTarget(Kind::Ball, 1496, 1500, 36, 96, 1500) == 1460);
    CHECK(ApproachTarget(Kind::None, 450, 200, 36, 96, 1500) == 200);
    CHECK(ClampCenter(200, 96, 64) == 80);
    CHECK(ApproachTarget(Kind::Box, 200, 200, 36, 96, 64) == 80);
    CHECK(ApproachTarget(Kind::Ball, 200, 200, 36, 96, 64) == 80);
    CHECK(ApproachVelocity(200) == 38);
    CHECK(ApproachVelocity(-200) == -38);
    CHECK(ApproachVelocity(2) == 0);
    CHECK(ApproachVelocity(-2) == 0);
    CHECK(ApproachVelocity(2.1) > 0 && ApproachVelocity(2.1) < ApproachVelocity(3));
    CHECK(ApproachVelocity(std::numeric_limits<double>::quiet_NaN()) == 0);
}

void ContactAndContinuousRoll() {
    using namespace cat::toy;
    Ball ball{400, 0};
    bool contacted = false;
    CHECK(!PawContact(0, contacted));
    CHECK(!PawContact(329, contacted));
    CHECK(ball.x == 400 && ball.velocity == 0);
    CHECK(PawContact(330, contacted));
    ball.Kick(1, 100);
    CHECK(ball.x == 400); // Contact changes velocity, never the position.
    CHECK(!PawContact(331, contacted));
    CHECK(!PawContact(1000, contacted));
    double previous = ball.x, speed = ball.velocity;
    for (int i = 0; i < 120; ++i) {
        ball.Advance(1.0/60, 96, 1500);
        CHECK(ball.x >= previous);
        CHECK(ball.x - previous < 2);
        CHECK(ball.velocity >= 0 && ball.velocity < speed);
        previous = ball.x; speed = ball.velocity;
    }
    bool skipped = false;
    CHECK(!PawContact(660, skipped));
    CHECK(skipped);
    CHECK(!PawContact(400, skipped));
}

void EdgesRemainReachable() {
    using namespace cat::toy;
    for (double growth : {.82, 1.0}) {
        for (double width : {1920.0, 120.0, 20.0}) {
            const double minCenter = std::min(96.0, width * .5);
            const double maxCenter = width - minCenter;
            const auto bounds = RollingBounds(width, minCenter, maxCenter, 36 * growth);
            CHECK(bounds.first <= bounds.second);
            CHECK(bounds.first >= std::min(14.0, width * .5));
            CHECK(bounds.second <= width - std::min(14.0, width * .5));
            for (int direction : {-1, 1}) {
                Ball ball{direction < 0 ? bounds.first : bounds.second, 0};
                ball.Kick(direction, 200);
                for (int frame = 0; frame < 180; ++frame)
                    ball.Advance(1.0/60, bounds.first, bounds.second);
                CHECK(ball.x >= bounds.first && ball.x <= bounds.second);
                CHECK(ball.velocity == 0);
                const double target = ApproachTarget(Kind::Ball, ball.x, width * .5,
                                                       36 * growth, minCenter, maxCenter);
                CHECK(target >= minCenter && target <= maxCenter);
                CHECK(std::abs(ball.x - target) <= 55 * growth);
            }
        }
    }
    CHECK(RollingBounds(20, 10, 10) == std::make_pair(10.0, 10.0));
    CHECK(RollingBounds(0, 0, 0) == std::make_pair(0.0, 0.0));
    const auto inverted = RollingBounds(120, 96, 24);
    CHECK(inverted.first <= inverted.second);
    CHECK(inverted.first >= 14 && inverted.second <= 106);
}

void FrameRateAndPause() {
    using namespace cat::toy;
    Ball thirty{400, 100}, sixty = thirty, oneTwenty = thirty;
    for (int i = 0; i < 30; ++i) thirty.Advance(1.0/30, 96, 1500);
    for (int i = 0; i < 60; ++i) sixty.Advance(1.0/60, 96, 1500);
    for (int i = 0; i < 120; ++i) oneTwenty.Advance(1.0/120, 96, 1500);
    CHECK(Near(thirty.x, sixty.x)); CHECK(Near(sixty.x, oneTwenty.x));
    CHECK(Near(thirty.velocity, sixty.velocity));
    CHECK(Near(sixty.velocity, oneTwenty.velocity));
    const Ball before = sixty;
    sixty.Advance(.016, 96, 1500, true);
    sixty.Advance(2, 96, 1500);
    sixty.Advance(-1, 96, 1500);
    CHECK(sixty.x == before.x && sixty.velocity == before.velocity);
    Ball edge{1499, 100}; edge.Advance(.1, 96, 1500);
    CHECK(edge.x == 1500 && edge.velocity == 0);
    edge.Kick(-1, 100); edge.Advance(.1, 96, 1500);
    CHECK(edge.x < 1500 && edge.velocity < 0);
    Ball narrow{200, 100}; narrow.Advance(.1, 96, 64);
    CHECK(narrow.x == 80 && narrow.velocity == 0);
}

void RealContactGate() {
    using namespace cat::toy;
    bool consumed=false;
    CHECK(!PawContactAt(340,consumed,90,55,true));
    CHECK(!consumed); // A ball rolling into reach later in the paw window can be hit.
    CHECK(!PawContactAt(400,consumed,35,55,false));
    CHECK(!consumed); // Pause/window support must never create a floor impulse.
    CHECK(PawContactAt(420,consumed,35,55,true));
    CHECK(!PawContactAt(440,consumed,35,55,true));
    consumed=false;
    CHECK(!PawContactAt(660,consumed,20,55,true));
    CHECK(consumed);
    CHECK(!InteractionAllowed(false,false,false,false));
    CHECK(!InteractionAllowed(true,true,false,false));
    CHECK(!InteractionAllowed(true,false,true,false));
    CHECK(!InteractionAllowed(true,false,false,true));
    CHECK(InteractionAllowed(true,false,false,false));
}

std::vector<cat::toy::Phase> FirstActivity(cat::toy::Kind kind) {
    using namespace cat::toy;
    Session session; session.Start(kind);
    std::vector<Phase> phases{session.phase};
    for (int tick=0;tick<60*45 && session.phase!=Phase::Wait;++tick) {
        if (session.Advance(1.0/60,true,true)) phases.push_back(session.phase);
    }
    CHECK(session.phase==Phase::Wait);
    return phases;
}

void DistinctActivitiesAndInterruptions() {
    using namespace cat::toy;
    for(const auto kind:{Kind::Ball,Kind::FishingRod,Kind::Cushion}) {
        Session authored;authored.Start(kind);authored.Set(Phase::Interact);authored.authoredPhaseMs=6583;
        for(unsigned tick=0;tick<390;++tick)CHECK(!authored.Advance(1./60,true,true));
        CHECK(authored.phase==Phase::Interact&&authored.PhaseDuration()==6583);
        for(unsigned tick=0;tick<6;++tick)authored.Advance(1./60,true,true);
        CHECK(authored.phase!=Phase::Interact&&authored.authoredPhaseMs==0);
    }
    CHECK((FirstActivity(Kind::Box)==std::vector<Phase>{Phase::Discover,Phase::Approach,Phase::Inspect,
        Phase::Enter,Phase::Interact,Phase::Sleep,Phase::Exit,Phase::Wait}));
    CHECK((FirstActivity(Kind::PaperBag)==std::vector<Phase>{Phase::Discover,Phase::Approach,Phase::Inspect,
        Phase::Enter,Phase::Interact,Phase::Rest,Phase::Exit,Phase::Wait}));
    CHECK((FirstActivity(Kind::Catnip)==std::vector<Phase>{Phase::Discover,Phase::Approach,Phase::Inspect,
        Phase::Interact,Phase::Rest,Phase::Exit,Phase::Wait}));
    CHECK((FirstActivity(Kind::Cushion)==std::vector<Phase>{Phase::Discover,Phase::Approach,Phase::Inspect,
        Phase::Interact,Phase::Sleep,Phase::Exit,Phase::Wait}));
    for (Kind kind:{Kind::Ball,Kind::FishingRod}) {
        const auto phases=FirstActivity(kind);
        CHECK(std::count(phases.begin(),phases.end(),Phase::Interact)==4);
        CHECK(Duration(kind,Phase::Wait)<=2500);
    }
    Session session; session.Start(Kind::Box,-1);
    CHECK(session.phase==Phase::Discover);
    CHECK(session.Advance(.25,true,false));
    CHECK(session.phase==Phase::Approach); // Discovery begins immediately, not after a random decision.
    for (int tick=0;tick<120;++tick) session.Advance(1.0/60,true,false);
    CHECK(session.phase==Phase::Approach); // Time cannot teleport the cat into a box.
    const auto before=session;
    session.Advance(.25,false,true);
    session.Advance(3,true,true);
    CHECK(session.phase==before.phase && session.elapsedMs==before.elapsedMs);
    CHECK(session.Advance(1.0/60,true,true));
    CHECK(session.phase==Phase::Inspect);
    session.Cancel();
    CHECK(session.kind==Kind::None && session.phase==Phase::None);
    CHECK(!session.Advance(.25,true,true));
}

void ContainerPathsAndFeather() {
    using namespace cat::toy;
    Session session; session.Start(Kind::Box,1); session.Set(Phase::Approach);
    CHECK(SessionTarget(session,500,200,1,96,1500)==432);
    session.Set(Phase::Enter);
    CHECK(SessionTarget(session,500,432,1,96,1500)==500);
    session.Set(Phase::Exit);
    CHECK(SessionTarget(session,500,500,1,96,1500)==400);
    CHECK(SessionTarget(session,100,100,1,96,1500)==200); // Leave through the available side at an edge.
    CHECK(SessionTarget(session,80,80,1,96,64)==80);
    Lure lure; lure.Touch(1);
    lure.Advance(.1);
    CHECK(lure.offset>0 && lure.offset<=26);
    const auto before=lure;
    lure.Advance(.1,true); lure.Advance(2);
    CHECK(lure.offset==before.offset && lure.velocity==before.velocity);
    for (int tick=0;tick<60*8;++tick) lure.Advance(1.0/60);
    CHECK(std::abs(lure.offset)<.01 && std::abs(lure.velocity)<.02);
    for(const int fps:{30,60,120}) {
        Lure released;released.offset=72;released.velocity=0;
        released.Advance(1./fps);
        CHECK(released.offset>65&&released.offset<72); // A recorded release must not snap to the old26px limit.
        for(int tick=0;tick<fps*8;++tick) {
            released.Advance(1./fps);
            CHECK(std::isfinite(released.offset)&&std::abs(released.offset)<=72);
        }
        CHECK(std::abs(released.offset)<.01&&std::abs(released.velocity)<.02);
    }
}

cat::toy::AutoDecision AdvanceAutomatic(cat::toy::Automatic& automatic,
        const cat::toy::AutoInput& input, double seconds, std::uint32_t entropy=0) {
    cat::toy::AutoDecision result;
    for (int tick=0;tick<static_cast<int>(seconds*4);++tick) {
        result=automatic.Advance(.25,input,entropy);
        if (result.action!=cat::toy::AutoAction::None) break;
    }
    return result;
}

void AutomaticTimingAndArbitration() {
    using namespace cat::toy;
    CHECK(Automatic::Delay(true,0)==90);
    CHECK(Automatic::Delay(true,UINT32_MAX)==240);
    CHECK(Automatic::Delay(false,0)==180);
    CHECK(Automatic::Delay(false,UINT32_MAX)==420);
    std::vector<Kind> catalog;
    static_assert(static_cast<int>(Kind::Box)==2 && static_cast<int>(Kind::PaperBag)==3);
    for (unsigned entropy=0;entropy<4;++entropy) catalog.push_back(Automatic::Choose(entropy));
    CHECK((catalog==std::vector<Kind>{Kind::Ball,Kind::Catnip,Kind::FishingRod,Kind::Cushion}));
    CHECK(Retired(Kind::Box)&&Retired(Kind::PaperBag)&&!Retired(Kind::Cushion));
    for (unsigned entropy=0;entropy<1000;++entropy) CHECK(!Retired(Automatic::Choose(entropy)));
    Automatic automatic;
    AutoInput input; input.eligible=true;
    CHECK(AdvanceAutomatic(automatic,input,45).action==AutoAction::None);
    CHECK(Near(automatic.RemainingSeconds(),45));
    input.paused=true;
    CHECK(AdvanceAutomatic(automatic,input,3600).action==AutoAction::None);
    CHECK(Near(automatic.RemainingSeconds(),45));
    input.paused=false;
    CHECK(automatic.Advance(7200,input,0).action==AutoAction::None);
    CHECK(Near(automatic.RemainingSeconds(),45)); // No offline or scheduler-stall catchup.
    input.eligible=false;
    CHECK(AdvanceAutomatic(automatic,input,600).action==AutoAction::None);
    CHECK(automatic.RemainingSeconds()==0);
    input.eligible=true; input.visibleKind=Kind::Cushion;
    CHECK(automatic.Advance(.25,input,0).action==AutoAction::None);
    input.visibleKind=Kind::None;
    const auto placed=automatic.Advance(.25,input,0);
    CHECK(placed.action==AutoAction::Place && placed.kind==Kind::Ball);
    input.visibleKind=placed.kind; input.phase=Phase::Discover;
    CHECK(automatic.Advance(.25,input,0).action==AutoAction::None); // Exactly one placement.
    CHECK(automatic.ActiveKind()==Kind::Ball);
}

void AutomaticCompletionAndTeardown() {
    using namespace cat::toy;
    Automatic automatic; AutoInput input; input.eligible=true;
    CHECK(AdvanceAutomatic(automatic,input,91).action==AutoAction::Place);
    input.visibleKind=Kind::Ball; input.phase=Phase::Interact; input.ballSettled=true;
    CHECK(AdvanceAutomatic(automatic,input,1).action==AutoAction::None); // Resting between strikes is not completion.
    input.phase=Phase::Wait; input.ballSettled=false;
    CHECK(AdvanceAutomatic(automatic,input,5).action==AutoAction::None);
    CHECK(automatic.WaitingToRetire());
    input.phase=Phase::Discover; // Completion stays latched if a host mistakenly restarts its session.
    input.ballSettled=true; input.protectedUse=true;
    CHECK(automatic.Advance(.25,input,0).action==AutoAction::None);
    input.protectedUse=false;
    const auto retired=automatic.Advance(.25,input,0);
    CHECK(retired.action==AutoAction::Retire && retired.kind==Kind::Ball && !retired.timedOut);
    CHECK(automatic.ActiveKind()==Kind::None && automatic.RemainingSeconds()==180);
    input.visibleKind=Kind::None; input.phase=Phase::None;
    CHECK(AdvanceAutomatic(automatic,input,179).action==AutoAction::None);
    CHECK(AdvanceAutomatic(automatic,input,1).action==AutoAction::Place);
    // A monitor reset or failed placement cannot immediately replay that item.
    CHECK(automatic.Advance(.25,input,0).action==AutoAction::None);
    CHECK(automatic.ActiveKind()==Kind::None && automatic.RemainingSeconds()==180);
    CHECK(AdvanceAutomatic(automatic,input,180).action==AutoAction::Place);
    input.visibleKind=Kind::Box; // Another activity replaced our ball.
    CHECK(automatic.Advance(.25,input,0).action==AutoAction::None);
    CHECK(automatic.ActiveKind()==Kind::None); // Never delete the other owner's item.
}

void AuthoredBoxVisitCompletesBeforeSleep() {
    using namespace cat::toy;
    Session session;session.Start(Kind::Box);session.boxVisitMs=3350;session.Set(Phase::Interact);
    for(int step=0;step<13;++step)CHECK(!session.Advance(.25,true,true));
    CHECK(session.phase==Phase::Interact);
    CHECK(session.Advance(.10,true,true) && session.phase==Phase::Sleep);
    session.Start(Kind::Box);session.Set(Phase::Interact);
    CHECK(session.boxVisitMs==0 && session.PhaseDuration()==Duration(Kind::Box,Phase::Interact));
    session.boxVisitMs=std::numeric_limits<double>::quiet_NaN();
    CHECK(session.PhaseDuration()==Duration(Kind::Box,Phase::Interact));
    session.Start(Kind::Ball);session.boxVisitMs=3350;session.Set(Phase::Interact);
    CHECK(session.PhaseDuration()==Duration(Kind::Ball,Phase::Interact));
}

void AuthoredBoxExitHoldsBeforeTravel() {
    using namespace cat::toy;
    static_assert(static_cast<int>(Phase::Exit)==8 && static_cast<int>(Phase::Wait)==9);
    Session session;session.Start(Kind::Box,-1);session.boxExitMs=1250;
    session.Set(Phase::Sleep);session.elapsedMs=session.PhaseDuration();
    CHECK(session.Advance(.001,true,false) && session.phase==Phase::BoxExit);
    CHECK(session.elapsedMs==0 && session.PhaseDuration()==1250);
    CHECK(!Travels(session.phase));
    CHECK(SessionTarget(session,500,470,.82,96,1500)==500);
    CHECK(SessionTarget(session,40,96,1,96,1500)==96);
    for(int step=0;step<4;++step)CHECK(!session.Advance(.25,true,false));
    CHECK(!session.Advance(.2,true,false));
    const auto before=session;
    CHECK(!session.Advance(.25,false,true));
    CHECK(!session.Advance(1,true,true));
    CHECK(session.elapsedMs==before.elapsedMs && session.activeMs==before.activeMs);
    CHECK(!session.Advance(.049,true,false));
    CHECK(session.phase==Phase::BoxExit && Near(session.elapsedMs,1249));
    // The authored outro finishes at the box even if no travel arrival was supplied.
    CHECK(session.Advance(.001,true,false) && session.phase==Phase::Exit);
    CHECK(Travels(session.phase) && SessionTarget(session,500,500,1,96,1500)==600);
    CHECK(!session.Advance(.25,true,false) && session.phase==Phase::Exit);
    CHECK(session.Advance(.001,true,true) && session.phase==Phase::Wait);

    // Direct exit requests retain their existing immediate travel contract.
    session.Set(Phase::Exit);
    CHECK(Travels(session.phase) && session.elapsedMs==0);
    session.Start(Kind::Box);
    CHECK(session.boxExitMs==0);
    for(double disabled:{0.0,-1.0,std::numeric_limits<double>::infinity(),
                         std::numeric_limits<double>::quiet_NaN()}) {
        session.boxExitMs=disabled;session.Set(Phase::Sleep);
        session.elapsedMs=session.PhaseDuration();
        CHECK(session.Advance(.001,true,true) && session.phase==Phase::Exit);
    }
    for(Kind kind:{Kind::PaperBag,Kind::Cushion,Kind::Ball}) {
        session.Start(kind);session.boxExitMs=1250;session.Set(Phase::Rest);
        session.elapsedMs=session.PhaseDuration();
        CHECK(session.Advance(.001,true,true) && session.phase==Phase::Exit);
    }
    session.Start(Kind::Box);session.boxExitMs=1250;session.Set(Phase::BoxExit);
    session.Cancel();
    CHECK(session.boxExitMs==0 && session.phase==Phase::None);
}

void LongAuthoredBoxProgramsKeepTheirCompleteDuration() {
    using namespace cat::toy;
    // A 128-frame program at 100ms per frame must finish its last 28 frames,
    // rather than advance to sleeping/travel when the former 10s cap expires.
    for(Phase phase:{Phase::Interact,Phase::BoxExit}) {
        Session session;session.Start(Kind::Box);
        session.boxVisitMs=session.boxExitMs=12800;session.Set(phase);
        CHECK(session.PhaseDuration()==12800);
        for(int step=0;step<40;++step)CHECK(!session.Advance(.25,true,false));
        CHECK(session.phase==phase && session.elapsedMs==10000);
        for(int step=0;step<11;++step)CHECK(!session.Advance(.25,true,false));
        CHECK(!session.Advance(.049,true,false));
        CHECK(session.phase==phase && Near(session.elapsedMs,12799));
        CHECK(session.Advance(.001,true,false));
        CHECK(session.phase==(phase==Phase::Interact?Phase::Sleep:Phase::Exit));
        session.Set(phase);
        session.boxVisitMs=session.boxExitMs=static_cast<double>(cat::sprite_sequence::kMaxProgramMs)+1;
        CHECK(session.PhaseDuration()==cat::sprite_sequence::kMaxProgramMs);
    }
}

void AutomaticWatchdog() {
    using namespace cat::toy;
    Automatic automatic; AutoInput input; input.eligible=true;
    CHECK(AdvanceAutomatic(automatic,input,91,1).kind==Kind::Catnip);
    input.visibleKind=Kind::Catnip; input.phase=Phase::Approach; input.protectedUse=true;
    CHECK(AdvanceAutomatic(automatic,input,120).action==AutoAction::None);
    CHECK(automatic.WaitingToRetire());
    input.paused=true; input.protectedUse=false;
    CHECK(automatic.Advance(.25,input,0).action==AutoAction::None);
    input.paused=false;
    const auto result=automatic.Advance(.25,input,0);
    CHECK(result.action==AutoAction::Retire && result.kind==Kind::Catnip && result.timedOut);
    CHECK(automatic.ActiveKind()==Kind::None);
}
}

int main() {
    try {
        ReachableTargets(); ContactAndContinuousRoll(); EdgesRemainReachable(); FrameRateAndPause();
        RealContactGate(); DistinctActivitiesAndInterruptions(); ContainerPathsAndFeather();
        AuthoredBoxVisitCompletesBeforeSleep(); AuthoredBoxExitHoldsBeforeTravel();
        LongAuthoredBoxProgramsKeepTheirCompleteDuration();
        AutomaticTimingAndArbitration(); AutomaticCompletionAndTeardown(); AutomaticWatchdog();
        std::cout << "Toy interaction: " << checks << " checks passed\n";
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
