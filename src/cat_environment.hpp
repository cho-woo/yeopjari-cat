#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace cat::environment {

enum class CursorAction { None, Watch, Hunt, Sniff, Paw, Abandon, ApproachSit, Sit };

struct CursorInput {
    std::uint64_t nowMs = 0;
    double cursorX = 0, cursorHeight = 0, catX = 0, minX = 0, maxX = 0;
    bool available = false, enabled = false, canStart = false, canContinue = false;
    // Interest controls response timing; a random draw only chooses rare sitting.
    double interest = .5, roll = .5;
};

struct CursorOutput {
    CursorAction action = CursorAction::None;
    bool changed = false;
    double velocity = 0;
    int direction = 1;
};

// Observes cursor samples only. It never owns input or asks the OS to move a
// cursor. The host applies the requested walking velocity through its motor.
class CursorReaction {
public:
    bool Active() const { return action_ != CursorAction::None; }
    CursorAction Action() const { return action_; }
    void Pause(std::uint64_t elapsedMs) {
        if (Active()) { began_ += elapsedMs; changedAt_ += elapsedMs; }
        if (stillTracked_) stillSince_ += elapsedMs;
        sampled_ = false;
        fastMs_ = 0;
    }
    void Cancel(std::uint64_t nowMs) {
        action_ = CursorAction::None;
        nextStart_ = nowMs + 12000;
        sampled_ = false;
        stillTracked_ = false;
        fastMs_ = 0;
    }

