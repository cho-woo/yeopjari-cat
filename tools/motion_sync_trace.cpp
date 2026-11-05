// Real production Controller/Locomotion trace. This is a diagnostic, not a
// replacement physics model or a rendered-cat/native-Windows validation.
#include "../src/cat_physics.hpp"
#include "../src/cat_animation.hpp"
#include "../src/cat_render_placement.hpp"
#include <cmath>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>

namespace {
using cat::animation::Motion;
struct Trace {
    cat::physics::Controller body;
    cat::animation::Locomotion legacy, synchronized;
    std::string scene;
    double time = 0;
    unsigned frame = 0;
    explicit Trace(std::string name) : scene(std::move(name)) { body.Reset(500, 0, 5000); }
    static std::pair<double,unsigned> ContactError(const cat::gait::Feet& before,
            const cat::gait::Feet& after,double distance) {
        double error=0;unsigned checks=0;
        for(unsigned leg=0;leg<before.size();++leg) {
            if(!before[leg].stance||!after[leg].stance||before[leg].contact!=after[leg].contact)continue;
            ++checks;
            error=std::max(error,std::abs(after[leg].x-before[leg].x+std::abs(distance)));
        }
        return {error,checks};
    }
    void Sample(double seconds, bool paused = false, Motion motion = Motion::Walk) {
        const auto before = body.CurrentPose(), renderBefore = body.RenderPose();
        const auto oldFeet=legacy.Feet(motion),newFeet=synchronized.Feet(motion);
        body.Update(paused ? 0 : seconds, paused);
        const auto current = body.CurrentPose(), render = body.RenderPose();
        const double oldDistance = current.x-before.x-(current.supportTravelX-before.supportTravelX);
        const double newDistance = render.relativeTravelX - renderBefore.relativeTravelX;
        if (!legacy.Advance(motion, oldDistance, paused ? 0 : seconds) ||
            !synchronized.Advance(motion, newDistance, paused ? 0 : seconds))
            throw std::runtime_error("Trace passed a discontinuity to locomotion");
        const auto placement = cat::render::BuildPlacement(900, 300, 1.25, render.lift, 0, 1.25);
        const auto oldContact=ContactError(oldFeet,legacy.Feet(motion),oldDistance);
        const auto newContact=ContactError(newFeet,synchronized.Feet(motion),newDistance);
        // Adaptive stride cannot be inverted as phase*24. Report the actual
        // sampled distances and independently verify planted-foot travel.
        const double oldTravel=cat::animation::Walking(motion)?std::abs(oldDistance):0;
        const double newTravel=cat::animation::Walking(motion)?std::abs(newDistance):0;
        time += seconds;
        std::cout << scene << ',' << frame++ << ',' << time << ',' << seconds << ',' << paused << ','
            << current.x << ',' << render.x << ',' << render.lift << ',' << render.velocityX << ','
            << render.supportVelocityX << ',' << (render.x-renderBefore.x) << ',' << newDistance << ','
            << oldTravel << ',' << newTravel << ','
            << legacy.Phase() << ',' << synchronized.Phase() << ',' << render.grounded << ',' << render.onPlatform << ','
            << placement.windowTop + placement.localBottom << ',' << placement.screenFeetY-1 << ','
            << 899-render.lift*1.25 << ',' << oldContact.first << ',' << newContact.first << ','
            << newContact.second << '\n';
    }
};
void Walking(const std::string& name, double dt) {
    Trace trace(name); trace.body.SetTargetVelocity(120);
    for (int i=0;i<360;++i) trace.body.Update(1./120);
    for (int i=0;i<120;++i) trace.Sample(dt);
}
void Support(bool walking) {
    Trace trace(walking ? "moving_support_walk_120hz" : "moving_support_rest_120hz");
    trace.body.SetPlatform(100,3500,180);
    if (!trace.body.LaunchTo(500,180)) throw std::runtime_error("Cannot reach trace platform");
    for (int i=0;i<360;++i) trace.body.Update(1./120);
    if (!trace.body.OnPlatform()) throw std::runtime_error("Trace did not land on platform");
    trace.body.SetTargetVelocity(walking ? 45 : 0);
    for (int i=1;i<=480;++i) {
        const double offset=i*(80./120);
        trace.body.SetPlatform(100+offset,3500+offset,180);
        if(i<=360) trace.body.Update(1./120);
        else trace.Sample(1./120);
    }
}
void Pause() {
    Trace trace("halfstep_pause_120hz"); trace.body.SetTargetVelocity(320);
    for (int i=0;i<241;++i) trace.body.Update(1./120); // Half of a fixed step remains.
    trace.Sample(0); // Record the exact visible location before the menu opens.
    for (int i=0;i<60;++i) trace.Sample(1./120,true);
    for (int i=0;i<60;++i) trace.Sample(1./120);
}
void Jump() {
    Trace trace("jump_120hz");
    if (!trace.body.Jump(55)) throw std::runtime_error("Cannot jump in trace");
    for (int i=0;i<110;++i) trace.Sample(1./120,false,Motion::Idle);
}
}
int main() {
    try {
        std::cout << std::setprecision(12)
            << "scenario,frame,time,dt,paused,current_x,render_x,render_lift,render_vx,support_vx,render_dx,relative_render_dx,legacy_gait_distance,synchronized_gait_distance,legacy_phase,synchronized_phase,grounded,on_platform,placed_y,legacy_rounded_y,expected_y,legacy_contact_error,synchronized_contact_error,synchronized_contact_checks\n";
        Walking("walk_120hz",1./120); Walking("walk_16ms",.016);
        Support(false); Support(true); Pause(); Jump();
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
