#pragma once

#include <algorithm>
#include <cstdint>

namespace cat::random_zoomies {

struct Event {
    bool started=false,turn=false,finished=false;
    std::uint64_t runDurationMs=0;
};

// One pending opportunity, rather than a probability rolled every screen frame.
// The quiet gap begins after completion/interruption. Sleep/care may defer a due
// opportunity; hidden/menu/drag/paused time never accumulates a catch-up burst.
class Scheduler {
public:
    static constexpr std::uint64_t MinWaitMs=60000,MaxWaitMs=300000;
    static constexpr std::uint64_t MinRunMs=5000,MaxRunMs=15000;
    static constexpr std::uint64_t MinLegMs=1500,MaxLegMs=3000;
    static constexpr std::uint64_t MaxNoProgressMs=8000;
    void Reset() {*this=Scheduler{};}
    bool Active() const {return active_;}
    bool Due() const {return initialized_&&!active_&&waitMs_==0;}
    std::uint64_t WaitMs() const {return waitMs_;}
    std::uint64_t RemainingRunMs() const {return runMs_;}

    template<class Draw> Event Update(std::uint64_t elapsedMs,bool clockAllowed,bool mayStart,
                                      bool moving,bool owned,Draw draw) {
        Event event;
        if(!initialized_) {initialized_=true;Schedule(draw);return event;}
        // A care request, explicit command or other new owner wins immediately.
        if(active_&&!owned) {Schedule(draw);return event;}
        if(!clockAllowed)return event;
        // The host freezes long stalls too. Keep this safety at the boundary so
        // a new caller cannot consume minutes of running in a single update.
        elapsedMs=std::min<std::uint64_t>(elapsedMs,250);
        if(active_) {
            if(moving) {
                runMs_=Subtract(runMs_,elapsedMs);
                legMs_=Subtract(legMs_,elapsedMs);
                noProgressMs_=0;
            } else noProgressMs_+=elapsedMs;
            // No place to run (for example a very narrow work area): give up
            // without teleporting or holding the event indefinitely.
            if(!runMs_||noProgressMs_>=MaxNoProgressMs) {
                event.finished=true;event.runDurationMs=durationMs_;Schedule(draw);
            } else if(!legMs_) {
                event.turn=true;legMs_=draw(MinLegMs,MaxLegMs);
            }
        } else {
            waitMs_=Subtract(waitMs_,elapsedMs);
            if(!waitMs_&&mayStart) {
                active_=true;durationMs_=runMs_=draw(MinRunMs,MaxRunMs);
                legMs_=draw(MinLegMs,MaxLegMs);noProgressMs_=0;
                event.started=true;event.runDurationMs=durationMs_;
            }
        }
        return event;
    }
private:
    static std::uint64_t Subtract(std::uint64_t value,std::uint64_t delta) {return value>delta?value-delta:0;}
    template<class Draw> void Schedule(Draw draw) {
        active_=false;runMs_=legMs_=noProgressMs_=durationMs_=0;
        waitMs_=draw(MinWaitMs,MaxWaitMs);
    }
    bool initialized_=false,active_=false;
    std::uint64_t waitMs_=0,runMs_=0,legMs_=0,noProgressMs_=0,durationMs_=0;
};

} // namespace cat::random_zoomies
