#include "../src/cat_ball_physics.hpp"
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>

namespace {
int checks = 0;
#define CHECK(c) do { ++checks; if (!(c)) throw std::runtime_error( \
    std::string(__FILE__) + ":" + std::to_string(__LINE__) + " " #c); } while (false)
using cat::toy::PhysicsBall;
constexpr double dt = PhysicsBall::FixedStep;
constexpr double pi = 3.14159265358979323846;
bool Near(double a, double b, double tolerance = .001) { return std::abs(a-b) <= tolerance; }
void Run(PhysicsBall& ball, double duration, double step = dt, double low = 100, double high = 900) {
    for (int i = 0; i < static_cast<int>(std::lround(duration / step)); ++i) ball.Advance(step, low, high);
}
void FiniteAndBounded(const PhysicsBall& ball, double low = 100, double high = 900) {
    for (double number : {ball.x,ball.velocity,ball.lift,ball.verticalVelocity,ball.angle,ball.angularVelocity})
        CHECK(std::isfinite(number));
    CHECK(ball.x >= low-.6 && ball.x <= high+.6);
    CHECK(ball.lift >= 0 && ball.lift <= 1201);
}

void ActualEngineGravityBounceAndRest() {
    const auto engine = b2GetVersion();
    CHECK(engine.major == 3 && engine.minor == 1 && engine.revision == 1);
    CHECK(PhysicsBall::Radius == 12);
    PhysicsBall ball(500);
    Run(ball, 5);
    CHECK(ball.grounded && Near(ball.x,500,.1) && ball.lift < .6);
    CHECK(std::abs(ball.velocity) < .01 && std::abs(ball.verticalVelocity) < .01);
    ball.Reset(500,120,40,0);
    ball.Advance(.1,100,900);
    CHECK(ball.lift < 117 && ball.lift > 114);
    CHECK(ball.verticalVelocity < -80 && ball.verticalVelocity > -100);
    bool bounced = false;
    double bouncePeak = 0, impact = 0;
    for (int i = 0; i < 600; ++i) {
        const auto before = ball.CurrentPose();
        ball.Advance(dt,100,900);
        FiniteAndBounded(ball);
        if (!bounced && before.verticalVelocity < -100 && ball.verticalVelocity > 30) {
            bounced = true; impact = -before.verticalVelocity;
        }
        if (bounced) bouncePeak = std::max(bouncePeak,ball.lift);
    }
    CHECK(bounced && impact > 350 && impact < 500);
    CHECK(bouncePeak > 20 && bouncePeak < 45);
    CHECK(ball.grounded && ball.lift < .6);
    CHECK(std::abs(ball.velocity) < .01 && std::abs(ball.verticalVelocity) < .01);
    CHECK(std::abs(ball.angularVelocity) < .01);
}

void PawImpulseTranslatesSpinsAndSettles() {
    for (int direction : {-1,1}) {
        PhysicsBall ball(500);
        ball.Advance(0,100,900);
        ball.Kick(direction,90);
        CHECK(ball.x == 500 && ball.lift == 0); // Impulse, never a position animation.
        CHECK(Near(ball.velocity,direction*90));
        CHECK(ball.verticalVelocity > 85 && ball.verticalVelocity < 95);
        CHECK(ball.angularVelocity * direction < -1);
        double peak = 0;
        for (int i = 0; i < 360; ++i) {
            ball.Advance(dt,100,900);
            peak = std::max(peak,ball.lift);
            FiniteAndBounded(ball);
        }
        CHECK(peak > 3 && peak < 6);
        const double distance = (ball.x-500)*direction;
        CHECK(distance > 80 && distance < 170);
        CHECK(ball.grounded && std::abs(ball.velocity) < .01 && std::abs(ball.angularVelocity) < .01);
        CHECK(std::abs(ball.angle) > .1);
    }
}

void RealSideCollidersReboundAboveTheFloor() {
    for (int direction : {-1,1}) {
        PhysicsBall ball;
        ball.Reset(150,160,direction*300,0);
        bool rebounded = false;
        for (int i = 0; i < 60; ++i) {
            ball.Advance(dt,100,200);
            FiniteAndBounded(ball,100,200);
            if (ball.velocity*direction < -100 && ball.lift > 50) rebounded = true;
        }
        CHECK(rebounded); // The wall contact reverses velocity before floor contact.
        Run(ball, 10,dt,100,200);
        CHECK(ball.grounded && std::abs(ball.velocity) < .01);
        CHECK(ball.x > 101 && ball.x < 199); // Bounces away instead of sticking at the paw boundary.
    }
}

void FixedTimeStepsPauseAndInterpolation() {
    cat::toy::BallPose reference;
    for (double fps : {60.,30.,120.,144.}) {
        PhysicsBall ball;
        ball.Reset(500,80,140,80);
        Run(ball,2,1/fps);
        if (fps == 60) reference = ball.CurrentPose();
        CHECK(Near(ball.x,reference.x,.0001));
        CHECK(Near(ball.lift,reference.lift,.0001));
        CHECK(Near(ball.velocity,reference.velocity,.0001));
        CHECK(Near(ball.verticalVelocity,reference.verticalVelocity,.0001));
    }
    PhysicsBall whole, split;
    for (auto ball : {&whole,&split}) { ball->Reset(500,80,100,0);ball->Advance(0,100,900); }
    whole.Advance(.25,100,900);Run(split,.25);
    CHECK(Near(whole.x,split.x,.0001) && Near(whole.lift,split.lift,.0001));
    const auto frozen = whole.CurrentPose();
    for (int i = 0; i < 60; ++i) whole.Advance(.1,100,900,true);
    whole.Advance(10,100,900);
    CHECK(whole.x == frozen.x && whole.lift == frozen.lift && whole.angle == frozen.angle);
    whole.Advance(dt,100,900);
    CHECK(whole.x != frozen.x && whole.lift != frozen.lift);

    PhysicsBall spinning;
    spinning.Reset(500,200);spinning.Kick(1,400,0);
    bool crossedAngleSeam = false;
    for (int i = 0; i < 70; ++i) {
        spinning.Advance(dt,100,900);
        const auto current = spinning.CurrentPose(), previousRender = spinning.RenderPose();
        crossedAngleSeam = crossedAngleSeam || std::abs(current.angle-previousRender.angle) > pi;
        spinning.Advance(dt*.5,100,900);
        const auto midpoint = spinning.RenderPose();
        CHECK(Near(midpoint.x,(previousRender.x+current.x)*.5,.0001));
        const double delta = std::remainder(current.angle-previousRender.angle,2*pi);
        CHECK(std::abs(std::remainder(midpoint.angle-previousRender.angle-delta*.5,2*pi)) < 1e-6);
        spinning.Advance(dt*.5,100,900);
    }
    CHECK(crossedAngleSeam);
}

void BoundsChangesAndInvalidInputsStayFinite() {
    const double nan = std::numeric_limits<double>::quiet_NaN();
    PhysicsBall ball;
    ball.Reset(nan,nan,nan,nan);
    ball.Advance(.1,-100,100);
    CHECK(Near(ball.x,0,.01) && ball.grounded);
    const auto before = ball.CurrentPose();
    for (double invalid : {nan,-1.,.251,std::numeric_limits<double>::infinity()})
        ball.Advance(invalid,-100,100);
    ball.Kick(1,nan);ball.Kick(0,500);ball.Kick(1,100,nan);
    CHECK(ball.x == before.x && ball.velocity == before.velocity && ball.lift == before.lift);
    ball.Reset(-500,120,200,30);
    Run(ball,1,dt,-900,-100);
    FiniteAndBounded(ball,-900,-100);
    ball.Reset(500,100,100,40);
    ball.Advance(0,120,200); // Work-area resize only clamps an outside x.
    CHECK(Near(ball.x,200,.01) && Near(ball.lift,100,.01));
    CHECK(ball.velocity == 0 && Near(ball.verticalVelocity,40,.01));
    for (int i = 0; i < 600; ++i) { ball.Advance(dt,150,150);FiniteAndBounded(ball,150,150); }
    CHECK(ball.grounded && Near(ball.x,150,.6));
    ball.Advance(0,180,120); // Reversed bounds collapse to the same midpoint.
    CHECK(Near(ball.x,150,.6));
}
}

int main() {
    try {
        ActualEngineGravityBounceAndRest();
        PawImpulseTranslatesSpinsAndSettles();
        RealSideCollidersReboundAboveTheFloor();
        FixedTimeStepsPauseAndInterpolation();
        BoundsChangesAndInvalidInputsStayFinite();
        std::cout << "Box2D ball: " << checks << " checks passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n'; return 1;
    }
}
