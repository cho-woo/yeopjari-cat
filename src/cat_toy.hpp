#pragma once

#include "cat_sprite_sequence.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <utility>

namespace cat::toy {

// Transient interaction state only; this module never changes pet save records.
enum class Kind { None, Ball, Box, PaperBag, Catnip, FishingRod, Cushion };

// Keep the stored IDs and archived container geometry readable, while runtime
// placement and automatic selection exclude both retired container events.
inline bool Retired(Kind kind) { return kind==Kind::Box||kind==Kind::PaperBag; }
inline bool IsContainer(Kind kind) { return kind == Kind::Box || kind == Kind::PaperBag; }
inline bool UsesCenter(Kind kind) {
    return IsContainer(kind) || kind == Kind::Catnip || kind == Kind::Cushion;
}

// Narrow work areas can invert the requested margins. Collapse them to their
// midpoint instead of invoking std::clamp with an invalid lower/upper pair.
inline double ClampCenter(double x, double low, double high) {
    if (low > high) return low + (high - low) * .5;
    return std::clamp(x, low, high);
}

// Ball centers stay both on screen and within paw reach of the cat's allowed
// center positions. All arguments/results use work-area logical coordinates.
inline std::pair<double, double> RollingBounds(double workWidth, double minCenter,
                                               double maxCenter, double reach = 36.0,
                                               double ballRadius = 14.0) {
    workWidth = std::max(0.0, workWidth);
    reach = std::max(0.0, reach);
    const double radius = std::clamp(ballRadius, 0.0, workWidth * .5);
    if (minCenter > maxCenter) minCenter = maxCenter = minCenter + (maxCenter - minCenter) * .5;
    const double low = std::max(radius, minCenter - reach);
    const double high = std::min(workWidth - radius, maxCenter + reach);
    if (low <= high) return {low, high};
    const double center = std::clamp(minCenter + (maxCenter - minCenter) * .5,
                                     radius, workWidth - radius);
    return {center, center};
}

inline double ApproachTarget(Kind kind, double toyX, double catX, double reach,
                             double low, double high) {
    if (kind == Kind::None) return ClampCenter(catX, low, high);
    if (UsesCenter(kind)) return ClampCenter(toyX, low, high);
    reach = std::max(0.0, reach);
    const double preferred = toyX + (catX > toyX ? reach : -reach);
    const double alternate = toyX + (catX > toyX ? -reach : reach);
    if (preferred >= low && preferred <= high) return preferred;
    if (alternate >= low && alternate <= high) return alternate;
    return ClampCenter(preferred, low, high);
}

inline double ApproachVelocity(double distance, double maxSpeed = 38.0,
                               double acceleration = 300.0) {
    if (!std::isfinite(distance) || !std::isfinite(maxSpeed) ||
        !std::isfinite(acceleration) || maxSpeed <= 0 || acceleration <= 0 ||
        std::abs(distance) <= 2.0) return 0.0;
    // Brake before the destination instead of overshooting and alternating sides.
    const double speed = std::min(maxSpeed, std::sqrt(2 * acceleration * (std::abs(distance) - 2.0)));
    return std::copysign(speed, distance);
}

inline constexpr std::uint64_t kPawContactMs = cat::sprite_sequence::kToyPawContactMs;
inline constexpr std::uint64_t kPawContactEndMs = cat::sprite_sequence::kToyPawReleaseMs;

// The active paw phase starts at 330ms. One action can impulse a ball only
// once; a skipped contact window must not kick it after the paw has retracted.
inline bool PawContact(std::uint64_t elapsed, bool& consumed) {
    if (consumed || elapsed < kPawContactMs) return false;
    consumed = true;
    return elapsed < kPawContactEndMs;
}
inline bool PawContactAt(std::uint64_t elapsed, bool& consumed, double distance,
                         double reach, bool available) {
    if (!available || !std::isfinite(distance) || !std::isfinite(reach) || reach < 0) return false;
    if (elapsed >= kPawContactEndMs) { consumed = true; return false; }
    if (std::abs(distance) > reach) return false;
    return PawContact(elapsed, consumed);
}

// A placed item owns a short, interruptible activity rather than a long random
// cooldown. Time advances only while the cat can interact on the taskbar floor.
// Phases needing travel finish on real position AND low-speed arrival supplied
// by the caller. No phase sets a cat position or resets a physics world.
enum class Phase { None, Discover, Approach, Inspect, Enter, Interact, Rest, Sleep, Exit, Wait, BoxExit };

inline bool Travels(Phase phase) {
    return phase == Phase::Approach || phase == Phase::Enter || phase == Phase::Exit;
}
inline bool InteractionAllowed(bool grounded, bool onPlatform, bool paused, bool busy) {
    return grounded && !onPlatform && !paused && !busy;
}
inline double Duration(Kind kind, Phase phase) {
    switch (phase) {
    case Phase::Discover: return 250;
    case Phase::Inspect: return kind == Kind::Ball ? 150 : kind == Kind::FishingRod ? 350 : 700;
    case Phase::Interact:
        switch (kind) {
        case Kind::Ball: return 1050;
        case Kind::FishingRod: return 3300;
        case Kind::Box: return 1700;
        case Kind::PaperBag: return 2800;
        case Kind::Catnip: return 4300;
        case Kind::Cushion: return 5000;
        default: return 0;
        }
    case Phase::Rest: return kind == Kind::PaperBag ? 1300 : 1700;
    case Phase::Sleep: return kind == Kind::Cushion ? 12000 : 9000;
    case Phase::Wait:
        switch (kind) {
        case Kind::Ball: return 2500;
        case Kind::FishingRod: return 1800;
        case Kind::Box: return 12000;
        case Kind::PaperBag: return 6500;
        case Kind::Catnip: return 7000;
        case Kind::Cushion: return 15000;
        default: return 0;
        }
    default: return 0;
    }
}

struct Session {
    Kind kind = Kind::None;
    Phase phase = Phase::None;
    double elapsedMs = 0, activeMs = 0;
    double boxVisitMs = 0; // Optional authored visit; zero retains the legacy timing.
    double boxExitMs = 0; // Optional stationary authored outro before ordinary exit travel.
    unsigned rounds = 0, revision = 0;
    int entryDirection = 1;

