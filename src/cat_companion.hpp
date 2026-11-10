#pragma once

#include "pet_state.hpp"
#include "cat_animation.hpp"
#include "cat_cursor_animation.hpp"
#include "cat_behavior.hpp"
#include "cat_memory.hpp"
#include "cat_life.hpp"
#include "cat_nutrition.hpp"
#include "cat_appearance_visual.hpp"
#include "cat_fish.hpp"
#include "cat_physics.hpp"
#include "cat_motion_gate.hpp"
#include "cat_pose_transition.hpp"
#include "cat_video_ground_bridge.hpp"
#include "cat_random_zoomies.hpp"
#include "cat_environment.hpp"
#include "cat_preferences.hpp"
#include "cat_species.hpp"
#include <functional>
#include <memory>
#include <utility>

namespace cat::companion {

// A companion owns every persistent field. No path in this module names or
// writes the first cat's pet.ini, memory.ini, nutrition.ini or preferences.ini.
struct State {
    cat::PetState pet;
    cat::nutrition::State nutrition;
    cat::memory::State memory;
    cat::life::State life;
    cat::Preferences preferences{cat::Character::Tuxedo, cat::Outfit::None};
    bool enabled = true;
    std::uint64_t sniffs = 0, mutualPlay = 0, sharedSleeps = 0, foodRaces = 0;
    std::int64_t lastSocialUtc = 0;
};

inline State NewState(std::int64_t now) {
    State s;
    s.pet = cat::NewPet(now);
    s.pet.position = .65;
    s.nutrition = cat::nutrition::NewState(now, now);
    s.memory = cat::memory::FromLegacy(s.pet, now);
    // NewPet already mixes the birthday into its entropy. Supplying birthday
    // XOR salt here would cancel that birthday and give every companion the
    // same temperament. The fixed independent salt keeps each birth stable.
    s.life = cat::life::NewPet(s.pet, now, 0x486ba0719d3ae743ULL);
    return s;
}

inline bool Valid(const State& s) {
    cat::PetState parsed;
    std::string error;
    return cat::Parse(cat::Serialize(s.pet), parsed, error) &&
        cat::nutrition::IsValid(s.nutrition) && cat::memory::Valid(s.memory) && cat::life::Valid(s.life) &&
        cat::IsValid(s.preferences.character) && cat::IsValid(s.preferences.outfit) &&
        s.pet.bornUtc == s.nutrition.bornUtc && s.pet.bornUtc == s.memory.bornUtc && s.pet.bornUtc == s.life.bornUtc &&
        s.memory.meals == s.pet.feedCount && s.memory.treats == s.pet.snackCount &&
        s.sniffs <= cat::memory::kMaxCount && s.mutualPlay <= cat::memory::kMaxCount &&
        s.sharedSleeps <= cat::memory::kMaxCount && s.foodRaces <= cat::memory::kMaxCount &&
        (s.lastSocialUtc == 0 || (s.lastSocialUtc >= s.pet.bornUtc &&
          s.lastSocialUtc <= std::max(s.pet.updatedUtc, s.memory.updatedUtc)));
}

inline std::string Hex(const std::string& text) {
    static constexpr char digits[] = "0123456789abcdef";
    std::string out; out.reserve(text.size() * 2);
    for (unsigned char c : text) { out += digits[c >> 4]; out += digits[c & 15]; }
    return out;
}
inline bool Unhex(const std::string& text, std::string& out) {
    if (text.empty() || text.size() % 2 || text.size() > 32768) return false;
    const auto digit = [](char c) { return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : -1; };
    std::string result; result.reserve(text.size()/2);
    for (std::size_t i = 0; i < text.size(); i += 2) {
        const int a = digit(text[i]), b = digit(text[i+1]);
        if (a < 0 || b < 0) return false;
        result += static_cast<char>((a << 4) | b);
    }
    out = std::move(result); return true;
}

inline std::string Serialize(const State& s) {
    if (!Valid(s)) return {};
    std::ostringstream body; body.imbue(std::locale::classic());
    body << "schema=1\nenabled=" << (s.enabled ? 1 : 0)
         << "\npet=" << Hex(cat::Serialize(s.pet))
         << "\nnutrition=" << Hex(cat::nutrition::Serialize(s.nutrition))
         << "\nmemory=" << Hex(cat::memory::Serialize(s.memory))
         << "\nlife=" << Hex(cat::life::Serialize(s.life))
         << "\npreferences=" << Hex(cat::SerializePreferences(s.preferences))
         << "\nsniffs=" << s.sniffs << "\nmutual_play=" << s.mutualPlay
         << "\nshared_sleeps=" << s.sharedSleeps << "\nfood_races=" << s.foodRaces
         << "\nlast_social_utc=" << s.lastSocialUtc << '\n';
    std::ostringstream out; out.imbue(std::locale::classic());
    out << body.str() << "checksum=" << std::hex << std::setw(16) << std::setfill('0') << cat::Checksum(body.str()) << '\n';
    return out.str();
}

inline bool Parse(const std::string& text, State& destination, std::string& error) {
    const auto fail = [&](const char* why) { error = why; return false; };
    if (text.empty() || text.size() > 65536) return fail("Empty or oversized companion record");
    const auto at = text.rfind("checksum=");
    if (at == std::string::npos || !at || text[at-1] != '\n') return fail("Missing companion checksum");
    const std::string body = text.substr(0,at);
    std::string hash = text.substr(at+9);
    if (!hash.empty() && hash.back() == '\n') hash.pop_back();
    if (hash.size() != 16 || hash.find_first_not_of("0123456789abcdef") != std::string::npos) return fail("Malformed companion checksum");
    std::uint64_t expected = 0;
    std::istringstream hashIn(hash); hashIn.imbue(std::locale::classic()); hashIn >> std::hex >> expected;
    if (!hashIn || expected != cat::Checksum(body)) return fail("Companion checksum mismatch");
    std::map<std::string,std::string> fields;
    std::istringstream lines(body); std::string line;
    while (std::getline(lines,line)) {
        const auto split = line.find('=');
        if (split == std::string::npos || !split || !fields.emplace(line.substr(0,split),line.substr(split+1)).second)
            return fail("Malformed or duplicate companion field");
    }
    const char* keys[] = {"schema","enabled","pet","nutrition","memory","life","preferences","sniffs","mutual_play","shared_sleeps","food_races","last_social_utc"};
    if (fields.size() != std::size(keys)) return fail("Unexpected companion fields");
    for (const auto key : keys) if (!fields.count(key)) return fail("Missing companion field");
    if (fields["schema"] != "1") return fail("Unsupported companion schema");
    if (fields["enabled"] != "0" && fields["enabled"] != "1") return fail("Invalid companion enabled flag");
    State s; s.enabled = fields["enabled"] == "1";
    std::string decoded;
    if (!Unhex(fields["pet"],decoded) || !cat::Parse(decoded,s.pet,error)) return fail("Invalid companion growth history");
    if (!Unhex(fields["nutrition"],decoded) || !cat::nutrition::Parse(decoded,s.nutrition,error)) return fail("Invalid companion nutrition history");
    if (!Unhex(fields["memory"],decoded) || !cat::memory::Parse(decoded,s.memory,error)) return fail("Invalid companion memories");
    if (!Unhex(fields["life"],decoded) || !cat::life::Parse(decoded,s.life,error)) return fail("Invalid companion life history");
    if (!Unhex(fields["preferences"],decoded) || !cat::ParsePreferences(decoded,s.preferences,error)) return fail("Invalid companion preferences");
    const auto number = [&](const char* key, std::uint64_t& value) {
        const auto& v = fields[key];
        if (v.empty() || v.size() > 11 || v.find_first_not_of("0123456789") != std::string::npos) return false;
        std::istringstream in(v); in.imbue(std::locale::classic()); in >> value;
        return in && in.peek() == std::char_traits<char>::eof();
    };
    std::uint64_t stamp = 0;
    if (!number("sniffs",s.sniffs) || !number("mutual_play",s.mutualPlay) ||
        !number("shared_sleeps",s.sharedSleeps) || !number("food_races",s.foodRaces) ||
        !number("last_social_utc",stamp) || stamp > static_cast<std::uint64_t>(cat::memory::kMaxUtc))
        return fail("Invalid companion interaction history");
    s.lastSocialUtc = static_cast<std::int64_t>(stamp);
    if (!Valid(s)) return fail("Companion ownership or value mismatch");
    destination = std::move(s); error.clear(); return true;
}

enum class Outcome { Missing, Adopted, Loaded, Recovered, Blocked };
// Same IO contract as cat_memory_storage.hpp: Write must create a new path,
// Replace must be atomic, Preserve must create a unique immutable copy. Caller
// holds the application storage mutex during Load/Save, including readbacks.
class Store {
public:
    State state;
    Outcome outcome = Outcome::Blocked;
    std::string error;
    bool available() const { return outcome != Outcome::Blocked && outcome != Outcome::Missing; }
    bool enabled() const { return available() && state.enabled; }

