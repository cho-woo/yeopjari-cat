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
}
}

int main() {
    try {
        ReachableTargets(); ContactAndContinuousRoll(); EdgesRemainReachable(); FrameRateAndPause();
        RealContactGate(); DistinctActivitiesAndInterruptions(); ContainerPathsAndFeather();
        std::cout << "Toy interaction: " << checks << " checks passed\n";
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