    CursorOutput Step(const CursorInput& input) {
        const auto before = action_;
        if (input.enabled && !enabled_) nextStart_ = input.nowMs;
        enabled_ = input.enabled;
        if (!Valid(input) || !input.enabled || !input.canContinue || !input.available) {
            if (Active()) Cancel(input.nowMs);
            sampled_ = false;
            stillTracked_ = false;
            return Result(input, before);
        }
        double speed = 0;
        double distanceMoved = 0;
        std::uint64_t delta = 0;
        if (sampled_) {
            if (input.nowMs < lastSample_ || input.nowMs - lastSample_ > 250) {
                Cancel(input.nowMs);
                return Result(input, before);
            }
            delta = input.nowMs - lastSample_;
            distanceMoved = std::hypot(input.cursorX - lastX_, input.cursorHeight - lastHeight_);
            if (delta) speed = distanceMoved * 1000 / delta;
        }
        sampled_ = true; lastSample_ = input.nowMs;
        lastX_ = input.cursorX; lastHeight_ = input.cursorHeight;
        if (!stillTracked_ || speed > 12 || std::hypot(input.cursorX - stillX_, input.cursorHeight - stillHeight_) > 4) {
            stillTracked_ = true; stillSince_ = input.nowMs;
            stillX_ = input.cursorX; stillHeight_ = input.cursorHeight;
        }
        const double distance = std::abs(input.cursorX - input.catX);
        if (distance > 330 || input.cursorHeight < -25 || input.cursorHeight > 240) {
            if (Active()) Cancel(input.nowMs);
            return Result(input, before);
        }
        if (Active() && action_ != CursorAction::Abandon) {
            fastMs_ = speed > 650 ? fastMs_ + delta : 0;
            if (distanceMoved > 180 || fastMs_ >= 100) Change(CursorAction::Abandon, input.nowMs);
            else if (input.nowMs - began_ >=
                    ((action_ == CursorAction::ApproachSit || action_ == CursorAction::Sit) ? 9000U : 7000U))
                Finish(input.nowMs);
        }
        if (!Active()) {
            if (!input.canStart || input.nowMs < nextStart_ || distance > 225) return Result(input, before);
            // Enabling cursor play is an explicit request for a response. A
            // personality lottery followed by an eight-second retry could
            // make low-curiosity animals ignore a nearby pointer for minutes.
            episodeInterest_ = std::clamp(input.interest, 0.0, 1.0);
            began_ = input.nowMs; fastMs_ = 0;
            Change(CursorAction::Watch, input.nowMs);
        } else {
            const auto elapsed = input.nowMs - changedAt_;
            const bool reachable = input.cursorHeight >= -8 && input.cursorHeight <= 130;
            switch (action_) {
            case CursorAction::Watch:
                if (elapsed >= static_cast<std::uint64_t>(650 + 400*(1-episodeInterest_)) && reachable && speed < 240) {
                    if (distance <= 48 && speed < 60) Change(CursorAction::Sniff, input.nowMs);
                    else if (distance > 48) Change(CursorAction::Hunt, input.nowMs);
                } else if (elapsed >= 2500) Finish(input.nowMs);
                break;
            case CursorAction::Hunt:
                if (!reachable || speed > 240) Change(CursorAction::Watch, input.nowMs);
                else if (distance <= 43 && speed < 60) Change(CursorAction::Sniff, input.nowMs);
                else if (elapsed >= 3300) Change(CursorAction::Abandon, input.nowMs);
                break;
            case CursorAction::Sniff:
                if (!reachable || distance > 60 || speed > 120) Change(CursorAction::Watch, input.nowMs);
                else if (elapsed >= 1050) {
                    if (input.nowMs >= nextSit_ && input.nowMs - stillSince_ >= 1200 &&
                            input.cursorHeight >= 0 && input.cursorHeight <= 55 && input.cursorX >= input.minX && input.cursorX <= input.maxX &&
                            input.roll < .12) {
                        seatX_ = input.cursorX; seatHeight_ = input.cursorHeight;
                        nextSit_ = input.nowMs + 180000;
                        Change(CursorAction::ApproachSit, input.nowMs);
                    } else Change(CursorAction::Paw, input.nowMs);
                }
                break;
            case CursorAction::ApproachSit:
                if (std::hypot(input.cursorX - seatX_, input.cursorHeight - seatHeight_) > 10 ||
                        seatX_ < input.minX || seatX_ > input.maxX || elapsed >= 1800)
                    Change(CursorAction::Abandon, input.nowMs);
                else if (std::abs(input.catX - seatX_) <= 2) Change(CursorAction::Sit, input.nowMs);
                break;
            case CursorAction::Sit:
                if (elapsed >= 2200 || std::hypot(input.cursorX - seatX_, input.cursorHeight - seatHeight_) > 10)
                    Finish(input.nowMs);
                break;
            case CursorAction::Paw:
                if (elapsed >= 1000) Finish(input.nowMs);
                break;
            case CursorAction::Abandon:
                if (elapsed >= 1100) Finish(input.nowMs);
                break;
            default: break;
            }
        }
        return Result(input, before);
    }

private:
    CursorAction action_ = CursorAction::None;
    std::uint64_t began_ = 0, changedAt_ = 0, nextStart_ = 0, lastSample_ = 0, fastMs_ = 0;
    std::uint64_t stillSince_ = 0, nextSit_ = 0;
    double lastX_ = 0, lastHeight_ = 0;
    double stillX_ = 0, stillHeight_ = 0, seatX_ = 0, seatHeight_ = 0;
    double episodeInterest_ = .5;
    bool sampled_ = false, stillTracked_ = false, enabled_ = false;
    int direction_ = 1;

    static bool Valid(const CursorInput& input) {
        return std::isfinite(input.cursorX) && std::isfinite(input.cursorHeight) &&
            std::isfinite(input.catX) && std::isfinite(input.minX) && std::isfinite(input.maxX) &&
            std::isfinite(input.interest) && std::isfinite(input.roll) && input.minX <= input.maxX;
    }
    void Change(CursorAction action, std::uint64_t now) { action_ = action; changedAt_ = now; }
    void Finish(std::uint64_t now) {
        action_ = CursorAction::None;
        nextStart_ = now + static_cast<std::uint64_t>(18000 - 6000*episodeInterest_);
    }
    CursorOutput Result(const CursorInput& input, CursorAction before) {
        CursorOutput output;
        output.action = action_; output.changed = before != action_;
        if (std::isfinite(input.cursorX) && std::isfinite(input.catX) &&
                std::abs(input.cursorX - input.catX) > 2)
            direction_ = input.cursorX < input.catX ? -1 : 1;
        output.direction = direction_;
        if (action_ == CursorAction::Hunt) {
            const double target = std::clamp(input.cursorX - direction_*32, input.minX, input.maxX);
            output.velocity = std::clamp((target - input.catX)*2.8, -45.0, 45.0);
        } else if (action_ == CursorAction::ApproachSit) {
            output.velocity = std::clamp((seatX_ - input.catX)*3.5, -35.0, 35.0);
        }
        return output;
    }
};

