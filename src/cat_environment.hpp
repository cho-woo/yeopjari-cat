#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace cat::environment {

enum class CursorAction { None, Watch, Hunt, Sniff, Paw, Abandon, ApproachSit, Sit, Punch, Chase, Pounce, Sneak, Tiptoe, BothPaws, Rest };

struct CursorInput {
    std::uint64_t nowMs = 0;
    double cursorX = 0, cursorHeight = 0, catX = 0, minX = 0, maxX = 0;
    bool available = false, enabled = false, canStart = false, canContinue = false;
    bool grounded = true;
    // Pointer motion is measured in desktop coordinates, independently of a
    // moving support. cursorHeight remains relative for physical reach checks.
    double supportHeight = 0;
    double sizeScale = 1;
    double pawStandOff = 24;
    double movementScale = 1;
    // Interest controls response timing; one episode draw chooses rare sneaking.
    // Sitting uses a separate draw after an ordinary sniff.
    double interest = .5, roll = .5;
    // The host supplies the authored playlist length when this character has
    // one. Legacy pointer animations keep their original timings by default.
    std::uint64_t pawDurationMs = 850, punchDurationMs = 650;
    std::uint64_t pouncePreparationMs = 240;
    bool repeatNearby=false,canApproach=true;
    bool pursuePointer=false;
    bool canAttack=true;
    double catVelocityX=0;
    std::uint64_t landingRecoveryMs=1240,tiptoeDurationMs=1400,bothPawsDurationMs=1550;
};

inline std::uint64_t CursorAttackDuration(CursorAction action,const CursorInput& input) {
    const auto duration=action==CursorAction::Paw?input.pawDurationMs:input.punchDurationMs;
    return duration?std::min<std::uint64_t>(duration,86400000):
        action==CursorAction::Paw?850:650;
}
inline std::uint64_t CursorPounceTimeout(const CursorInput& input) {
    // Retain the legacy1160ms grace after expected takeoff, but do not time
    // out an expanded anticipation strip before Box2D receives its impulse.
    const auto preparation=input.pouncePreparationMs?
        std::min<std::uint64_t>(input.pouncePreparationMs,86400000):240;
    return preparation+1160;
}
inline std::uint64_t CursorEpisodeMinimum(const CursorInput& input) {
    // A longer source stroke must leave room for the following punch/chase
    // and real pounce, rather than spending their entire episode allowance.
    return std::min<std::uint64_t>(60000,CursorAttackDuration(CursorAction::Paw,input)+
        CursorAttackDuration(CursorAction::Punch,input)+CursorPounceTimeout(input)+3500);
}

struct CursorOutput {
    CursorAction action = CursorAction::None;
    bool changed = false;
    double velocity = 0;
    int direction = 1;
    double targetX = 0;
};

// Observes cursor samples only. It never owns input or asks the OS to move a
// cursor. The host applies the requested walking velocity through its motor.
class CursorReaction {
public:
    bool Active() const { return action_ != CursorAction::None; }
    CursorAction Action() const { return action_; }
    bool InFlight() const { return action_ == CursorAction::Pounce && launched_; }
    bool ContactAllowed() const { return contactAllowed_; }
    double TargetX() const { return pounceX_; }
    double TargetHeight() const { return pounceHeight_; }
    double Excitement() const { return excitement_; }
    void ConfirmLaunch(bool launched) { launchAnswered_ = true; launched_ = launched; }
    void Pause(std::uint64_t elapsedMs) {
        if (Active()) { began_ += elapsedMs; changedAt_ += elapsedMs; }
        if (stillTracked_) stillSince_ += elapsedMs;
        sampled_ = false;
        fastMs_ = 0;
        if (lastTease_) lastTease_ += elapsedMs;
    }
    void Cancel(std::uint64_t nowMs,std::uint64_t cooldownMs=12000) {
        action_ = CursorAction::None;
        nextStart_ = nowMs + cooldownMs;
        sampled_ = false;
        stillTracked_ = false;
        fastMs_ = 0;
        excitement_ = 0;
        launchAnswered_ = launched_ = false;
        contactAllowed_ = false;
    }