    void Start(Kind selected, int direction = 1) {
        kind = selected; entryDirection = direction < 0 ? -1 : 1;
        rounds = 0; elapsedMs = activeMs = boxVisitMs = boxExitMs = 0;
        phase = selected == Kind::None ? Phase::None : Phase::Discover;
        ++revision;
    }
    void Cancel() { Start(Kind::None); }
    void Set(Phase next) { phase = next; elapsedMs = 0; ++revision; }
    double PhaseDuration() const {
        if (kind==Kind::Box && phase==Phase::BoxExit && std::isfinite(boxExitMs) && boxExitMs>0)
            return std::clamp(boxExitMs,1.0,static_cast<double>(cat::sprite_sequence::kMaxProgramMs));
        return kind==Kind::Box && phase==Phase::Interact && std::isfinite(boxVisitMs) && boxVisitMs>0 ?
            std::clamp(boxVisitMs,Duration(kind,phase),static_cast<double>(cat::sprite_sequence::kMaxProgramMs)):
            Duration(kind,phase);
    }
    bool Advance(double seconds, bool enabled, bool arrived) {
        if (!enabled || kind == Kind::None || phase == Phase::None || !std::isfinite(seconds) ||
                seconds <= 0 || seconds > .25) return false;
        activeMs += seconds * 1000;
        elapsedMs += seconds * 1000;
        if (Travels(phase) && !arrived) return false;
        if (!Travels(phase) && elapsedMs + 1e-6 < PhaseDuration()) return false;
        switch (phase) {
        case Phase::Discover: Set(Phase::Approach); break;
        case Phase::Approach: Set(Phase::Inspect); break;
        case Phase::Inspect: Set(IsContainer(kind) ? Phase::Enter : Phase::Interact); break;
        case Phase::Enter: Set(Phase::Interact); break;
        case Phase::Interact:
            if (kind == Kind::Ball || kind == Kind::FishingRod) {
                ++rounds;
                Set(rounds < 4 ? Phase::Approach : Phase::Wait);
            } else if (kind == Kind::Box || kind == Kind::Cushion) Set(Phase::Sleep);
            else Set(Phase::Rest);
            break;
        case Phase::Rest: case Phase::Sleep:
            Set(kind==Kind::Box && std::isfinite(boxExitMs) && boxExitMs>0 ? Phase::BoxExit : Phase::Exit);
            break;
        case Phase::BoxExit: Set(Phase::Exit); break;
        case Phase::Exit: Set(Phase::Wait); break;
        case Phase::Wait: rounds = 0; Set(Phase::Discover); break;
        default: return false;
        }
        return true;
    }
};

enum class AutoAction { None, Place, Retire };
struct AutoDecision {
    AutoAction action = AutoAction::None;
    Kind kind = Kind::None;
    bool timedOut = false;
};
struct AutoInput {
    bool paused = false;
    // Host arbitration: calm, on the floor, and no other activity owns the cat.
    bool eligible = false;
    Kind visibleKind = Kind::None;
    Phase phase = Phase::None;
    bool ballSettled = true;
    // A rare event may temporarily borrow the object. Never remove it while
    // that event owns it, even when our ordinary activity has finished.
    bool protectedUse = false;
};

// Automatic toys use active application time, never UTC or save records.
// A placement owns one complete Session. Afterwards the item disappears and
// the ordinary behavior scheduler has several minutes before another item.
// The host supplies entropy, so this helper never disturbs another RNG itself.
class Automatic {
public:
    static constexpr double MaximumActiveSeconds = 120;