struct CursorMotionState {
    bool ownsMotion = true, waitingForStop = false;
    std::uint64_t motionStarted = 0;
};

// This is the production host adapter, shared with the Box2D integration tests.
// Action clocks may be rebased by the stop gate or pause handling; another
// action's clock/identity must instead cancel cursor control without replacing it.
class CursorSession {
public:
    bool Active() const { return planner_.Active(); }
    CursorAction Action() const { return planner_.Action(); }
    double Velocity() const { return velocity_; }
    void AdoptMotion(std::uint64_t started, bool waitingForStop) {
        motionStarted_ = started; wasWaiting_ = waitingForStop;
    }
    void Pause(std::uint64_t elapsedMs, const CursorMotionState& motion) {
        planner_.Pause(elapsedMs); velocity_ = 0;
        AdoptMotion(motion.motionStarted, motion.waitingForStop);
    }
    CursorOutput Step(CursorInput input, std::uint64_t elapsedMs, const CursorMotionState& motion) {
        if (Active() && wasWaiting_ && !motion.waitingForStop && motion.ownsMotion)
            motionStarted_ = motion.motionStarted;
        const bool owns = motion.ownsMotion && (motion.motionStarted == motionStarted_ ||
            (wasWaiting_ && motion.waitingForStop));
        if (Active() && !owns) {
            planner_.Cancel(input.nowMs); velocity_ = 0; wasWaiting_ = false;
            return {}; // Keep the interrupting action, including the same named action.
        }
        // OFF, missing cursor and manual/busy states are checked before the
        // braking pause. Otherwise toggling OFF could leave an active session.
        if (Active() && (!input.enabled || !input.canContinue || !input.available)) {
            const auto output = planner_.Step(input);
            velocity_ = 0; wasWaiting_ = false;
            return output;
        }
        if (Active() && motion.waitingForStop) {
            Pause(elapsedMs, motion);
            return {planner_.Action(), false, 0, direction_};
        }
        wasWaiting_ = false;
        const auto output = planner_.Step(input);
        velocity_ = output.velocity;
        direction_ = output.direction;
        return output;
    }
private:
    CursorReaction planner_;
    std::uint64_t motionStarted_ = 0;
    bool wasWaiting_ = false;
    double velocity_ = 0;
    int direction_ = 1;
};

inline double WindowJumpRiseLimit(double workAreaHeight, double jumpScale) {
    if (!std::isfinite(workAreaHeight) || !std::isfinite(jumpScale) || jumpScale <= 0) return 0;
    const double scale = std::clamp(jumpScale, .25, 1.2);
    const double headroom = std::max(0.0, workAreaHeight - 160 - 45*scale);
    return std::min(900.0, headroom) * scale;
}

// A broad finite ledge needs both a landing point and room to brake. Trying a
// deterministic small set of points allows the physics controller to reject
// unreachable hops without changing the existing support or body position.
template<class Reachable>
bool ChooseLanding(double catX, double left, double right, Reachable reachable, double& target) {
    if (!std::isfinite(catX) || !std::isfinite(left) || !std::isfinite(right) || right - left < 150) return false;
    const double low = left + 72, high = right - 72;
    const double candidates[] = {std::clamp(catX, low, high), (low + high)*.5,
        low, high, low + (high-low)*.25, low + (high-low)*.75};
    bool found = false;
    double bestDistance = 0;
    for (double candidate : candidates) {
        if (!reachable(candidate)) continue;
        const double distance = std::abs(candidate - catX);
        if (!found || distance < bestDistance) { target = candidate; bestDistance = distance; found = true; }
    }
    return found;
}

inline double BalanceStrength(double horizontalSpeed, double verticalSpeed) {
    if (!std::isfinite(horizontalSpeed) || !std::isfinite(verticalSpeed)) return 0;
    return std::clamp((std::hypot(horizontalSpeed, verticalSpeed) - 18) / 150, 0.0, 1.0);
}

} // namespace cat::environment