    template<class IO> void Load(IO& io, std::int64_t /*now*/) {
        outcome = Outcome::Blocked; expectedBorn_ = 0; error.clear();
        auto main = Inspect(io,kMain), backup = Inspect(io,kBackup);
        if (main.valid) { state = main.state; outcome = Outcome::Loaded; }
        else if (backup.valid) { state = backup.state; outcome = Outcome::Recovered; }
        else if (!main.exists && !backup.exists) outcome = Outcome::Missing;
        else error = "No readable companion history; both original files were preserved";
        if (available()) expectedBorn_ = state.pet.bornUtc;
        knownMain_ = Inspect(io,kMain); knownBackup_ = Inspect(io,kBackup);
    }
    bool Adopt(std::int64_t now) {
        if (outcome != Outcome::Missing || now < cat::memory::kMinUtc || now > cat::memory::kMaxUtc) return false;
        state = NewState(now); expectedBorn_ = now; outcome = Outcome::Adopted; return true;
    }
    template<class IO> bool Save(IO& io) {
        if (!available()) { error = "Companion history is not writable"; return false; }
        const auto bytes = Serialize(state); State checked;
        if (bytes.empty() || !Validate(bytes,checked)) { error = "Refusing invalid companion state or a changed birthday"; return false; }
        auto main = Inspect(io,kMain), backup = Inspect(io,kBackup);
        if (!Same(main,knownMain_) || !Same(backup,knownBackup_)) { error = "Companion files changed; reload before saving"; return false; }
        if ((main.exists && !main.readable) || (backup.exists && !backup.readable)) { error = "Unreadable companion history cannot be overwritten"; return false; }
        std::string mainTemp, backupTemp;
        const auto cleanup = [&] { if (!mainTemp.empty()) io.Remove(mainTemp); if (!backupTemp.empty()) io.Remove(backupTemp); };
        if (!Temp(io,kMain,bytes,mainTemp)) return false;
        if (main.valid && !Temp(io,kBackup,main.bytes,backupTemp)) { cleanup(); return false; }
        if ((main.exists && !main.valid && !io.Preserve(kMain)) ||
            (main.valid && backup.exists && !backup.valid && !io.Preserve(kBackup))) {
            error = "Unable to preserve damaged companion history"; cleanup(); return false;
        }
        if (!Same(Inspect(io,kMain),main) || !Same(Inspect(io,kBackup),backup)) {
            error = "Companion history changed during save"; cleanup(); return false;
        }
        if (!backupTemp.empty()) {
            if (!io.Replace(backupTemp,kBackup)) { error = "Unable to replace companion backup"; cleanup(); return false; }
            backupTemp.clear(); backup = main; knownBackup_ = main;
        }
        if (!Same(Inspect(io,kMain),main) || !Same(Inspect(io,kBackup),backup)) {
            error = "Companion history changed before replacement"; cleanup(); return false;
        }
        if (!io.Replace(mainTemp,kMain)) { error = "Unable to replace companion record"; cleanup(); return false; }
        mainTemp.clear(); knownMain_ = {true,true,true,checked,bytes}; error.clear(); return true;
    }
private:
    static constexpr const char* kMain = "companion.ini";
    static constexpr const char* kBackup = "companion.bak";
    struct Record { bool exists=false, readable=false, valid=false; State state; std::string bytes; };
    Record knownMain_, knownBackup_;
    std::int64_t expectedBorn_ = 0;
    static bool Same(const Record& a,const Record& b) { return a.exists==b.exists && a.readable==b.readable && a.valid==b.valid && a.bytes==b.bytes; }
    bool Validate(const std::string& bytes,State& out) const {
        std::string ignored; return Parse(bytes,out,ignored) && (!expectedBorn_ || out.pet.bornUtc==expectedBorn_);
    }
    template<class IO> Record Inspect(IO& io,const std::string& name) const {
        Record r; r.exists=io.Exists(name);
        if (r.exists) { r.readable=io.Read(name,r.bytes); if(r.readable) r.valid=Validate(r.bytes,r.state); }
        return r;
    }
    template<class IO> bool Temp(IO& io,const std::string& name,const std::string& bytes,std::string& path) {
        for (unsigned i=0;i<256;++i) {
            const auto candidate=name+".tmp-"+std::to_string(i);
            if(io.Exists(candidate)) continue;
            if(!io.Write(candidate,bytes)) { error="Unable to write companion temporary record"; return false; }
            std::string readback; State parsed;
            if(!io.Read(candidate,readback) || readback!=bytes || !Validate(readback,parsed)) {
                io.Remove(candidate); error="Companion temporary record failed validation"; return false;
            }
            path=candidate; return true;
        }
        error="No unused companion temporary path"; return false;
    }
};

enum class Interaction { None, Sniff, Follow, RunTogether, Paw, SharedSleep, FoodCompetition, WindowFollow, Startled };
struct Platform {
    std::uintptr_t id = 0;
    double left = 0, right = 0, height = 0;
    bool valid = false;
};
struct Context {
    std::int64_t nowUtc = 0;
    std::uint64_t ticks = 0;
    double elapsedSeconds = 0;
    int localHour = 12;
    bool paused = false, primaryVisible = true, primaryBusy = false;
    cat::physics::Pose primary;
    cat::animation::Motion primaryMotion = cat::animation::Motion::Idle;
    Platform primaryPlatform, supportPlatform;
};
struct Snapshot {
    bool enabled = false;
    cat::physics::Pose physical;
    cat::animation::Pose animation;
    cat::animation::Motion motion = cat::animation::Motion::Idle;
    cat::Preferences preferences;
    cat::nutrition::Visual body;
    cat::life::Profile life;
    cat::life::Temperament temperament = cat::life::Temperament::Legacy;
    cat::behavior::Archetype archetype = cat::behavior::Archetype::Cool;
    double growthScale = 1, bond = 0;
    Interaction interaction = Interaction::None;
    std::uint64_t presentationTicks=0,presentationGeneration=0;
    bool presentationPaused=false;
};

// Owns one independent Box2D world/body and one birthday-derived Brain. The
// primary cat is a read-only observation; mutual reactions are explicit events
// returned to the existing main loop, never writes through a primary pointer.
class Model {
public:
    // A review build may use a newly authored Walk through the same renderer.
    // The caller owns this immutable registration for the model's lifetime.
    // Other motions and normal startup retain the approved manifest.
    void SetWalkRegistration(const cat::sprite_assets::Descriptor* registration) {
        reviewWalk_ = registration && cat::sprite_assets::Valid(*registration) &&
            registration->clip==cat::sprite_sequence::Clip::Walk && registration->character==0 ? registration:nullptr;
    }
    void Bind(State& state) {
        state_=&state; brain_=std::make_unique<cat::behavior::Brain>(state.pet.bornUtc,static_cast<std::uint64_t>(state.pet.bornUtc)^0xb49a5a244cc13db7ULL);
        rng_=static_cast<std::uint64_t>(state.pet.bornUtc)^0x1a7e24977a4eab43ULL;
        zoomiesRng_=static_cast<std::uint64_t>(state.pet.bornUtc)^0x27f08e185cb46a93ULL;
        randomZoomies_.Reset();randomZoomiesOwner_=0;randomZoomiesMsRemainder_=0;
        ready_=false; nextDecision_=0; interaction_=event_=Interaction::None; socialUntil_=0; supportId_=0;
        motion_=cat::animation::Motion::Idle; walkingMotion_=cat::animation::Motion::Walk;
        feedingKind_=cat::feeding::FoodKind::Meal;
        groomVariant_=0;
        motionStart_=lastTicks_=careUntil_=landedAt_=jumpTurnHandledLanding_=0; direction_=facing_=1; speed_=0; support_={}; gait_.Reset(); groundDrive_.Reset(facing_);
        poseTransition_.Reset(); jumpPresentation_.Reset(); groundArtBridge_.Reset(); motionSerial_=0;
        waitingForStop_=jumpPending_=wakingFromSleep_=false;
        storyActive_=quietActive_=false;
        presentationTicks_=0;presentationPaused_=false;++presentationGeneration_;
        presentationIdentity_=state.preferences.character;
    }
    bool Bound() const { return state_ && brain_; }
    bool Busy() const {
        if(!Bound() || !ready_ || !state_->enabled) return false;
        return randomZoomies_.Active() || !physics_.CurrentPose().grounded || StationaryLanding(lastTicks_,physics_.CurrentPose()) ||
            waitingForStop_ || jumpPending_ || interaction_!=Interaction::None ||
            motion_==cat::animation::Motion::Eat || motion_==cat::animation::Motion::Treat ||
            motion_==cat::animation::Motion::TailRaise || motion_==cat::animation::Motion::ToyPaw;
    }
    void Reset(double minX,double maxX,double maxLift) {
        if(!Bound()) return;
        minX_=std::isfinite(minX)?minX:40; maxX_=std::isfinite(maxX)?std::max(maxX,minX_+1):minX_+800;
        physics_.Reset(minX_+std::clamp(state_->pet.position,0.0,1.0)*(maxX_-minX_),minX_,maxX_);
        maxLift_=std::isfinite(maxLift)?std::max(1.0,maxLift):1;
        physics_.SetCeiling(maxLift_); gait_.Reset(); groundDrive_.Reset(facing_); poseTransition_.Reset(); jumpPresentation_.Reset(); groundArtBridge_.Reset(); ready_=true; supportId_=0;
        ++presentationGeneration_;
    }
    void Relocate(double minX,double maxX,double maxLift,double deltaX=0) {
        if(!ready_) { Reset(minX,maxX,maxLift); return; }
        minX_=minX; maxX_=std::max(minX+1,maxX); maxLift_=std::isfinite(maxLift)?std::max(1.0,maxLift):1;
        physics_.RelocateBounds(minX_,maxX_,deltaX); physics_.SetCeiling(maxLift_); gait_.Reset(); groundDrive_.Reset(facing_); poseTransition_.Reset(); jumpPresentation_.Reset(); groundArtBridge_.Reset();
        ++presentationGeneration_;
    }
    std::uintptr_t SupportId() const { return supportId_; }
    cat::physics::Pose Physical() const { return physics_.CurrentPose(); }
    Interaction TakeEvent() { const auto value=event_; event_=Interaction::None; return value; }
    bool RandomZoomiesActive() const {return randomZoomies_.Active();}
    std::uint64_t RandomZoomiesWaitMs() const {return randomZoomies_.WaitMs();}
    std::uint64_t RandomZoomiesRemainingRunMs() const {return randomZoomies_.RemainingRunMs();}