    AutoDecision Advance(double seconds, const AutoInput& input, std::uint32_t entropy) {
        if (input.paused || !std::isfinite(seconds) || seconds <= 0 || seconds > .25) return {};
        if (!initialized_) Schedule(true, entropy);
        if (active_ == Kind::None) {
            remaining_ = std::max(0.0, remaining_ - seconds);
            if (remaining_ > 0 || !input.eligible || input.visibleKind != Kind::None) return {};
            active_ = Choose(entropy);
            activeSeconds_ = 0;
            completed_ = expired_ = false;
            return {AutoAction::Place, active_, false};
        }
        if (input.visibleKind != active_) {
            // Placement failed, a monitor reset removed it, or another owner
            // replaced it. Do not replay or delete somebody else's object.
            Schedule(false, entropy);
            return {};
        }
        activeSeconds_ += seconds;
        completed_ = completed_ || input.phase == Phase::Wait;
        expired_ = expired_ || activeSeconds_ + 1e-9 >= MaximumActiveSeconds;
        const bool ready = expired_ || (completed_ && (active_ != Kind::Ball || input.ballSettled));
        if (!ready || input.protectedUse) return {};
        const AutoDecision result{AutoAction::Retire, active_, expired_};
        Schedule(false, entropy);
        return result;
    }

    Kind ActiveKind() const { return active_; }
    bool WaitingToRetire() const { return completed_ || expired_; }
    double RemainingSeconds() const { return remaining_; }
    double ActiveSeconds() const { return activeSeconds_; }

    static Kind Choose(std::uint32_t entropy) {
        constexpr Kind catalog[]{Kind::Ball,Kind::Catnip,Kind::FishingRod,Kind::Cushion};
        return catalog[entropy % 4];
    }
    static double Delay(bool first, std::uint32_t entropy) {
        const double fraction = static_cast<double>(entropy) / 4294967295.0;
        return first ? 90 + 150 * fraction : 180 + 240 * fraction;
    }

private:
    bool initialized_ = false, completed_ = false, expired_ = false;
    Kind active_ = Kind::None;
    double remaining_ = 0, activeSeconds_ = 0;

    void Schedule(bool first, std::uint32_t entropy) {
        initialized_ = true;
        active_ = Kind::None;
        completed_ = expired_ = false;
        remaining_ = Delay(first, entropy);
        activeSeconds_ = 0;
    }
};

inline double SessionTarget(const Session& session, double toyX, double catX, double growth,
                            double low, double high) {
    growth = std::max(.1, growth);
    if (session.phase == Phase::BoxExit) return ClampCenter(toyX, low, high);
    if(session.kind==Kind::FishingRod)return ClampCenter(toyX,low,high);
    if (session.phase == Phase::Exit) {
        const double preferred = toyX - session.entryDirection * 100 * growth;
        const double alternate = toyX + session.entryDirection * 100 * growth;
        if (std::abs(ClampCenter(preferred, low, high) - toyX) >= 50 * growth)
            return ClampCenter(preferred, low, high);
        return ClampCenter(alternate, low, high);
    }
    if (session.phase == Phase::Approach && IsContainer(session.kind))
        return ClampCenter(toyX - session.entryDirection * 68 * growth, low, high);
    return ApproachTarget(session.kind, toyX, catX, 36 * growth, low, high);
}

// The fishing feather is attached to a damped spring. A paw impulse moves the
// feather, not the cat; it remains within a short, physically reachable arc.
struct Lure {
    double offset = 0, velocity = 0;
    void Touch(int direction) { velocity = (direction < 0 ? -1 : 1) * 90; }
    void Advance(double seconds, bool paused = false) {
        if (paused || !std::isfinite(seconds) || seconds <= 0 || seconds > .25) return;
        const int steps = std::max(1, static_cast<int>(std::ceil(seconds * 240)));
        const double step = seconds / steps;
        for (int index = 0; index < steps; ++index) {
            velocity += (-18 * offset - 3.5 * velocity) * step;
            offset += velocity * step;
            if (std::abs(offset) > 26) { offset = std::copysign(26.0, offset); velocity *= -.35; }
        }
    }
};

struct Ball {
    double x = 0.0;
    double velocity = 0.0;
    static constexpr double friction = 2.8;

    void Kick(int direction, double speed) {
        if (!direction || !std::isfinite(speed) || speed <= 0) return;
        velocity = (direction < 0 ? -1 : 1) * std::min(speed, 500.0);
    }

    void Advance(double deltaSeconds, double low, double high, bool paused = false) {
        if (paused || !std::isfinite(deltaSeconds) || deltaSeconds <= 0 || deltaSeconds > .25) return;
        if (!std::isfinite(x) || !std::isfinite(velocity)) {
            x = ClampCenter(0.0, low, high); velocity = 0; return;
        }
        // Analytic exponential friction keeps 30/60/120Hz motion identical.
        const double decay = std::exp(-friction * deltaSeconds);
        const double next = x + velocity * (-std::expm1(-friction * deltaSeconds)) / friction;
        x = ClampCenter(next, low, high);
        velocity *= decay;
        if (x != next || std::abs(velocity) < .05) velocity = 0;
    }
};

} // namespace cat::toy
