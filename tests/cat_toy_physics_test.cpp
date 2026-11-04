#include "../src/cat_physics.hpp"
#include "../src/cat_toy.hpp"
#include "../src/cat_ball_physics.hpp"
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {
int checks = 0;
#define CHECK(condition) do { ++checks; if (!(condition)) throw std::runtime_error( \
    std::string(__FILE__) + ":" + std::to_string(__LINE__) + " failed: " #condition); } while (false)

// Use the actual Box2D motor and interpolated display position, matching Tick.
// This catches an approach controller that looks correct algebraically but
// oscillates or never satisfies the low-speed contact gate in the application.
void Approach(cat::toy::Kind kind, double fps, double start, double destination,
              double growth, double initialVelocity, bool movingBall) {
    constexpr double low = 96, high = 1824;
    const double dt = 1.0 / fps;
    cat::physics::Controller cat;
    cat.Reset(start, low, high);
    cat.SetTargetVelocity(initialVelocity);
    for (int i = 0; i < static_cast<int>(fps * .5); ++i) cat.Update(dt);
    cat::toy::Ball ball{destination, 0};
    if (movingBall) ball.Kick(start > destination ? -1 : 1, 90);
    double target = destination, distance = 0;
    bool arrived = false;
    for (int i = 0; i < static_cast<int>(fps * 60); ++i) {
        const double center = cat.RenderPose().x;
        target = cat::toy::ApproachTarget(kind, ball.x, center, 36 * growth, low, high);
        distance = target - center;
        if (std::abs(distance) <= 2 && std::abs(cat.CurrentPose().velocityX) <= 3) {
            arrived = true;
            break;
        }
        cat.SetTargetVelocity(cat::toy::ApproachVelocity(distance));
        if (kind == cat::toy::Kind::Ball) {
            const auto [ballLow, ballHigh] = cat::toy::RollingBounds(1920, low, high, 36 * growth);
            ball.Advance(dt, ballLow, ballHigh);
        }
        cat.Update(dt);
        CHECK(cat.CurrentPose().x >= low && cat.CurrentPose().x <= high);
        CHECK(cat.CurrentPose().grounded);
    }
    if (!arrived) throw std::runtime_error("Approach failed: fps=" + std::to_string(fps) +
        " start=" + std::to_string(start) + " target=" + std::to_string(destination));
    CHECK(arrived);
    CHECK(std::abs(cat.RenderPose().x - target) <= 2);
    CHECK(std::abs(cat.CurrentPose().velocityX) <= 3);
    if (kind == cat::toy::Kind::Ball)
        CHECK(std::abs(cat.RenderPose().x - ball.x) <= 55 * growth);
    else
        CHECK(std::abs(cat.RenderPose().x - destination) <= 2);
}

void FullActivity(cat::toy::Kind kind, double fps, double growth, double destination) {
    using namespace cat::toy;
    constexpr double low=96,high=1824;
    const double dt=1/fps;
    cat::physics::Controller physics;
    physics.Reset(300,low,high);
    PhysicsBall ball; ball.Reset(destination);
    Session session; session.Start(kind,destination<300 ? -1 : 1);
    Automatic automatic;
    AutoInput automaticInput; automaticInput.eligible=true;
    AutoDecision placement;
    const auto entropy=static_cast<std::uint32_t>(kind)-1;
    for (int tick=0;tick<1000 && placement.action==AutoAction::None;++tick)
        placement=automatic.Advance(.25,automaticInput,entropy);
    CHECK(placement.action==AutoAction::Place && placement.kind==kind);
    automaticInput.eligible=false; automaticInput.visibleKind=kind;
    bool consumed=false, sawEnter=false, sawSleep=false, sawExit=false;
    unsigned contacts=0;
    double maxStep=0, previous=physics.CurrentPose().x;
    double toyX=destination;
    for (int tick=0;tick<static_cast<int>(fps*90) && session.phase!=Phase::Wait;++tick) {
        automaticInput.phase=session.phase;
        if (automatic.Advance(dt,automaticInput,entropy).action!=AutoAction::None)
            throw std::runtime_error("Automatic item retired during its active session");
        const auto state=physics.CurrentPose();
        if (kind==Kind::Ball) {
            const auto bounds=RollingBounds(1920,low,high,36*growth);
            ball.Advance(dt,bounds.first,bounds.second);
            toyX=ball.x;
        }
        const auto target=[&] { return SessionTarget(session,toyX,physics.RenderPose().x,growth,low,high); };
        const bool arrived=std::abs(target()-physics.RenderPose().x)<=2 && std::abs(state.velocityX)<=3;
        const bool braking=!Travels(session.phase) && std::abs(state.velocityX)>2;
        if (session.Advance(dt,InteractionAllowed(state.grounded,state.onPlatform,false,braking),arrived)) {
            consumed=false;
            if (session.phase==Phase::Enter) sawEnter=true;
            if (session.phase==Phase::Sleep) sawSleep=true;
            if (session.phase==Phase::Exit) sawExit=true;
            if (session.phase==Phase::Interact && UsesCenter(kind))
                CHECK(std::abs(state.x-toyX)<=2.5);
        }
        if (kind==Kind::Ball && session.phase==Phase::Interact &&
                PawContactAt(static_cast<std::uint64_t>(session.elapsedMs),consumed,toyX-state.x,
                             55*growth,state.grounded && ball.lift<=24*growth)) {
            const auto before=ball.x;
            ball.Kick(toyX<state.x ? -1 : 1,90);
            CHECK(ball.x==before); // A visible contact supplies impulse, never a position correction.
            ++contacts;
        }
        const double speed=session.phase==Phase::Enter ? 22 : kind==Kind::Ball ? 58 : 38;
        physics.SetTargetVelocity(Travels(session.phase) ? ApproachVelocity(target()-physics.RenderPose().x,speed) : 0);
        physics.Update(dt);
        maxStep=std::max(maxStep,std::abs(physics.CurrentPose().x-previous));
        previous=physics.CurrentPose().x;
    }
    CHECK(session.phase==Phase::Wait);
    CHECK(maxStep<=60*std::max(1/fps,1.0/60)+.1); // At most one bounded fixed-step move, never a Reset/teleport.
    if (kind==Kind::Ball) CHECK(contacts==4);
    else {
        CHECK(sawExit);
        CHECK(std::abs(physics.CurrentPose().x-destination)>=50*growth);
        CHECK(sawEnter==IsContainer(kind));
        CHECK(sawSleep==(kind==Kind::Box || kind==Kind::Cushion));
    }
    // The automatic lifecycle must wait for a real Box2D ball to finish moving,
    // while containers retire only after the physical exit has completed.
    automaticInput.phase=Phase::Wait;
    AutoDecision retirement;
    const double catAfterExit=physics.CurrentPose().x;
    for (int tick=0;tick<static_cast<int>(fps*12) && retirement.action==AutoAction::None;++tick) {
        automaticInput.ballSettled=ball.grounded && std::abs(ball.velocity)<.5 &&
            std::abs(ball.verticalVelocity)<.5;
        retirement=automatic.Advance(dt,automaticInput,entropy);
        if (retirement.action==AutoAction::None && kind==Kind::Ball) {
            const auto bounds=RollingBounds(1920,low,high,36*growth);
            ball.Advance(dt,bounds.first,bounds.second);
        }
    }
    CHECK(retirement.action==AutoAction::Retire && retirement.kind==kind && !retirement.timedOut);
    CHECK(kind!=Kind::Ball || automaticInput.ballSettled);
    CHECK(physics.CurrentPose().x==catAfterExit); // Lifecycle retirement never relocates the cat.
    CHECK(automatic.RemainingSeconds()>=180 && automatic.ActiveKind()==Kind::None);
}

void PauseAndRaisedSupport() {
    using namespace cat::toy;
    cat::physics::Controller physics;
    physics.Reset(300,96,1824);
    PhysicsBall ball; ball.Reset(340);
    Session session; session.Start(Kind::Ball); session.Set(Phase::Interact);
    bool contact=false;
    const auto before=session;
    CHECK(!session.Advance(.1,InteractionAllowed(true,false,true,false),true));
    CHECK(!PawContactAt(350,contact,40,55,InteractionAllowed(true,false,true,false)));
    CHECK(!PawContactAt(350,contact,40,55,InteractionAllowed(true,true,false,false)));
    ball.Advance(.1,60,1860,true);
    CHECK(ball.x==340 && ball.velocity==0);
    CHECK(session.phase==before.phase && session.elapsedMs==before.elapsedMs);
    CHECK(!contact);
    CHECK(physics.Jump());
    physics.Update(1.0/60);
    CHECK(!InteractionAllowed(physics.CurrentPose().grounded,false,false,false));
    CHECK(!PawContactAt(350,contact,40,55,physics.CurrentPose().grounded));
}
}