    void Tick(const Context& c) {
        if(!Bound() || !ready_ || !state_->enabled) return;
        Advance(c.nowUtc); lastContext_=c;
        cat::life::Observe(state_->life,state_->pet.updatedUtc,{c.localHour,motion_==cat::animation::Motion::Sleep,
            interaction_!=Interaction::None,!c.paused,state_->nutrition.fullness});
        const auto profile=cat::memory::Summarize(state_->memory);
        brain_->SetExperience({profile.bond,profile.petPreference,profile.treatPreference,profile.sociabilityShift,profile.energyShift,profile.curiosityShift,profile.mischiefShift});
        const auto life=cat::life::Summarize(state_->life,profile.bond);
        const auto species=cat::species::ProfileFor(state_->preferences.character);
        brain_->SetDisposition(life.activityScale,life.restScale,life.socialScale,life.curiosityScale*species.cursorInterest,life.foodScale*species.foodInterest);
        const auto clockBefore=lastTicks_?lastTicks_:motionStart_;
        const auto wallDelta=c.ticks>=clockBefore?c.ticks-clockBefore:0;
        if(presentationIdentity_!=state_->preferences.character) {
            presentationIdentity_=state_->preferences.character;++presentationGeneration_;
        }
        // The main loop already converts a long physics stall to zero seconds.
        // Its wall-clock interval must not finish a companion action either.
        if(c.paused || !std::isfinite(c.elapsedSeconds) || c.elapsedSeconds<0 || c.elapsedSeconds>.25 ||
                (c.elapsedSeconds==0 && wallDelta>250)) {
            const auto frozen=wallDelta;
            presentationPaused_=true;
            motionStart_+=frozen; nextDecision_+=frozen; socialUntil_+=frozen; careUntil_+=frozen; lastTicks_=c.ticks;
            if(landedAt_) landedAt_+=frozen;
            poseTransition_.Pause(frozen); groundArtBridge_.Freeze(frozen);
            physics_.Update(0,true); return;
        }
        presentationPaused_=false;
        presentationTicks_+=wallDelta;
        UpdateSupport(c);
        const auto before=physics_.CurrentPose();
        PrepareCompletedJumpReturn(c.ticks,before);
        const bool landingRecovery=StationaryLanding(c.ticks,before);
        if(landingRecovery) {
            // A newly accepted care action keeps its full clock while the
            // contact-owned recovery finishes. The actual landing clock is
            // deliberately independent and continues toward its outro end.
            const auto held=std::min(wallDelta,c.ticks>=motionStart_?c.ticks-motionStart_:0);
            motionStart_+=held;nextDecision_+=held;socialUntil_+=held;
            if(careUntil_)careUntil_+=held;
        }
        if(c.ticks < lastTicks_) {
            interaction_=Interaction::None; socialUntil_=0; poseTransition_.Reset(); jumpPresentation_.Reset(); groundArtBridge_.Reset();
            brain_->Interrupt(c.ticks); BeginQuiet(c.ticks);
        }
        lastTicks_=c.ticks;
        if(!landingRecovery && interaction_!=Interaction::None && c.ticks >= socialUntil_) FinishSocial(c);
        if(interaction_!=Interaction::None && (!c.primaryVisible || Busy(c,interaction_) || state_->pet.roamingPaused)) {
            FinishSocial(c);
        }
        AdvanceRandomZoomies(c,before,landingRecovery);
        if(!randomZoomies_.Active() && !landingRecovery && before.grounded && !waitingForStop_ && !jumpPending_ && interaction_==Interaction::None &&
                c.ticks>=nextDecision_ && !AuthoredCareHolding(c.ticks)) {
            const bool continuation=storyActive_ && !quietActive_ && brain_->HasContinuation();
            if(!quietActive_ && !continuation) BeginQuiet(c.ticks);
            else {
                bool social=false;
                if(!continuation && c.primaryVisible && !c.primaryBusy && !state_->pet.roamingPaused &&
                    (!state_->lastSocialUtc || state_->pet.updatedUtc-state_->lastSocialUtc>=45) && Random()<.55) {
                    const auto kind = c.primary.onPlatform && (!before.onPlatform || supportId_!=c.primaryPlatform.id) ? Interaction::WindowFollow :
                        c.primaryMotion==cat::animation::Motion::Sleep ? Interaction::SharedSleep :
                        c.primaryMotion==cat::animation::Motion::Zoomies ? Interaction::RunTogether :
                        Random()<.30 ? Interaction::Paw : Random()<.5 ? Interaction::Sniff : Interaction::Follow;
                    social=TrySocial(kind,c);
                }
                if(!social) Choose(c);
            }
        }
        const auto* videoWalk=RegisteredVideoWalk();
        PrepareGroundArt(c.ticks,!landingRecovery);
        double velocity=0;
        if(!landingRecovery && interaction_!=Interaction::None) velocity=SocialVelocity(c);
        else if(!landingRecovery && cat::animation::Walking(motion_) && !state_->pet.roamingPaused) velocity=direction_*speed_;
        // Keep the complete action and care interval after the moving body has
        // actually braked. Rendering alone must not hide a sliding meal/crouch.
        if(waitingForStop_) {
            const auto start=motionStart_;
            cat::motion::AdvanceGroundStop(waitingForStop_,before.grounded &&
                !groundArtBridge_.Stopping() && !groundArtBridge_.StartingForSideAction(),
                before.velocityX-before.supportVelocityX,groundDrive_,c.ticks,motionStart_,nextDecision_);
            if(careUntil_>start) careUntil_+=motionStart_-start;
            velocity=0;
        }
        if(before.grounded && !before.onPlatform && cat::motion::BrakeBeforeEdge(
                before.x,before.velocityX-before.supportVelocityX,velocity,minX_,maxX_)) {
            direction_=velocity<0?1:-1; velocity=0;
        }
        // Stay on a finite ledge until an explicit ballistic transfer. A gentle
        // braking margin prevents two cats following each other over its edge.
        if(before.onPlatform && support_.valid) {
            if(cat::motion::BrakeBeforeEdge(before.x,before.velocityX-before.supportVelocityX,velocity,
                    support_.left+30,support_.right-30)) velocity=0;
        }
        velocity*=cat::appearance::VisualModifiers(state_->nutrition,state_->preferences.character).speed*species.movementSpeedMultiplier;
        if(groundArtBridge_.HoldMotor())velocity=0;
        else if(groundArtBridge_.Starting() && videoWalk) {
            const double maximum=cat::sprite_assets::NominalLocomotionSpeed(*videoWalk)*
                cat::appearance::VisualModifiers(state_->nutrition,state_->preferences.character).speed*species.movementSpeedMultiplier;
            velocity=std::clamp(velocity,-maximum,maximum);
        }
        const auto displayedPhysical=physics_.RenderPose();
        velocity=groundDrive_.Update(velocity,before.velocityX-before.supportVelocityX,
            displayedPhysical.velocityX-displayedPhysical.supportVelocityX,before.grounded,c.elapsedSeconds);
        if(before.grounded) {facing_=groundDrive_.Facing();physics_.SetTargetVelocity(velocity);}
        const auto* jumpArt=Registered(cat::sprite_sequence::Clip::Jump);
        const auto takeoff=jumpArt && jumpArt->program.stepCount?
            cat::sprite_sequence::Duration(jumpArt->program,jumpArt->program.intro):
            (motion_==cat::animation::Motion::Jump?220U:240U);
        if(!waitingForStop_ && jumpPending_ && before.grounded && c.ticks>=motionStart_ &&
            c.ticks-motionStart_ >= takeoff) {
            jumpPending_=false;
            const auto body=cat::appearance::VisualModifiers(state_->nutrition,state_->preferences.character);
            const double growth=cat::GrowthScale(state_->pet.bornUtc,state_->pet.updatedUtc);
            const double jump=growth*body.jump*species.jumpHeightMultiplier;
            const bool launched=motion_==cat::animation::Motion::Jump?physics_.Jump(55*jump):
                physics_.Pounce(direction_,30*jump,state_->pet.roamingPaused?0:72*body.speed*species.movementSpeedMultiplier);
            if(launched) facing_=direction_;
        }
        const auto beforeRendered=physics_.RenderPose();
        physics_.Update(c.elapsedSeconds);
        const auto after=physics_.CurrentPose();
        const auto rendered=physics_.RenderPose();
        if(after.landed) { landedAt_=c.ticks; landingStrength_=std::clamp(after.landingSpeed/300.0,.2,1.0); }
        if(after.grounded && !after.onPlatform && supportId_) { supportId_=0; support_={}; physics_.RemovePlatform(); }
        // Advance paws from the same interpolated displacement used by View.
        // At120Hz the body moves between60Hz Box2D steps; current-pose deltas
        // would otherwise make the feet alternate between a hold and a jump.
        const double stride=rendered.relativeTravelX-beforeRendered.relativeTravelX;
        const auto walking=cat::motion::DisplayedMotion(motion_,walkingMotion_,rendered.grounded,waitingForStop_,
            rendered.velocityX-rendered.supportVelocityX);
        using Clip=cat::sprite_sequence::Clip;
        const auto* walk=Registered(Clip::Walk);
        const auto* run=Registered(Clip::Run);
        if(walk && run) {
            const auto visual=cat::appearance::VisualModifiers(state_->nutrition,state_->preferences.character);
            const double growth=std::max(.1,cat::GrowthScale(state_->pet.bornUtc,state_->pet.updatedUtc));
            const double hint=cat::sprite_assets::NominalLocomotionSpeed(*walk)*visual.speed*
                cat::species::ProfileFor(state_->preferences.character).movementSpeedMultiplier/growth;
            gait_.ConfigureAuthoredStrides(walk->stridePixels*visual.bodyWidth,run->stridePixels*visual.bodyWidth,
                walk->standingFrame,hint);
        }
        gait_.Advance(walking,stride/std::max(.1,cat::GrowthScale(state_->pet.bornUtc,state_->pet.updatedUtc)),
            c.elapsedSeconds,rendered.grounded);
        if(groundArtBridge_.ConsumeLoopReady())gait_.SetAuthoredPhase(0);
        state_->pet.position=std::clamp((after.x-minX_)/(maxX_-minX_),0.0,1.0);
    }

