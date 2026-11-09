#pragma once

#include <cmath>

namespace cat::authored_locomotion {

enum class Mode { Walk, Run };

// The registered artwork supplies stride lengths in logical pixels at the
// common 110 px adult reference. Hosts apply nutrition width to those lengths;
// input travel has already been divided by DPI and real-age growth.
// There is deliberately no independent animation-rate cap: slowing the paws
// while Box2D keeps moving would make the whole painted animal skate.
class Cycle {
public:
    static constexpr double RunEnterSpeed = 60;
    static constexpr double RunExitSpeed = 45;

    bool Configure(double walkStride, double runStride) {
        if (!ValidStride(walkStride) || !ValidStride(runStride)) return false;
        walkStride_ = walkStride;
        runStride_ = runStride;
        return true;
    }
    void Reset() {
        phase_ = 0;
        mode_ = Mode::Walk;
        stillSeconds_=0;hasWalked_=false;
    }
    bool Configured() const { return walkStride_ > 0 && runStride_ > 0; }
    double Phase() const { return phase_; }
    Mode CurrentMode() const { return mode_; }
    double Stride() const { return mode_ == Mode::Run ? runStride_ : walkStride_; }
    bool Standing() const {return hasWalked_ && stillSeconds_>=.10;}
    bool RecentWalk() const {return hasWalked_ && stillSeconds_<1.2;}

    bool Advance(double signedDistance, double seconds, bool walking, bool grounded) {
        if (!std::isfinite(signedDistance) || !std::isfinite(seconds) || seconds < 0 ||
                (seconds == 0 && signedDistance != 0)) {
            Reset();
            return false;
        }
        // Carrying a cat with its window, falling off a support, pauses and
        // blocked movement cannot consume a step or switch its painted gait.
        if (!Configured() || seconds == 0)
            return true;
        if (!grounded) {stillSeconds_=0;hasWalked_=false;return true;}
        const double distance = std::abs(signedDistance);
        const double speed = distance / seconds;
        if (!std::isfinite(speed)) { Reset(); return false; }
        if(speed<=.5) stillSeconds_=std::min(10.0,stillSeconds_+seconds);
        if(!walking || distance==0)return true;
        if(speed>.5) {
            if(stillSeconds_>=.18) {phase_=0;mode_=Mode::Walk;}
            stillSeconds_=0;hasWalked_=true;
        }
        if (mode_ == Mode::Walk && speed >= RunEnterSpeed) mode_ = Mode::Run;
        else if (mode_ == Mode::Run && speed <= RunExitSpeed) mode_ = Mode::Walk;
        const double advance = distance / Stride();
        if (!std::isfinite(advance)) { Reset(); return false; }
        phase_ = std::fmod(phase_ + advance, 1.0);
        if (phase_ > 1 - 1e-12) phase_ = 0;
        return true;
    }

private:
    static bool ValidStride(double value) {
        return std::isfinite(value) && value > 0 && value <= 16384;
    }
    double walkStride_ = 0, runStride_ = 0, phase_ = 0;
    double stillSeconds_=0;
    bool hasWalked_=false;
    Mode mode_ = Mode::Walk;
};
} // namespace cat::authored_locomotion