int main() {
    try {
        for (double fps : {30.0, 60.0, 120.0})
            for (auto kind : {cat::toy::Kind::Ball, cat::toy::Kind::Box})
                for (double growth : {.82, 1.0}) {
                    Approach(kind, fps, 500, 700, growth, 60, false);
                    Approach(kind, fps, 800, 600, growth, -60, false);
                    Approach(kind, fps, 500, 96, growth, 110, false);
                    Approach(kind, fps, 1300, 1824, growth, -110, false);
                    Approach(kind, fps, 700, 700, growth, 0, false);
                    if (kind == cat::toy::Kind::Ball) {
                        Approach(kind, fps, 500, 750, growth, 60, true);
                        Approach(kind, fps, 130, 96, growth, 0, true);
                        Approach(kind, fps, 1790, 1824, growth, 0, true);
                    }
                }
        for (double fps:{30.0,60.0,120.0})
            for (double growth:{.82,1.0}) {
                FullActivity(cat::toy::Kind::Ball,fps,growth,440);
                FullActivity(cat::toy::Kind::Ball,fps,growth,1815);
                FullActivity(cat::toy::Kind::Box,fps,growth,440);
                FullActivity(cat::toy::Kind::PaperBag,fps,growth,110);
                FullActivity(cat::toy::Kind::Catnip,fps,growth,440);
                FullActivity(cat::toy::Kind::Cushion,fps,growth,440);
            }
        PauseAndRaisedSupport();
        std::cout << "Toy/Box2D approach: " << checks << " checks passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