    bool TrySocial(Interaction kind,const Context& c) {
        if(!Bound() || !ready_ || !state_->enabled || randomZoomies_.Active() || c.ticks<careUntil_ || c.paused || !c.primaryVisible || Busy(c,kind) || state_->pet.roamingPaused) return false;
        const auto own=physics_.CurrentPose();
        if(!own.grounded || !std::isfinite(c.primary.x) || !std::isfinite(c.primary.lift)) return false;
        if(kind==Interaction::WindowFollow) {
            const auto& p=c.primaryPlatform;
            if(!c.primary.onPlatform || !p.valid || !p.id || p.right-p.left<100) return false;
            const double jump=cat::appearance::VisualModifiers(state_->nutrition,state_->preferences.character).jump*cat::species::ProfileFor(state_->preferences.character).jumpHeightMultiplier;
            if(p.height-own.lift>cat::environment::WindowJumpRiseLimit(maxLift_+160,jump)) return false;
            const double target=std::clamp(c.primary.x+(own.x<c.primary.x?-65:65),p.left+35,p.right-35);
            if(!physics_.LaunchToPlatform(p.left,p.right,p.height,target,45*jump)) return false;
            supportId_=p.id; support_=p; interaction_=kind; socialUntil_=c.ticks+6000;
            storyActive_=true;quietActive_=false;
            contactRecorded_=false;
            SetMotion(cat::animation::Motion::Jump,c.ticks,6000,0,true); event_=kind; return true;
        }
        if(kind==Interaction::None || kind==Interaction::Startled || !c.primary.grounded ||
            std::abs(own.lift-c.primary.lift)>8 || std::abs(own.x-c.primary.x)>650) return false;
        if(own.onPlatform!=c.primary.onPlatform || (own.onPlatform && supportId_!=c.primaryPlatform.id)) return false;
        if(kind==Interaction::FoodCompetition && state_->nutrition.fullness>85) return false;
        interaction_=kind; socialUntil_=c.ticks+(kind==Interaction::SharedSleep?30000:kind==Interaction::RunTogether?10000:12000);
        storyActive_=true;quietActive_=false;
        contactRecorded_=false; brain_->Interrupt(c.ticks); nextDecision_=socialUntil_;
        return true;
    }
    bool FoodOffered(const Context& c) {
        // A food race is autonomous play, not acceptance of food by this cat.
        // Explicit feeding can invite a race after the minimum quiet rest,
        // while unfinished grooming/sleep and the first18s stay protected.
        const bool rested=quietActive_ && !waitingForStop_ && c.ticks>=motionStart_ &&
            c.ticks-motionStart_>=cat::behavior::Brain::NeutralRestMinMs;
        if(!rested && (c.ticks<nextDecision_ || quietActive_))return false;
        return TrySocial(Interaction::FoodCompetition,c);
    }
    cat::nutrition::FeedResult Feed(std::int64_t now,std::uint64_t ticks,bool treat,double random01=.9) {
        return FeedFood(now,ticks,treat?cat::feeding::FoodKind::Treat:cat::feeding::FoodKind::Meal,random01);
    }
    cat::nutrition::FeedResult FeedFood(std::int64_t now,std::uint64_t ticks,cat::feeding::FoodKind kind,double random01=.9) {
        if(!cat::feeding::IsValid(kind) || (kind==cat::feeding::FoodKind::Fish &&
            (!Bound() || !cat::feeding::CanOfferFish(state_->preferences.character)))) return cat::nutrition::FeedResult::TooSoon;
        if(!Bound() || !ready_ || !state_->enabled || !physics_.CurrentPose().grounded) return cat::nutrition::FeedResult::TooSoon;
        Advance(now);
        const bool treat=cat::feeding::IsTreat(kind);
        const auto result=cat::feeding::TryFeed(state_->pet,state_->nutrition,state_->memory,state_->pet.updatedUtc,kind,random01);
        if(result!=cat::nutrition::FeedResult::Accepted) return result;
        brain_->Fed(ticks,treat); interaction_=Interaction::None;storyActive_=true;quietActive_=false;
        const auto clip=kind==cat::feeding::FoodKind::Fish?cat::sprite_sequence::Clip::Fish:
            treat?cat::sprite_sequence::Clip::Treat:cat::sprite_sequence::Clip::Eat;
        const auto* authored=Registered(clip);
        const auto duration=RegisteredDuration(clip,authored?cat::sprite_sequence::CareDuration(clip,0):4200);
        SetMotion(treat?cat::animation::Motion::Treat:cat::animation::Motion::Eat,ticks,duration,0,false,clip);
        feedingKind_=kind;
        PrepareGroundArt(ticks);
        careUntil_=nextDecision_;
        return result;
    }
    bool Pet(std::int64_t now,std::uint64_t ticks) {
        if(!Bound() || !ready_ || !state_->enabled || Feeding(ticks) || !physics_.CurrentPose().grounded) return false;
        Advance(now); brain_->Pet(ticks); interaction_=Interaction::None;storyActive_=true;quietActive_=false;
        SetMotion(cat::animation::Motion::TailRaise,ticks,3400,0);
        careUntil_=nextDecision_;
        state_->pet.affection=std::min(100.0,state_->pet.affection+1);
        return cat::memory::Record(state_->memory,cat::memory::Event::Pet,state_->pet.updatedUtc);
    }
    bool Play(std::int64_t now,std::uint64_t ticks) {
        if(!Bound() || !ready_ || !state_->enabled || Feeding(ticks) || !physics_.CurrentPose().grounded) return false;
        Advance(now); brain_->Interrupt(ticks); interaction_=Interaction::None;storyActive_=true;quietActive_=false;
        SetMotion(cat::animation::Motion::ToyPaw,ticks,3000,0);
        careUntil_=nextDecision_;
        return cat::memory::Record(state_->memory,cat::memory::Event::Play,state_->pet.updatedUtc);
    }
    void DragTo(double x) {
        if(!ready_) return;
        physics_.Reset(std::clamp(x,minX_,maxX_),minX_,maxX_); supportId_=0; support_={}; gait_.Reset(); groundDrive_.Reset(facing_); poseTransition_.Reset(); jumpPresentation_.Reset(); groundArtBridge_.Reset();
        physics_.SetCeiling(maxLift_);
        state_->pet.position=std::clamp((physics_.CurrentPose().x-minX_)/(maxX_-minX_),0.0,1.0);
        interaction_=Interaction::None; nextDecision_=careUntil_=landedAt_=0;
        waitingForStop_=jumpPending_=false;
        storyActive_=quietActive_=false;
        brain_->Interrupt(lastTicks_);
        ++presentationGeneration_;
    }
    bool HoldingFinalCarePose(std::uint64_t ticks) const {
        return Bound() && ready_ && !waitingForStop_ && AuthoredCareHolding(ticks);
    }
    Snapshot View(std::uint64_t ticks) const {
        Snapshot s;
        if(!Bound() || !ready_) return s;
        s.enabled=state_->enabled; s.physical=physics_.RenderPose(); s.preferences=state_->preferences;
        s.body=cat::appearance::VisualModifiers(state_->nutrition,state_->preferences.character); s.growthScale=cat::GrowthScale(state_->pet.bornUtc,state_->pet.updatedUtc);
        s.life=cat::life::Summarize(state_->life,state_->memory.bond); s.temperament=state_->life.temperament;
        s.bond=state_->memory.bond; s.archetype=brain_->GetArchetype(); s.motion=motion_; s.interaction=interaction_;
        s.presentationTicks=presentationTicks_;
        s.presentationGeneration=presentationGeneration_+(presentationIdentity_!=state_->preferences.character?1:0);
        s.presentationPaused=presentationPaused_;
        const double relativeVelocity=s.physical.velocityX-s.physical.supportVelocityX;
        const auto displayed=cat::motion::DisplayedMotion(motion_,walkingMotion_,s.physical.grounded,waitingForStop_,relativeVelocity);
        const int displayedFacing=s.physical.grounded && std::abs(relativeVelocity)>2?(relativeVelocity<0?-1:1):facing_;
        s.motion=displayed;
        const auto elapsed=ticks>=motionStart_?ticks-motionStart_:0;
        const bool authoredSleep=Registered(cat::sprite_sequence::Clip::Sleep)!=nullptr;
        s.animation=displayed==cat::animation::Motion::Sleep?
            cat::animation::EvaluateSleep(sleepVariant_,elapsed,ticks,displayedFacing,s.physical.onPlatform && !authoredSleep):
            cat::animation::Evaluate(displayed,elapsed,ticks,displayedFacing,gait_);
        if(!s.physical.grounded) s.animation=cat::animation::AirbornePose(s.physical.velocityY,facing_);
        else if((displayed==cat::animation::Motion::Jump || displayed==cat::animation::Motion::Pounce) && jumpPending_) {
            s.animation=cat::animation::JumpPreparationPose(elapsed,displayedFacing,
                displayed==cat::animation::Motion::Pounce);
        }
        else if((displayed==cat::animation::Motion::Jump || displayed==cat::animation::Motion::Pounce) && !jumpPending_) {
            s.animation=cat::animation::LandingPose(landedAt_ && ticks>=landedAt_?ticks-landedAt_:280,landingStrength_,displayedFacing);
        }
        if(s.physical.grounded && s.physical.onPlatform && std::abs(s.physical.supportVelocityX)>2 &&
                s.animation.frame==4 && s.animation.blendFrame<0 && !s.animation.bellyRig &&
                s.animation.groundContact && !s.animation.centerPivot) {
            // A moving support carries the physical feet. Balance bends the
            // neck/chest locally; rotating the whole sprite would lift them.
            s.animation.joints.headNod=std::clamp(s.animation.joints.headNod+
                std::clamp(-s.physical.supportVelocityX/300.0,-.2,.2),-1.0,1.0);
            s.animation.joints.crouch=std::max(s.animation.joints.crouch,
                std::min(std::abs(s.physical.supportVelocityX)/500.0,.16));
        }
        if(displayed==cat::animation::Motion::Groom && s.physical.grounded && !waitingForStop_) {
            s.animation=cat::cursor::GroomPose(elapsed,displayedFacing,nextDecision_>=motionStart_?nextDecision_-motionStart_:0);
        }
        if(displayed==cat::animation::Motion::Stretch && wakingFromSleep_ && sleepVariant_==0 &&
                s.physical.grounded && (!s.physical.onPlatform || authoredSleep) &&
                !waitingForStop_) {
            cat::sprite_sequence::Options sprite;sprite.sleep=cat::sprite_sequence::SleepStage::Waking;
            cat::animation::AttachSprite(s.animation,cat::sprite_sequence::Clip::Sleep,elapsed,sprite);
        }
        s.animation=poseTransition_.Apply(s.animation,ticks,motionSerial_,
            static_cast<unsigned>(s.preferences.character),s.enabled,motionStart_);
        s.animation.fishMeal=displayed==cat::animation::Motion::Eat && s.physical.grounded &&
            feedingKind_==cat::feeding::FoodKind::Fish && cat::feeding::CanOfferFish(s.preferences.character);
        if(s.animation.fishMeal)cat::animation::AttachSprite(s.animation,cat::sprite_sequence::Clip::Fish,elapsed);
        if(s.animation.spriteClip==cat::sprite_sequence::Clip::Groom ||
                s.animation.spriteClip==cat::sprite_sequence::Clip::Eat ||
                s.animation.spriteClip==cat::sprite_sequence::Clip::Treat ||
                s.animation.spriteClip==cat::sprite_sequence::Clip::Fish ||
                s.animation.spriteClip==cat::sprite_sequence::Clip::Punch)
            s.animation.spriteOptions.durationMs=nextDecision_>=motionStart_?nextDecision_-motionStart_:0;
        const auto gaitClip=cat::animation::AuthoredLocomotionClip(gait_);
        cat::animation::ApplyAuthoredLocomotion(s.animation,displayed,gait_,Registered(gaitClip)!=nullptr);
        if((displayed==cat::animation::Motion::Idle || displayed==cat::animation::Motion::Watch) &&
                s.physical.grounded && s.animation.spriteClip==cat::sprite_sequence::Clip::None &&
                Registered(cat::sprite_sequence::Clip::Sleep)) {
            s.animation.spriteClip=cat::sprite_sequence::Clip::Sleep;
            s.animation.spriteFrame=0;s.animation.spriteTimed=false;
        }
        if(const auto* walk=RegisteredVideoWalk();walk && s.physical.grounded &&
                (cat::animation::Walking(motion_) || motion_==cat::animation::Motion::Idle ||
                 motion_==cat::animation::Motion::Watch ||
                 groundArtBridge_.Stopping() ||
                 ((VideoSideMeal() || (jumpPending_ && (motion_==cat::animation::Motion::Jump ||
                    motion_==cat::animation::Motion::Pounce))) && groundArtBridge_.Starting()))) {
            const auto bridge=groundArtBridge_.Sample(*walk);
            if(bridge.valid) {
                s.animation.spriteClip=cat::sprite_sequence::Clip::Walk;s.animation.spriteFrame=bridge.frame;
                s.animation.spriteTimed=false;s.animation.fishMeal=false;
                cat::animation::UseAuthoredSprite(s.animation);
            }
        }
        if(StationaryLanding(ticks,s.physical)) {
            cat::sprite_sequence::Options sprite;
            sprite.jump=cat::sprite_sequence::JumpPhase::Landing;
            sprite.contactElapsedMs=ticks-landedAt_;
            cat::animation::AttachSprite(s.animation,cat::sprite_sequence::Clip::Jump,0,sprite);
            s.animation.fishMeal=false;
        }
        const auto* referenceSleep=Registered(cat::sprite_sequence::Clip::Sleep);
        cat::animation::ApplyReferenceSleepHold(s.animation,displayed,elapsed,s.physical.grounded,
            cat::animation::ReferenceSleepIntroMs(referenceSleep?&referenceSleep->program:nullptr,
                referenceSleep?referenceSleep->frameCount:0),
            !waitingForStop_&&!groundArtBridge_.Starting()&&!groundArtBridge_.Stopping()&&
                !StationaryLanding(ticks,s.physical)&&s.animation.spriteClip!=cat::sprite_sequence::Clip::Jump);
        cat::animation::ApplyReferenceNeutralHold(s.animation,displayed,s.physical.grounded,relativeVelocity,
            !waitingForStop_&&groundArtBridge_.Neutral()&&!jumpPending_&&!StationaryLanding(ticks,s.physical)&&
                interaction_==Interaction::None,elapsed);
        if(Registered(s.animation.spriteClip))cat::animation::UseAuthoredSprite(s.animation);
        if(s.animation.spriteClip==cat::sprite_sequence::Clip::Jump && RegisteredVideoWalk()) {
            s.animation.jumpRig=true;
            if(!s.physical.grounded)s.animation.jumpControls=jumpPresentation_.Flight(s.physical.velocityY,s.physical.lift,s.growthScale);
            else if(s.animation.spriteOptions.jump==cat::sprite_sequence::JumpPhase::Landing)
                s.animation.jumpControls=jumpPresentation_.Land(s.animation.spriteOptions.contactElapsedMs,landingStrength_,s.physical.lift);
            else {
                const auto* jump=Registered(cat::sprite_sequence::Clip::Jump);
                const auto preparation=jump?cat::sprite_sequence::Duration(jump->program,jump->program.intro):cat::jump_animation::kPreparationMs;
                s.animation.jumpControls=jumpPresentation_.Prepare(elapsed,preparation,s.physical.lift);
            }
        } else if(s.physical.grounded)jumpPresentation_.Ground(s.physical.lift);
        return s;
    }
private:
    State* state_=nullptr;
    void PrepareCompletedJumpReturn(std::uint64_t ticks,const cat::physics::Pose& physical) {
        using Clip=cat::sprite_sequence::Clip;using Motion=cat::animation::Motion;
        const auto* walk=RegisteredVideoWalk();const auto* jump=Registered(Clip::Jump);
        const auto turn=walk?cat::video_ground_bridge::JumpTurns(*walk):cat::video_ground_bridge::JumpTurnProfile{};
        if(!turn.valid||!jump||!physical.grounded||!landedAt_||jumpTurnHandledLanding_==landedAt_||ticks<landedAt_||
                ticks-landedAt_<cat::sprite_sequence::Duration(jump->program,jump->program.outro))return;
        if(motion_==Motion::Idle||motion_==Motion::Watch) {
            const auto visible=physics_.RenderPose();
            if(std::abs(visible.velocityX-visible.supportVelocityX)>.5)return;
            groundArtBridge_.Configure(cat::sprite_sequence::Duration(walk->program,walk->program.intro),
                cat::sprite_sequence::Duration(walk->program,walk->program.outro),walk->movementDelayMs,turn.sideEntryMs);
            groundArtBridge_.BeginConnectedJumpExit(ticks,*walk);waitingForStop_=true;
            jumpTurnHandledLanding_=landedAt_;
        } else if(motion_!=Motion::Jump&&motion_!=Motion::Pounce)jumpTurnHandledLanding_=landedAt_;
    }
    void PrepareGroundArt(std::uint64_t ticks,bool eligible=true) {
        const auto* videoWalk=RegisteredVideoWalk();
        const bool sideMeal=VideoSideMeal();
        const auto jumpTurn=videoWalk?cat::video_ground_bridge::JumpTurns(*videoWalk):cat::video_ground_bridge::JumpTurnProfile{};
        const bool jumpPreparation=jumpTurn.valid && jumpPending_ &&
            (motion_==cat::animation::Motion::Jump || motion_==cat::animation::Motion::Pounce);
        if(videoWalk && eligible && !physics_.Hanging()) {
            groundArtBridge_.Configure(cat::sprite_sequence::Duration(videoWalk->program,videoWalk->program.intro),
                cat::sprite_sequence::Duration(videoWalk->program,videoWalk->program.outro),
                jumpPreparation?jumpTurn.entryMs:videoWalk->movementDelayMs,jumpTurn.sideEntryMs);
            const auto visible=physics_.RenderPose();
            const bool travel=cat::animation::Walking(motion_) && !state_->pet.roamingPaused;
            if(travel && groundArtBridge_.Neutral() && std::abs(visible.velocityX-visible.supportVelocityX)<=.5 && direction_!=facing_) {
                facing_=direction_;groundDrive_.Reset(direction_);
            }
            groundArtBridge_.Update(ticks,travel && direction_==facing_,
                visible.grounded,visible.velocityX-visible.supportVelocityX,true,sideMeal||jumpPreparation,jumpPreparation);
            if(groundArtBridge_.Stopping() || groundArtBridge_.StartingForSideAction())waitingForStop_=true;
        } else groundArtBridge_.Reset(ticks);
    }
    const cat::sprite_assets::Descriptor* RegisteredVideoWalk() const {
        const auto* walk=Registered(cat::sprite_sequence::Clip::Walk);
        return walk && walk->nominalFramesPerSecond>0 && walk->program.intro.count &&
            walk->program.outro.count ? walk : nullptr;
    }
    bool VideoSideMeal() const {
        const auto* eat=Registered(cat::sprite_sequence::Clip::Eat);
        return motion_==cat::animation::Motion::Eat && feedingKind_==cat::feeding::FoodKind::Meal &&
            eat && eat->nominalFramesPerSecond>0;
    }
    const cat::sprite_assets::Descriptor* reviewWalk_=nullptr;
    std::unique_ptr<cat::behavior::Brain> brain_;
    cat::physics::Controller physics_;
    cat::animation::Locomotion gait_;
    cat::motion::GroundDrive groundDrive_;
    mutable cat::animation::PoseTransition poseTransition_;
    mutable cat::jump_animation::Driver jumpPresentation_;
    cat::video_ground_bridge::GroundBridge groundArtBridge_;
    bool ready_=false,contactRecorded_=false,waitingForStop_=false,jumpPending_=false;
    bool wakingFromSleep_=false,storyActive_=false,quietActive_=false;
    bool presentationPaused_=false;
    std::uint64_t presentationTicks_=0,presentationGeneration_=0;
    cat::Character presentationIdentity_=cat::Character::Cream;
    double minX_=40,maxX_=1000,maxLift_=700,speed_=0,landingStrength_=1;
    int direction_=1,facing_=1;
    unsigned sleepVariant_=0,groomVariant_=0;
    std::uint64_t rng_=1,nextDecision_=0,motionStart_=0,socialUntil_=0,lastTicks_=0,careUntil_=0,landedAt_=0,jumpTurnHandledLanding_=0;
    std::uint64_t motionSerial_=0;
    cat::random_zoomies::Scheduler randomZoomies_;
    std::uint64_t zoomiesRng_=1,randomZoomiesOwner_=0;
    double randomZoomiesMsRemainder_=0;
    std::uintptr_t supportId_=0;
    Platform support_;
    Context lastContext_;
    cat::animation::Motion motion_=cat::animation::Motion::Idle;
    cat::animation::Motion walkingMotion_=cat::animation::Motion::Walk;
    cat::feeding::FoodKind feedingKind_=cat::feeding::FoodKind::Meal;
    Interaction interaction_=Interaction::None,event_=Interaction::None;
    double Random() { rng_^=rng_>>12; rng_^=rng_<<25; rng_^=rng_>>27; return ((rng_*2685821657736338717ULL)>>11)*(1.0/9007199254740992.0); }
    const cat::sprite_assets::Descriptor* Registered(cat::sprite_sequence::Clip clip) const {
        if(Bound() && reviewWalk_ && clip==cat::sprite_sequence::Clip::Walk &&
                cat::sprite_assets::AuthoredCharacter(static_cast<unsigned>(state_->preferences.character))==0)return reviewWalk_;
        return Bound()?cat::sprite_assets::Find(clip,static_cast<unsigned>(state_->preferences.character),
            cat::sprite_assets::kCareSprites):nullptr;
    }
    std::uint64_t RegisteredDuration(cat::sprite_sequence::Clip clip,std::uint64_t fallback) const {
        const auto* asset=Registered(clip);
        return asset && asset->program.stepCount?cat::sprite_sequence::ProgramDuration(asset->program):fallback;
    }
    bool AuthoredCareHolding(std::uint64_t ticks) const {
        using Clip=cat::sprite_sequence::Clip;using Motion=cat::animation::Motion;
        const auto clip=motion_==Motion::Groom?Clip::Groom:motion_==Motion::Eat?
            (feedingKind_==cat::feeding::FoodKind::Fish?Clip::Fish:Clip::Eat):
            motion_==Motion::Treat?Clip::Treat:motion_==Motion::Punch?Clip::Punch:Clip::None;
        const auto* asset=Registered(clip);
        if(!asset || !asset->program.stepCount || ticks<motionStart_)return false;
        cat::sprite_sequence::Options options;options.sheetFrames=asset->frameCount;
        options.durationMs=nextDecision_>=motionStart_?nextDecision_-motionStart_:0;
        options.grounded=physics_.CurrentPose().grounded;
        return cat::sprite_sequence::HoldsFinalCarePose(clip,asset->program,ticks-motionStart_,options);
    }
    bool StationaryLanding(std::uint64_t ticks,const cat::physics::Pose& physical) const {
        if(groundArtBridge_.JumpReturning())return false; // Recovery already finished; the real turn now owns presentation.
        const auto* asset=Registered(cat::sprite_sequence::Clip::Jump);
        const auto duration=asset && asset->program.stepCount?
            cat::sprite_sequence::Duration(asset->program,asset->program.outro):0;
        // Preserve the final landing drawing while a newly accepted action
        // finishes its short planting gate. Never flash Walk between recovery
        // and the meal; the bounded extension cannot resurrect an old landing.
        return duration && landedAt_ && ticks>=landedAt_ &&
            (ticks-landedAt_<duration || (waitingForStop_ && ticks-landedAt_<duration+500)) &&
            physical.grounded && std::abs(physical.velocityX-physical.supportVelocityX)<=2;
    }
    void BeginQuiet(std::uint64_t ticks) {
        storyActive_=false;quietActive_=true;
        SetMotion(cat::animation::Motion::Idle,ticks,
            cat::behavior::Brain::NeutralRestDuration(static_cast<std::uint32_t>(Random()*4294967295.0)),0);
        // A second Idle can keep the same drawing; this rest still has a new
        // start for minimum-rest checks and for freezing its braking interval.
        motionStart_=ticks;
    }
    void FinishSocial(const Context& c) {
        interaction_=Interaction::None;brain_->Interrupt(c.ticks);
        if(!physics_.CurrentPose().grounded) {
            // Let the current real trajectory finish before a neutral rest.
            storyActive_=true;quietActive_=false;
            nextDecision_=std::max(nextDecision_,c.ticks+280);
        } else if(motion_==cat::animation::Motion::Sleep) {
            storyActive_=true;quietActive_=false;
            SetMotion(cat::animation::Motion::Stretch,c.ticks,3200,0);
        } else BeginQuiet(c.ticks);
    }
    void SetSocialMotion(cat::animation::Motion motion,const Context& c,std::uint64_t duration,double speed) {
        // Steering is sampled every tick; one phase is still one action.
        if(motion_!=motion)SetMotion(motion,c.ticks,duration,speed);
        else speed_=speed;
    }
    bool Feeding(std::uint64_t ticks) const {
        return ticks<careUntil_ && (motion_==cat::animation::Motion::Eat || motion_==cat::animation::Motion::Treat);
    }
    static bool Busy(const Context& c,Interaction kind) {
        return c.primaryBusy && !(kind==Interaction::FoodCompetition &&
            (c.primaryMotion==cat::animation::Motion::Eat || c.primaryMotion==cat::animation::Motion::Treat));
    }
    void Advance(std::int64_t now) {
        now=std::clamp(now,state_->pet.bornUtc,cat::memory::kMaxUtc);
        now=std::max({now,state_->pet.updatedUtc,state_->nutrition.updatedUtc,state_->memory.updatedUtc,state_->life.updatedUtc});
        cat::Advance(state_->pet,now); cat::nutrition::Advance(state_->nutrition,now); cat::memory::Advance(state_->memory,now);
        state_->pet.hunger=state_->nutrition.fullness;
    }
    void SetMotion(cat::animation::Motion m,std::uint64_t ticks,std::uint64_t duration,double speed,bool alreadyAirborne=false,
                   cat::sprite_sequence::Clip requestedClip=cat::sprite_sequence::Clip::None) {
        using Clip=cat::sprite_sequence::Clip;
        const auto* returnWalk=RegisteredVideoWalk();
        const auto returnTurn=returnWalk?cat::video_ground_bridge::JumpTurns(*returnWalk):cat::video_ground_bridge::JumpTurnProfile{};
        const auto* returnJump=Registered(Clip::Jump);
        const bool jumpReturn=returnTurn.valid && returnJump && physics_.RenderPose().grounded &&
            std::abs(physics_.RenderPose().velocityX-physics_.RenderPose().supportVelocityX)<=.5 &&
            (motion_==cat::animation::Motion::Jump || motion_==cat::animation::Motion::Pounce) && !jumpPending_ &&
            landedAt_ && ticks>=landedAt_ &&
            ticks-landedAt_>=cat::sprite_sequence::Duration(returnJump->program,returnJump->program.outro) &&
            (m==cat::animation::Motion::Idle || m==cat::animation::Motion::Watch);
        if(requestedClip==Clip::None) {
            if(m==cat::animation::Motion::Groom)requestedClip=Clip::Groom;
            else if(m==cat::animation::Motion::Punch)requestedClip=Clip::Punch;
            else if(m==cat::animation::Motion::Eat)requestedClip=Clip::Eat;
            else if(m==cat::animation::Motion::Treat)requestedClip=Clip::Treat;
        }
        if(requestedClip!=Clip::None)duration=RegisteredDuration(requestedClip,duration);
        if(motion_==cat::animation::Motion::Sleep && m==cat::animation::Motion::Stretch) {
            if(const auto* sleep=Registered(Clip::Sleep)) {
                const auto wake=sleep->program.stepCount?
                    cat::sprite_sequence::Duration(sleep->program,sleep->program.outro):cat::sprite_sequence::kSleepWakeMs;
                duration=std::max(duration,wake+500);
            }
        }
        const bool jumping=m==cat::animation::Motion::Jump || m==cat::animation::Motion::Pounce;
        const bool authoredGroom=m==cat::animation::Motion::Groom &&
            cat::sprite_assets::Find(cat::sprite_sequence::Clip::Groom,
                static_cast<unsigned>(state_->preferences.character),cat::sprite_assets::kCareSprites);
        // Food reaches SetMotion only after acceptance. A long menu can
        // preserve an Eat/Treat pose while UTC passes the feeding cooldown;
        // its next accepted serving is a new action, even of the same kind.
        const bool freshFood=m==cat::animation::Motion::Eat || m==cat::animation::Motion::Treat;
        if(motion_!=m || jumping || authoredGroom || freshFood) {
            wakingFromSleep_=motion_==cat::animation::Motion::Sleep && m==cat::animation::Motion::Stretch;
            feedingKind_=cat::feeding::FoodKind::Meal;
            ++motionSerial_;
            motionStart_=ticks; motion_=m;
            if(authoredGroom)groomVariant_=std::min(2u,static_cast<unsigned>(Random()*3));
            if(m==cat::animation::Motion::Sleep) sleepVariant_=Registered(Clip::Sleep)?0:
                cat::sprite_sequence::SelectSleepVariant(Random(),false);
            const auto physical=physics_.CurrentPose();
            const auto rendered=physics_.RenderPose();
            waitingForStop_=cat::motion::BeginGroundStop(m,physical.grounded,physical.velocityX-physical.supportVelocityX,
                rendered.velocityX-rendered.supportVelocityX,groundDrive_,alreadyAirborne);
            jumpPending_=jumping && !alreadyAirborne;
        }
        if(cat::animation::Walking(m)) walkingMotion_=m;
        nextDecision_=ticks+duration; speed_=speed;
        if(m!=cat::animation::Motion::Eat)PrepareGroundArt(ticks);
        if(jumpReturn) {
            groundArtBridge_.BeginConnectedJumpExit(ticks,*returnWalk);
            waitingForStop_=true;
            jumpTurnHandledLanding_=landedAt_;
        }
    }
    static cat::animation::Motion MotionFor(cat::behavior::Action action) {
        using A=cat::behavior::Action; using M=cat::animation::Motion;
        switch(action) {
        case A::Walk:return M::Walk; case A::Sleep:return M::Sleep; case A::Groom:return M::Groom; case A::Stretch:return M::Stretch;
        case A::Watch:return M::Watch; case A::Stalk:return M::Stalk; case A::Pounce:return M::Pounce; case A::Zoomies:return M::Zoomies;
        case A::TailChase:return M::TailChase; case A::Punch:return M::Punch; case A::Jump:return M::Jump; case A::EarFlick:return M::EarFlick;
        case A::TailRaise:return M::TailRaise; case A::BellyRoll:return M::BellyRoll; case A::Loaf:return M::Loaf; case A::Knead:return M::Knead;
        case A::Peek:return M::Peek; case A::Scratch:return M::Scratch; case A::Sneeze:return M::Sneeze; default:return M::Idle;
        }
    }
    std::uint64_t DrawZoomies(std::uint64_t low,std::uint64_t high) {
        // Independent stream: adding this event does not change social or
        // personality choices made by the existing birthday-derived RNG.
        zoomiesRng_^=zoomiesRng_>>12;zoomiesRng_^=zoomiesRng_<<25;zoomiesRng_^=zoomiesRng_>>27;
        return low+(zoomiesRng_*2685821657736338717ULL)%(high-low+1);
    }
    void AdvanceRandomZoomies(const Context& c,const cat::physics::Pose& physical,bool landingRecovery) {
        using Motion=cat::animation::Motion;
        const bool owned=motion_==Motion::Zoomies && motionSerial_==randomZoomiesOwner_ &&
            interaction_==Interaction::None && !state_->pet.roamingPaused && !physical.onPlatform;
        const bool cancelled=randomZoomies_.Active()&&!owned;
        const bool canStart=quietActive_ && c.ticks>=nextDecision_ &&
            (motion_==Motion::Idle || motion_==Motion::Watch) && physical.grounded && !physical.onPlatform &&
            !landingRecovery && !waitingForStop_ && !jumpPending_ && interaction_==Interaction::None &&
            c.ticks>=careUntil_ && !state_->pet.roamingPaused && c.primaryVisible && !c.primaryBusy;
        const bool moving=owned && physical.grounded && !waitingForStop_ &&
            !groundArtBridge_.Starting() && !groundArtBridge_.Stopping() && direction_==facing_ &&
            std::abs(physical.velocityX-physical.supportVelocityX)>10;
        const double preciseMs=(state_->pet.roamingPaused?0:std::max(0.0,c.elapsedSeconds*1000))+randomZoomiesMsRemainder_;
        const auto elapsed=static_cast<std::uint64_t>(preciseMs);
        randomZoomiesMsRemainder_=preciseMs-elapsed;
        const auto event=randomZoomies_.Update(elapsed,!state_->pet.roamingPaused,canStart,moving,owned,
            [&](std::uint64_t low,std::uint64_t high){return DrawZoomies(low,high);});
        if(event.started) {
            brain_->Interrupt(c.ticks);storyActive_=true;quietActive_=false;
            direction_=physical.x<=(minX_+maxX_)*.5?1:-1;
            const auto* run=Registered(cat::sprite_sequence::Clip::Run);
            const double native=run?cat::sprite_assets::NominalLocomotionSpeed(*run):0;
            SetMotion(Motion::Zoomies,c.ticks,60000,native>0?native:130);
            randomZoomiesOwner_=motionSerial_;
        } else if(event.turn) {
            // The common motor and ground bridge brake, plant and turn before
            // accepting this target; there is no root-coordinate correction.
            direction_=-direction_;
        } else if(event.finished || (cancelled && motion_==Motion::Zoomies &&
                motionSerial_==randomZoomiesOwner_ && interaction_==Interaction::None)) {
            BeginQuiet(c.ticks);
        }
    }
    void Choose(const Context& c) {
        cat::behavior::Context context; context.ticks=c.ticks; context.ageYears=cat::Age(state_->pet.bornUtc,state_->pet.updatedUtc).years;
        context.hunger=state_->nutrition.fullness; context.affection=state_->pet.affection; context.localHour=c.localHour; context.roamingPaused=state_->pet.roamingPaused;
        const auto plan=brain_->Next(context); auto motion=MotionFor(plan.action);
        // Scheduled zoomies own the 1–5 minute random interval. Explicit paired
        // running remains a separate social interaction.
        if(motion==cat::animation::Motion::Zoomies)motion=cat::animation::Motion::Walk;
        storyActive_=true;quietActive_=false;
        if(Random()<.3) direction_=-direction_;
        double selectedSpeed=plan.speed;
        if(motion==cat::animation::Motion::Walk || motion==cat::animation::Motion::Zoomies) {
            const auto* artwork=Registered(motion==cat::animation::Motion::Walk?
                cat::sprite_sequence::Clip::Walk:cat::sprite_sequence::Clip::Run);
            const double native=artwork?cat::sprite_assets::NominalLocomotionSpeed(*artwork):0;
            if(native>0)selectedSpeed=native;
        }
        SetMotion(motion,c.ticks,plan.durationMs,selectedSpeed);
    }
    void UpdateSupport(const Context& c) {
        if(!supportId_) return;
        const auto candidate=c.supportPlatform.valid && c.supportPlatform.id==supportId_?c.supportPlatform:
            c.primaryPlatform.valid && c.primaryPlatform.id==supportId_?c.primaryPlatform:Platform{};
        if(candidate.valid) { support_=candidate; physics_.SetPlatform(candidate.left,candidate.right,candidate.height); }
        else {
            physics_.RemovePlatform(); supportId_=0; support_={}; interaction_=Interaction::None; event_=Interaction::Startled;
            brain_->Interrupt(c.ticks);storyActive_=true;quietActive_=false;
            SetMotion(cat::animation::Motion::Jump,c.ticks,2500,0,true);
        }
    }
    void RecordContact(const Context& c) {
        if(contactRecorded_) return;
        contactRecorded_=true; state_->lastSocialUtc=state_->pet.updatedUtc; event_=interaction_;
        const auto increment=[](std::uint64_t& value) { if(value<cat::memory::kMaxCount) ++value; };
        if(interaction_==Interaction::Sniff) increment(state_->sniffs);
        if(interaction_==Interaction::Paw || interaction_==Interaction::RunTogether) increment(state_->mutualPlay);
        if(interaction_==Interaction::SharedSleep) increment(state_->sharedSleeps);
        if(interaction_==Interaction::FoodCompetition) increment(state_->foodRaces);
        (void)c; // Social play never manufactures human-care or food credits.
    }
    double SocialVelocity(const Context& c) {
        const auto own=physics_.CurrentPose();
        if(interaction_==Interaction::WindowFollow) {
            if(own.onPlatform) { RecordContact(c); interaction_=Interaction::None; BeginQuiet(c.ticks); }
            return 0;
        }
        const double delta=c.primary.x-own.x;
        const double spacing=interaction_==Interaction::Paw?48:interaction_==Interaction::SharedSleep?65:60;
        if(std::abs(own.lift-c.primary.lift)>8 || !c.primary.grounded) { FinishSocial(c); return 0; }
        const bool gesture=motion_==cat::animation::Motion::ToyPaw || motion_==cat::animation::Motion::Sniff ||
            motion_==cat::animation::Motion::Knead || motion_==cat::animation::Motion::Sleep;
        if(gesture && (waitingForStop_ || c.ticks<nextDecision_)) {
            if(!waitingForStop_ && groundDrive_.Settled() &&
                    cat::motion::ReadyToStart(own.grounded,own.velocityX-own.supportVelocityX))RecordContact(c);
            return 0;
        }
        if(std::abs(delta)>spacing+5) {
            direction_=delta<0?-1:1;
            const bool running=interaction_==Interaction::RunTogether || interaction_==Interaction::FoodCompetition;
            SetSocialMotion(running?cat::animation::Motion::Zoomies:cat::animation::Motion::Walk,c,2000,running?80:28);
            return direction_*speed_;
        }
        if(std::abs(delta)<30) {
            direction_=delta<0?1:-1;
            SetSocialMotion(cat::animation::Motion::Walk,c,1500,18);
            return direction_*speed_;
        }
        facing_=delta<0?-1:1;
        const double primarySpeed=c.primary.velocityX-c.primary.supportVelocityX;
        if(interaction_==Interaction::RunTogether && std::abs(primarySpeed)>5) {
            RecordContact(c);
            // Box2D adds the supporting window's velocity exactly once. The
            // paired target already is a real speed, not a second species/body
            // multiplier applied to the first pet's measured movement.
            const double multiplier=cat::appearance::VisualModifiers(state_->nutrition,state_->preferences.character).speed*
                cat::species::ProfileFor(state_->preferences.character).movementSpeedMultiplier;
            const double target=std::clamp(primarySpeed,-140.0,140.0)/std::max(.1,multiplier);
            SetSocialMotion(cat::animation::Motion::Zoomies,c,2000,std::abs(target));
            return target;
        }
        const auto motion=interaction_==Interaction::Paw?cat::animation::Motion::ToyPaw:
            interaction_==Interaction::SharedSleep?cat::animation::Motion::Sleep:
            interaction_==Interaction::Sniff?cat::animation::Motion::Sniff:
            interaction_==Interaction::FoodCompetition?cat::animation::Motion::Knead:cat::animation::Motion::Watch;
        SetSocialMotion(motion,c,motion==cat::animation::Motion::Sleep?
            std::max<std::uint64_t>(1,socialUntil_>c.ticks?socialUntil_-c.ticks:0):2000,0);
        if(!waitingForStop_ && groundDrive_.Settled() &&
                cat::motion::ReadyToStart(own.grounded,own.velocityX-own.supportVelocityX)) RecordContact(c);
        return 0;
    }
};

} // namespace cat::companion

#ifdef _WIN32
#include "cat_companion_window.hpp"
#endif