    CursorOutput Step(const CursorInput& input) {
        if(input.pursuePointer)return StepPursuit(input);
        if(input.repeatNearby)return StepNearby(input);
        const auto before = action_;
        const double sampledHeight = input.cursorHeight + input.supportHeight;
        if (input.enabled && !enabled_) nextStart_ = input.nowMs;
        enabled_ = input.enabled;
        if (InFlight()) {
            // Losing interest or turning OFF cannot revoke a physical impulse.
            // Keep the motor idle until real contact; never retarget in flight.
            if (!Valid(input) || !input.enabled || !input.available || !input.canContinue ||
                    std::abs(input.cursorX-input.catX) > 330 || input.cursorHeight > 240)
                contactAllowed_ = false;
            if (sampled_ && input.nowMs > lastSample_) {
                const auto elapsed = input.nowMs-lastSample_;
                const double moved = std::hypot(input.cursorX-lastX_,sampledHeight-lastHeight_);
                // World-height sampling excludes the cat's own physical jump.
                // A fast departure revokes contact without changing its impulse.
                if (elapsed > 250 || moved > 180 || moved*1000/elapsed > 650)
                    contactAllowed_ = false;
            }
            sampled_ = true; lastSample_ = input.nowMs;
            lastX_ = input.cursorX; lastHeight_ = sampledHeight;
            if (input.grounded) Finish(input.nowMs);
            return Result(input, before);
        }
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
            distanceMoved = std::hypot(input.cursorX - lastX_, sampledHeight - lastHeight_);
            if (delta) speed = distanceMoved * 1000 / delta;
        }
        sampled_ = true; lastSample_ = input.nowMs;
        lastX_ = input.cursorX; lastHeight_ = sampledHeight;
        if (!stillTracked_ || speed > 12 || std::hypot(input.cursorX - stillX_, sampledHeight - stillHeight_) > 4) {
            stillTracked_ = true; stillSince_ = input.nowMs;
            stillX_ = input.cursorX; stillHeight_ = sampledHeight;
        }
        const double distance = std::abs(input.cursorX - input.catX);
        const double scale = std::clamp(input.sizeScale, .5, 2.0);
        const double sniffDistance = (input.pawStandOff+6)*scale;
        const bool reachable = input.cursorHeight >= -8 && input.cursorHeight <= 130*scale;
        const bool teasing = reachable && distance <= 180*scale && speed >= 25 && speed <= 600;
        excitement_ = std::clamp(excitement_ + delta*.001*(teasing ? 1.0 : -.3), 0.0, 3.0);
        if (teasing) lastTease_ = input.nowMs;
        if (distance > 330 || input.cursorHeight < -25 || input.cursorHeight > 240) {
            if (Active()) Cancel(input.nowMs);
            return Result(input, before);
        }
        if (Active() && action_ != CursorAction::Abandon) {
            fastMs_ = speed > 650 ? fastMs_ + delta : 0;
            if (distanceMoved > 180 || fastMs_ >= 100) Change(CursorAction::Abandon, input.nowMs);
            // An episode budget cannot cut off an authored paw stroke before
            // its retraction/settling poses. Pointer departure and OFF still
            // cancel immediately through the checks above.
            else if (!(((action_==CursorAction::Paw || action_==CursorAction::Punch) &&
                        input.nowMs-changedAt_<CursorAttackDuration(action_,input)) ||
                       (action_==CursorAction::Pounce && input.nowMs-changedAt_<CursorPounceTimeout(input))) &&
                    input.nowMs - began_ >= std::max({episodeBudget_,CursorEpisodeMinimum(input),
                    (action_ == CursorAction::ApproachSit || action_ == CursorAction::Sit) ? std::uint64_t{9000} :
                     excitement_ > .5 ? std::uint64_t{12000} : std::uint64_t{7000}}))
                Finish(input.nowMs);
        }
        if (!Active()) {
            if (!input.canStart || input.nowMs < nextStart_ || distance > 225) return Result(input, before);
            // Enabling cursor play is an explicit request for a response. A
            // personality lottery followed by an eight-second retry could
            // make low-curiosity animals ignore a nearby pointer for minutes.
            episodeInterest_ = std::clamp(input.interest, 0.0, 1.0);
            began_ = input.nowMs; fastMs_ = 0;
            episodeBudget_ = 7000;
            cautiousEpisode_ = input.nowMs >= nextSneak_ && input.roll < .24;
            sneaked_ = false;
            contactAllowed_ = true;
            Change(CursorAction::Watch, input.nowMs);
        } else {
            const auto elapsed = input.nowMs - changedAt_;
            switch (action_) {
            case CursorAction::Watch:
                if (elapsed >= 140 && teasing && distance <= 65*scale)
                    Change(excitement_ >= 1.2 ? CursorAction::Punch : CursorAction::Paw, input.nowMs);
                else if (elapsed >= static_cast<std::uint64_t>(650 + 400*(1-episodeInterest_)) && reachable && speed < 600) {
                    if (distance <= sniffDistance && speed < 60) Change(CursorAction::Sniff, input.nowMs);
                    else if (distance > sniffDistance) {
                        const bool sneak = cautiousEpisode_ && !sneaked_ && input.nowMs >= nextSneak_ && speed < 12 && input.nowMs-stillSince_ >= 500 &&
                            input.cursorHeight >= -4 && input.cursorHeight <= 35*scale;
                        if (sneak) { nextSneak_ = input.nowMs+90000; sneaked_ = true; }
                        Change(sneak ? CursorAction::Sneak : CursorAction::Hunt, input.nowMs);
                    }
                } else if (elapsed >= 2500) Finish(input.nowMs);
                break;
            case CursorAction::Sneak:
            case CursorAction::Hunt:
                if (!reachable || speed > 600) Change(CursorAction::Watch, input.nowMs);
                else if (action_ == CursorAction::Sneak && speed > 120) Change(CursorAction::Watch, input.nowMs);
                else if (distance <= 55*scale && teasing) Change(CursorAction::Paw, input.nowMs);
                else if (distance <= sniffDistance && speed < 60) Change(CursorAction::Sniff, input.nowMs);
                else if (elapsed >= huntBudget_) Change(CursorAction::Abandon, input.nowMs);
                break;
            case CursorAction::Sniff:
                if (teasing && distance <= 70*scale) Change(CursorAction::Paw, input.nowMs);
                else if (!reachable || distance > 60*scale || speed > 120) Change(CursorAction::Watch, input.nowMs);
                else if (elapsed >= 1050) {
                    if (!sneaked_ && input.nowMs >= nextSit_ && input.nowMs - stillSince_ >= 1200 &&
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
                if (elapsed >= CursorAttackDuration(CursorAction::Paw,input)) {
                    if (excitement_ >= 1.2 && reachable && distance <= 90*scale)
                        Change(CursorAction::Punch, input.nowMs);
                    else if (lastTease_ && input.nowMs-lastTease_ < 500) Change(CursorAction::Watch, input.nowMs);
                    else Finish(input.nowMs);
                }
                break;
            case CursorAction::Punch:
                if (elapsed >= CursorAttackDuration(CursorAction::Punch,input)) {
                    if (excitement_ >= 1.8 && reachable && distance <= 180*scale)
                        Change(CursorAction::Chase, input.nowMs);
                    else if (lastTease_ && input.nowMs-lastTease_ < 500) Change(CursorAction::Watch, input.nowMs);
                    else Finish(input.nowMs);
                }
                break;
            case CursorAction::Chase:
                if (!reachable || input.cursorHeight > 90*scale || elapsed >= 1400)
                    Change(CursorAction::Abandon, input.nowMs);
                else if (elapsed >= 300 && distance <= 150*scale &&
                        input.cursorX >= input.minX+8 && input.cursorX <= input.maxX-8) {
                    pounceX_ = input.cursorX;
                    launchAnswered_ = launched_ = false;
                    Change(CursorAction::Pounce, input.nowMs);
                }
                break;
            case CursorAction::Pounce:
                if ((launchAnswered_ && !launched_) || !reachable ||
                        std::abs(input.cursorX-pounceX_) > 75*scale || elapsed >= CursorPounceTimeout(input))
                    Change(CursorAction::Abandon, input.nowMs);
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
    std::uint64_t stillSince_ = 0, nextSit_ = 0, nextSneak_ = 0;
    double lastX_ = 0, lastHeight_ = 0;
    double stillX_ = 0, stillHeight_ = 0, seatX_ = 0, seatHeight_ = 0;
    double episodeInterest_ = .5;
    double excitement_ = 0, pounceX_ = 0,pounceHeight_=0;
    std::uint64_t lastTease_ = 0;
    std::uint64_t huntBudget_ = 3300, episodeBudget_ = 7000;
    bool launchAnswered_ = false, launched_ = false, contactAllowed_ = false;
    bool sampled_ = false, stillTracked_ = false, enabled_ = false;
    bool cautiousEpisode_ = false, sneaked_ = false;
    int direction_ = 1;
    unsigned nearbyAttempt_=0;
    std::uint64_t nearbyRestMs_=1000;
    double pointerVx_=0,pointerVy_=0;
    std::uint64_t nextLeap_=0;

    void PursuitRest(const CursorInput& input,bool landed=false) {
        contactAllowed_=false;launchAnswered_=launched_=false;
        nearbyRestMs_=landed?input.landingRecoveryMs+180:280;
        Change(CursorAction::Rest,input.nowMs);
    }
    void PursuitAttempt(const CursorInput& input) {
        const double scale=std::clamp(input.sizeScale,.5,2.);
        const double distance=std::abs(input.cursorX-input.catX);
        const double height=input.cursorHeight/scale;
        const bool edge=(input.catX<=input.minX+1&&input.cursorX<input.catX)||
            (input.catX>=input.maxX-1&&input.cursorX>input.catX);
        const bool align=height<=75&&std::abs(distance-input.pawStandOff*scale)>9*scale;
        if(input.canApproach&&!edge&&(distance>62*scale||align||
                (distance>18*scale&&(input.cursorX-input.catX)*direction_<0))) {
            contactAllowed_=false;Change(CursorAction::Chase,input.nowMs);return;
        }
        if(!input.canAttack)return;
        ++nearbyAttempt_;contactAllowed_=true;launchAnswered_=launched_=false;
        if(height>75&&input.nowMs>=nextLeap_) {
            const double lead=std::clamp(pointerVx_*.22,-65*scale,65*scale);
            pounceX_=input.canApproach&&distance>35*scale?std::clamp(input.cursorX+lead-direction_*28*scale,
                input.minX+8,input.maxX-8):input.catX;
            pounceHeight_=std::clamp(input.cursorHeight+pointerVy_*.12,25*scale,245*scale);
            nextLeap_=input.nowMs+input.pouncePreparationMs+input.landingRecoveryMs+1600;
            Change(CursorAction::Pounce,input.nowMs);
        } else if(height>75) {
            PursuitRest(input);
        } else Change(nearbyAttempt_%2?CursorAction::Paw:CursorAction::Punch,input.nowMs);
    }
    CursorOutput StepPursuit(const CursorInput& input) {
        const auto before=action_;
        const double scale=std::clamp(input.sizeScale,.5,2.);
        const bool valid=Valid(input)&&std::isfinite(input.catVelocityX)&&input.maxX-input.minX>16;
        const bool onSurface=valid&&input.cursorX>=input.minX-120*scale&&input.cursorX<=input.maxX+120*scale&&
            input.cursorHeight>=-20*scale;
        if(input.enabled&&!enabled_)nextStart_=input.nowMs;
        enabled_=input.enabled;
        const double worldHeight=input.cursorHeight+input.supportHeight;
        if(valid&&sampled_&&input.nowMs>lastSample_&&input.nowMs-lastSample_<=250) {
            const double dt=double(input.nowMs-lastSample_);
            const double weight=1-std::exp(-dt/75.);
            pointerVx_+=(std::clamp((input.cursorX-lastX_)*1000/dt,-1400.,1400.)-pointerVx_)*weight;
            pointerVy_+=(std::clamp((worldHeight-lastHeight_)*1000/dt,-1400.,1400.)-pointerVy_)*weight;
        } else pointerVx_=pointerVy_=0;
        sampled_=valid;lastSample_=input.nowMs;lastX_=input.cursorX;lastHeight_=worldHeight;
        if(InFlight()) {
            if(!onSurface||!input.enabled||!input.available||!input.canContinue||input.cursorHeight>310*scale||
                    std::abs(input.cursorX-input.catX)>360*scale)
                contactAllowed_=false;
            if(input.grounded) {
                if(onSurface&&input.enabled&&input.available&&input.canContinue)PursuitRest(input,true);
                else Cancel(input.nowMs,300);
            }
            return Result(input,before);
        }
        if(!onSurface||!input.enabled||!input.available||!input.canContinue) {
            if(Active())Cancel(input.nowMs,300);
            sampled_=false;return Result(input,before);
        }
        if(!Active()) {
            if(input.canStart&&input.grounded&&input.nowMs>=nextStart_) {
                began_=input.nowMs;episodeInterest_=std::clamp(input.interest,0.,1.);
                contactAllowed_=false;Change(CursorAction::Watch,input.nowMs);
            }
            return Result(input,before);
        }
        const auto elapsed=input.nowMs-changedAt_;
        const double distance=std::abs(input.cursorX-input.catX);
        switch(action_) {
        case CursorAction::Watch:
            if(elapsed>=static_cast<std::uint64_t>(180+140*(1-episodeInterest_)))PursuitAttempt(input);
            break;
        case CursorAction::Chase: {
            if(!input.canApproach){PursuitRest(input);break;}
            const double ahead=(input.cursorX-input.catX)*(input.catVelocityX<0?-1:1);
            const double speed=std::abs(input.catVelocityX);
            // Commit while still running, early enough to show the complete
            // source gather. The impulse itself keeps the actual motor speed.
            const double intercept=speed*(input.pouncePreparationMs*.001+.32)+50*scale;
            const bool runningCatch=input.canAttack&&elapsed>=300&&input.nowMs>=nextLeap_&&speed>42*scale&&
                ahead>55*scale&&ahead<std::min(230*scale,intercept)&&
                input.cursorHeight>22*scale&&
                (input.cursorHeight>75*scale||std::abs(pointerVx_)>80*scale);
            if(runningCatch) {
                ++nearbyAttempt_;contactAllowed_=true;launchAnswered_=launched_=false;
                direction_=input.catVelocityX<0?-1:1;
                pounceX_=std::clamp(input.cursorX+std::clamp(pointerVx_*.25,-70*scale,70*scale)-direction_*28*scale,
                    input.minX+8,input.maxX-8);
                pounceHeight_=std::clamp(input.cursorHeight+pointerVy_*.12,35*scale,245*scale);
                nextLeap_=input.nowMs+input.pouncePreparationMs+input.landingRecoveryMs+1600;
                Change(CursorAction::Pounce,input.nowMs);
            } else if((distance<=62*scale||input.catX<=input.minX+1||input.catX>=input.maxX-1)&&
                    std::abs(pointerVx_)<100*scale&&speed<35*scale)
                PursuitAttempt(input);
            break;
        }
        case CursorAction::Paw:case CursorAction::Punch:
            if(elapsed>=CursorAttackDuration(action_,input))PursuitRest(input);
            break;
        case CursorAction::Pounce:
            if((launchAnswered_&&!launched_)||elapsed>=CursorPounceTimeout(input))PursuitRest(input);
            break;
        case CursorAction::Rest:
            if(input.grounded&&elapsed>=nearbyRestMs_)PursuitAttempt(input);
            break;
        default:PursuitRest(input);break;
        }
        return Result(input,before);
    }

    void CancelNearby(std::uint64_t now) {
        Cancel(now);nextStart_=now+400;
    }
    void NearbyRest(const CursorInput& input,bool landed=false) {
        contactAllowed_=false;launchAnswered_=launched_=false;
        nearbyRestMs_=5000+(nearbyAttempt_*173u)%4001;
        if(landed)nearbyRestMs_+=std::min<std::uint64_t>(input.landingRecoveryMs,86400000);
        Change(CursorAction::Rest,input.nowMs);
    }
    void NearbyAttempt(const CursorInput& input,double height) {
        ++nearbyAttempt_;contactAllowed_=true;launchAnswered_=launched_=false;
        if(height>155 || (height>75 && nearbyAttempt_%3==0)) {
            if(input.maxX-input.minX<=16){NearbyRest(input);return;}
            pounceX_=input.canApproach?std::clamp(input.cursorX,input.minX+8,input.maxX-8):input.catX;
            pounceHeight_=input.cursorHeight;
            Change(CursorAction::Pounce,input.nowMs);
        } else if(height>115 || (height>35 && nearbyAttempt_%4==3))Change(CursorAction::BothPaws,input.nowMs);
        else if(height>55)Change(CursorAction::Tiptoe,input.nowMs);
        else Change(CursorAction::Paw,input.nowMs);
    }
    CursorOutput StepNearby(const CursorInput& input) {
        const auto before=action_;
        const double scale=std::clamp(input.sizeScale,.5,2.);
        const bool valid=Valid(input);
        const double distance=valid?std::abs(input.cursorX-input.catX):100000;
        const bool nearby=valid&&distance<=225*scale&&input.cursorHeight>=-8*scale&&input.cursorHeight<=245*scale;
        if(input.enabled&&!enabled_)nextStart_=input.nowMs;
        enabled_=input.enabled;
        if(InFlight()) {
            if(!valid||!input.enabled||!input.available||!input.canContinue||!nearby)contactAllowed_=false;
            const double worldHeight=input.cursorHeight+input.supportHeight;
            if(sampled_&&input.nowMs>lastSample_) {
                const auto dt=input.nowMs-lastSample_;
                if(dt>250||std::hypot(input.cursorX-lastX_,worldHeight-lastHeight_)*1000/dt>900)contactAllowed_=false;
            }
            sampled_=valid;lastSample_=input.nowMs;lastX_=input.cursorX;lastHeight_=worldHeight;
            if(input.grounded) {
                if(input.enabled&&input.available&&input.canContinue&&nearby)NearbyRest(input,true);
                else CancelNearby(input.nowMs);
            }
            return Result(input,before);
        }
        if(!valid||!input.enabled||!input.available||!input.canContinue||!nearby) {
            if(Active())CancelNearby(input.nowMs);
            sampled_=false;return Result(input,before);
        }
        if(sampled_&&(input.nowMs<lastSample_||input.nowMs-lastSample_>250)) {
            CancelNearby(input.nowMs);return Result(input,before);
        }
        sampled_=true;lastSample_=input.nowMs;lastX_=input.cursorX;lastHeight_=input.cursorHeight+input.supportHeight;
        if(!Active()) {
            if(input.canStart&&input.grounded&&input.nowMs>=nextStart_) {
                episodeInterest_=std::clamp(input.interest,0.,1.);contactAllowed_=false;
                began_=input.nowMs;Change(CursorAction::Watch,input.nowMs);
            }
            return Result(input,before);
        }
        const auto elapsed=input.nowMs-changedAt_;
        const double reachDistance=std::max(40.,input.pawStandOff+20)*scale;
        switch(action_) {
        case CursorAction::Watch:
            if(elapsed>=static_cast<std::uint64_t>(350+250*(1-episodeInterest_))) {
                if(distance>reachDistance) {
                    if(input.canApproach)Change(CursorAction::Hunt,input.nowMs);
                    else NearbyRest(input);
                } else NearbyAttempt(input,input.cursorHeight/scale);
            }
            break;
        case CursorAction::Hunt:
            if(!input.canApproach)NearbyRest(input);
            else if(distance<=reachDistance)NearbyAttempt(input,input.cursorHeight/scale);
            else if(elapsed>=huntBudget_)NearbyRest(input);
            break;
        case CursorAction::Paw:case CursorAction::Punch:
            if(elapsed>=CursorAttackDuration(action_,input))NearbyRest(input);
            break;
        case CursorAction::Tiptoe:
            if(elapsed>=std::min<std::uint64_t>(input.tiptoeDurationMs?input.tiptoeDurationMs:1400,86400000))NearbyRest(input);
            break;
        case CursorAction::BothPaws:
            if(elapsed>=std::min<std::uint64_t>(input.bothPawsDurationMs?input.bothPawsDurationMs:1550,86400000))NearbyRest(input);
            break;
        case CursorAction::Pounce:
            if((launchAnswered_&&!launched_)||std::abs(input.cursorX-pounceX_)>90*scale||
                    elapsed>=CursorPounceTimeout(input))NearbyRest(input);
            break;
        case CursorAction::Rest:
            if(elapsed>=nearbyRestMs_&&input.grounded)Change(CursorAction::Watch,input.nowMs);
            break;
        default:NearbyRest(input);break;
        }
        return Result(input,before);
    }

    static bool Valid(const CursorInput& input) {
        return std::isfinite(input.cursorX) && std::isfinite(input.cursorHeight) &&
            std::isfinite(input.catX) && std::isfinite(input.minX) && std::isfinite(input.maxX) &&
            std::isfinite(input.interest) && std::isfinite(input.roll) &&
            std::isfinite(input.supportHeight) &&
            std::isfinite(input.sizeScale) && input.sizeScale > 0 && std::isfinite(input.pawStandOff) &&
            std::isfinite(input.movementScale) && input.movementScale > 0 &&
            input.pawStandOff > 0 && input.pawStandOff <= 100 && input.minX <= input.maxX;
    }
    void Change(CursorAction action, std::uint64_t now) { action_ = action; changedAt_ = now; }
    void Finish(std::uint64_t now) {
        action_ = CursorAction::None;
        nextStart_ = now + static_cast<std::uint64_t>(18000 - 6000*episodeInterest_);
        excitement_ = 0;
        launchAnswered_ = launched_ = false;
        contactAllowed_ = false;
    }
    CursorOutput Result(const CursorInput& input, CursorAction before) {
        CursorOutput output;
        output.action = action_; output.changed = before != action_;
        if(input.pursuePointer) {
            const bool attack=action_==CursorAction::Paw||action_==CursorAction::Punch||action_==CursorAction::Pounce||action_==CursorAction::Rest;
            if(!attack&&std::isfinite(input.cursorX)&&std::abs(input.cursorX-input.catX)>18*input.sizeScale)
                direction_=input.cursorX<input.catX?-1:1;
            output.direction=direction_;output.targetX=pounceX_;
            if(action_==CursorAction::Chase&&input.canApproach) {
                const double lead=std::clamp(pointerVx_*.16,-85*input.sizeScale,85*input.sizeScale);
                const double target=std::clamp(input.cursorX+lead-direction_*input.pawStandOff*input.sizeScale,input.minX,input.maxX);
                output.velocity=std::clamp((target-input.catX)*3.4,-170.,170.);
            }
            return output;
        }
        if (std::isfinite(input.cursorX) && std::isfinite(input.catX) &&
                std::abs(input.cursorX - input.catX) > 2 &&
                (before != action_ || (action_ != CursorAction::Paw && action_ != CursorAction::Punch && action_ != CursorAction::Pounce &&
                    action_!=CursorAction::Tiptoe&&action_!=CursorAction::BothPaws)))
            direction_ = input.cursorX < input.catX ? -1 : 1;
        output.direction = direction_;
        output.targetX = pounceX_;
        if (action_ == CursorAction::Hunt || action_ == CursorAction::Sneak) {
            const bool sneak = action_ == CursorAction::Sneak;
            const double speed = sneak ? 14.0 : 45.0;
            if (before != action_) {
                const double distance = std::max(0.0,std::abs(input.cursorX-input.catX)-input.pawStandOff*input.sizeScale);
                huntBudget_ = static_cast<std::uint64_t>(std::clamp(distance/(speed*input.movementScale)*1000+1600,
                    3300.0,sneak ? 36000.0 : 8500.0));
                episodeBudget_ = std::min<std::uint64_t>(sneak ? 40000 : 12000,input.nowMs-began_+huntBudget_+2500);
            }
            const double target = std::clamp(input.cursorX - direction_*input.pawStandOff*input.sizeScale, input.minX, input.maxX);
            output.velocity = std::clamp((target - input.catX)*2.8, -speed, speed);
        } else if (action_ == CursorAction::Chase) {
            const double approach = std::max(0.0,std::abs(input.cursorX-input.catX)-35*input.sizeScale);
            const double target = std::clamp(input.catX+direction_*approach, input.minX, input.maxX);
            output.velocity = std::clamp((target-input.catX)*4.0, -110.0, 110.0);
        } else if (action_ == CursorAction::ApproachSit) {
            output.velocity = std::clamp((seatX_ - input.catX)*3.5, -35.0, 35.0);
        }
        return output;
    }
};

struct CursorMotionState {
    bool ownsMotion = true, waitingForStop = false;
    std::uint64_t motionStarted = 0;
    std::uint64_t serial = 0;
};

// This is the production host adapter, shared with the Box2D integration tests.
// Action clocks may be rebased by the stop gate or pause handling; another
// action's clock/identity must instead cancel cursor control without replacing it.
class CursorSession {
public:
    bool Active() const { return planner_.Active(); }
    CursorAction Action() const { return planner_.Action(); }
    bool InFlight() const { return planner_.InFlight(); }
    bool ContactAllowed() const { return planner_.ContactAllowed(); }
    bool OwnsSerial(std::uint64_t serial) const { return serial == serial_; }
    double TargetX() const { return planner_.TargetX(); }
    double TargetHeight() const { return planner_.TargetHeight(); }
    double Excitement() const { return planner_.Excitement(); }
    void ConfirmLaunch(bool succeeded) { planner_.ConfirmLaunch(succeeded); }
    double Velocity() const { return velocity_; }
    void AdoptMotion(std::uint64_t started, bool waitingForStop, std::uint64_t serial = 0) {
        motionStarted_ = started; wasWaiting_ = waitingForStop; serial_ = serial;
    }
    void Pause(std::uint64_t elapsedMs, const CursorMotionState& motion) {
        if (Active() && (!motion.ownsMotion || motion.serial != serial_)) {
            planner_.Cancel(motion.motionStarted,repeatNearby_?600:12000); velocity_ = 0; wasWaiting_ = false;
            return;
        }
        planner_.Pause(elapsedMs); velocity_ = 0;
        AdoptMotion(motion.motionStarted, motion.waitingForStop, motion.serial);
    }
    CursorOutput Step(CursorInput input, std::uint64_t elapsedMs, const CursorMotionState& motion) {
        repeatNearby_=input.repeatNearby;
        if (Active() && wasWaiting_ && !motion.waitingForStop && motion.ownsMotion)
            motionStarted_ = motion.motionStarted;
        const bool owns = motion.ownsMotion && motion.serial == serial_ && (motion.motionStarted == motionStarted_ ||
            (wasWaiting_ && motion.waitingForStop));
        if (Active() && !owns) {
            planner_.Cancel(input.nowMs,input.repeatNearby?600:12000); velocity_ = 0; wasWaiting_ = false;
            return {}; // Keep the interrupting action, including the same named action.
        }
        // OFF, missing cursor and manual/busy states are checked before the
        // braking pause. Otherwise toggling OFF could leave an active session.
        if (Active() && (!input.enabled || !input.canContinue || !input.available)) {
            const auto output = planner_.Step(input);
            velocity_ = 0; wasWaiting_ = false;
            return output;
        }
        if (Active() && motion.waitingForStop && !InFlight()) {
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
    std::uint64_t motionStarted_ = 0, serial_ = 0;
    bool wasWaiting_ = false;
    bool repeatNearby_=false;
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
