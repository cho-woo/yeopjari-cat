#pragma once
#include "cat_animation.hpp"
#include <cmath>
#include <cstdint>

namespace cat::motion {
inline constexpr double kStoppedSpeed = .5;

// One per actor. The motor still belongs to Box2D; this gate changes only its
// request. Both simulated and displayed velocities must settle before a flip.
// Seconds are accepted simulation time, never wall time across menus/suspend.
class GroundDrive {
public:
    static constexpr double PlantSeconds=.18, TurnSeconds=.12, StartSeconds=.10;
    void Reset(int facing=1) { facing_=facing<0?-1:1; still_=PlantSeconds; hold_=0; turning_=false; }
    int Facing() const { return facing_; }
    bool Settled() const { return still_>=PlantSeconds; }
    double Update(double desired,double physical,double displayed,bool grounded,double seconds) {
        if(!std::isfinite(desired)||!std::isfinite(physical)||!std::isfinite(displayed)||
                !std::isfinite(seconds)||seconds<=0||seconds>.25) return 0;
        if(!grounded) {still_=0;hold_=0;turning_=false;return desired;}
        const bool moving=std::abs(physical)>kStoppedSpeed||std::abs(displayed)>kStoppedSpeed;
        if(moving) {still_=0; if(std::abs(displayed)>kStoppedSpeed)facing_=displayed<0?-1:1;}
        else still_=std::min(10.0,still_+seconds);
        const int requested=desired<0?-1:1;
        if(desired!=0 && requested!=facing_) {
            turning_=true;
            if(moving || !Settled()) return 0;
            facing_=requested;hold_=TurnSeconds;
        }
        if(hold_>0) {hold_=std::max(0.0,hold_-seconds);return 0;}
        if(turning_) {turning_=false;return desired;}
        // A stopped animal starts in the registered planted drawing. A change
        // of requested speed in the same direction never resets its step.
        if(desired!=0 && !moving && still_<StartSeconds) return 0;
        return desired;
    }
private:
    int facing_=1;
    double still_=PlantSeconds,hold_=0;
    bool turning_=false;
};

inline bool ReadyToStart(bool grounded, double velocityX) {
    return grounded && std::isfinite(velocityX) && std::abs(velocityX) <= kStoppedSpeed;
}

inline animation::Motion DisplayedMotion(animation::Motion requested,animation::Motion previousWalking,
                                        bool grounded,bool waiting,double relativeVelocity) {
    if (!grounded || !std::isfinite(relativeVelocity)) return requested;
    // Motor braking outlives the action that requested it, especially after
    // a pounce landing. Every planted translating body needs actual footfalls,
    // not only Idle/Watch. Motion clocks and saved state remain unchanged.
    if ((waiting || std::abs(relativeVelocity)>kStoppedSpeed) && !animation::Walking(requested))
        return animation::Walking(previousWalking)?previousWalking:animation::Motion::Walk;
    return requested;
}

inline bool NeedsStop(animation::Motion requested, bool grounded, double velocityX,
                      bool windowTransfer = false) {
    // A pounce travels only after its planted crouch/takeoff preparation.
    const bool stationaryStart = !animation::Travels(requested) || requested == animation::Motion::Pounce;
    return !windowTransfer && stationaryStart && grounded && !ReadyToStart(grounded, velocityX);
}

// Request ordinary motor braking before a finite ledge's safe walking edge.
// The 680px/s² limit matches grounded braking in the Box2D controller; 12px
// reserves its easing tail and the interval before the next rendered frame.
// Velocity and desired speed are relative to the supporting window. The
// caller owns grounded/travel checks and latches the resulting stop action.
inline bool BrakeBeforeEdge(double x, double velocity, double desired,
                            double low, double high) {
    if (!std::isfinite(x) || !std::isfinite(velocity) || !std::isfinite(desired) ||
            !std::isfinite(low) || !std::isfinite(high) || low > high) return false;
    const double distance = velocity * velocity / (2 * 680.0) + 12.0;
    if (desired < 0 && velocity <= 0) return x - low <= distance;
    if (desired > 0 && velocity >= 0) return high - x <= distance;
    return false;
}

// Move both clocks together, keeping the complete planned action duration.
// The caller brakes via Box2D while retaining the previous walking gait.
// Return true once when the pending action can begin on planted feet.
inline bool AdvanceStopGate(bool& waiting, bool grounded, double velocityX,
                            std::uint64_t ticks, std::uint64_t& started,
                            std::uint64_t& decision) {
    if (!waiting) return false;
    const auto frozen = ticks >= started ? ticks - started : 0;
    started += frozen;
    decision += frozen;
    if (!ReadyToStart(grounded, velocityX)) return false;
    waiting = false;
    return true;
}
} // namespace cat::motion
