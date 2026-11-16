#pragma once
#include "furniture_assets.generated.hpp"
#include <algorithm>
#include <cmath>

namespace cat::furniture {
// One owner from the quiet boundary through the complete authored recovery.
// World root never moves during the movie: cat, paws and prop share its clock.
enum class Phase { None, Queued, Approach, Settle, Play, Recover };
struct Space {double left=0,right=0;bool valid=false;};
inline Space BodySpace(double root,double scale) {return {root-65*scale,root+65*scale,true};}
inline Space MovieSpace(Kind kind,unsigned coat,double root,double scale) {
    const auto* asset=Find(kind,coat);if(!asset)return {};
    const double pixels=110*scale/asset->referenceHeight;
    return {root-asset->anchorX*pixels,root+(asset->width-asset->anchorX)*pixels,true};
}
class Session {
public:
    bool Request(Kind kind) {
        if(kind==Kind::None||phase_!=Phase::None)return false;
        kind_=kind;phase_=Phase::Queued;return true;
    }
    bool Begin(double x,double left,double right,double growth,unsigned coat,Space occupied={},Space fallback={}) {
        if(phase_!=Phase::Queued)return false;
        asset_=Find(kind_,coat);
        const double safeRight=right-std::max(0.,225*growth-96);
        if(!asset_||safeRight<left){Cancel();return false;}
        const auto extents=MovieSpace(kind_,coat,0,growth);
        const auto place=[&](Space blocked,double& target) {
            target=std::clamp(x,left,safeRight);
            if(!blocked.valid)return true;
            const double gap=12*growth;
            if(target+extents.right<=blocked.left-gap||target+extents.left>=blocked.right+gap)return true;
            const double a=std::min(target,blocked.left-gap-extents.right);
            const double b=std::max(target,blocked.right+gap-extents.left);
            const bool before=a>=left,after=b<=safeRight;
            if(!before&&!after)return false;
            target=before&&(!after||std::abs(a-x)<=std::abs(b-x))?a:b;return true;
        };
        if(!place(occupied,target_)&&(!fallback.valid||!place(fallback,target_)))return false;
        growth_=growth;elapsed_=0;++serial_;
        phase_=std::abs(x-target_)>.6?Phase::Approach:Phase::Settle;return true;
    }
    void Cancel(){phase_=Phase::None;kind_=Kind::None;asset_=nullptr;elapsed_=0;}
    bool Advance(double seconds,double x,double speed,bool sideReady,bool decoded) {
        if(!std::isfinite(seconds)||seconds<=0||seconds>.25)return false;
        if(phase_==Phase::Approach&&std::abs(x-target_)<=.6&&std::abs(speed)<=.5)
            phase_=Phase::Settle;
        if(phase_==Phase::Settle&&sideReady&&decoded&&std::abs(speed)<=.5) {
            phase_=Phase::Play;elapsed_=0;return false;
        }
        if(phase_==Phase::Play||phase_==Phase::Recover) {
            elapsed_+=seconds*1000;
            if(phase_==Phase::Play&&elapsed_>=asset_->durationMs) {
                phase_=Phase::Recover;elapsed_=0;
            } else if(phase_==Phase::Recover&&elapsed_>=750) {Cancel();return true;}
        }
        return false;
    }
    double Velocity(double x,double maxSpeed)const {
        if(phase_!=Phase::Approach)return 0;
        const double distance=target_-x;
        return std::abs(distance)<.3?0:std::clamp(distance*1.65,-maxSpeed,maxSpeed);
    }
    bool Owned()const{return phase_!=Phase::None&&phase_!=Phase::Queued;}
    bool Requested()const{return phase_!=Phase::None;}
    bool Playing()const{return phase_==Phase::Play;}
    Phase State()const{return phase_;}
    Kind Action()const{return kind_;}
    const Asset* Selected()const{return asset_;}
    std::uint64_t Milliseconds()const{return static_cast<std::uint64_t>(elapsed_);}
    std::uint64_t Serial()const{return serial_;}
    double Target()const{return target_;}
    Space ReservedSpace()const{return Owned()&&asset_?MovieSpace(kind_,asset_->character,target_,growth_):Space{};}
private:
    Phase phase_=Phase::None;Kind kind_=Kind::None;const Asset* asset_=nullptr;
    double target_=0,elapsed_=0,growth_=1;std::uint64_t serial_=0;
};
// Count only usable, quiet floor time. Menus, other actions and roaming pause
// cannot cause a burst of overdue random events when they finish.
class RandomSchedule {
public:
    bool Advance(double seconds,bool quiet,unsigned roll,bool mayStart=true) {
        if(!quiet||seconds<=0||seconds>.25)return false;
        if(!due_) {
            if(!remaining_)remaining_=90000+roll%90001;
            remaining_=std::max(0.,remaining_-seconds*1000);
            if(remaining_>0)return false;
            due_=true;
        }
        // Cooldown still accrues during usable quiet time, but an overdue
        // event cannot steal a just-started neutral recovery/rest interval.
        if(!mayStart)return false;
        due_=false;remaining_=180000+(roll/3)%240001;return true;
    }
    void Completed(unsigned roll){due_=false;remaining_=180000+roll%240001;}
    double Remaining()const{return remaining_;}
private: double remaining_=0;bool due_=false;
};
} // namespace cat::furniture
