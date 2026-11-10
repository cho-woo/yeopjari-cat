#pragma once

// Included after the production host functions, only for --self-test. These
// fixtures call those functions directly: no OS cursor movement, application
// window, AppData loading, update worker, or enabled persistence store.
namespace native_cursor_tests {
using Action = cat::environment::CursorAction;

struct Host {
    ULONGLONG now = 100000;
    unsigned contacts = 0, launches = 0, landingContacts = 0;
    bool continuousTakeoff = true;
    POINT pointer{};

    void Reset(cat::Character character = cat::Character::Cream) {
        g.initialized = false; g.window = nullptr;
        g.hidden = g.menuOpen = g.dragging = g.closing = false;
        updates.applying = false;
        g.memory.outcome = cat::memory::storage::Outcome::Blocked;
        g.life.outcome = cat::life::storage::Outcome::Blocked;
        g.companionStore.state.enabled = false;
        g.brain.reset(); g.lifeSequenceCount = g.memorySequenceCount = 0;
        g.lifeCrossing = {}; g.smallBoxEvent = g.lifeDispatching = false;
        g.toy = ToyKind::None; g.toyEngaged = g.toyDispatching = g.approachingToy = false;
        g.toySession.Cancel(); g.automaticToy = {};
        g.approachingUser = false; g.pendingCare = -1;
        g.preferences = {character, cat::Outfit::None};
        const auto utc = UtcNow();
        g.pet = cat::NewPet(utc - 366 * cat::life::Day); g.pet.updatedUtc = utc;
        g.nutrition = cat::nutrition::NewState(g.pet.bornUtc, utc, 80);
        // Nonzero monitor origin and DPI exercise the real screen-to-world map.
        g.work = {120, -180, 2040, 1170}; g.dpiScale = 1.5;
        g.centerX = g.work.left + 400*g.dpiScale; g.direction = g.facing = 1;
        g.workInterference = true; g.lifeInteractionUntil = 0;
        cursorInteraction = {}; ResetMovement();
        for (int frame = 0; frame < 12; ++frame) g.physics.Update(1.0/60);
        now = 100000; contacts = launches = landingContacts = 0;
        continuousTakeoff = true;
        SetMotion(Motion::Idle, 8000); RebaseMotion();
    }

    void RebaseMotion() {
        const auto duration = g.nextDecision - g.motionStart;
        g.motionStart = now; g.nextDecision = now + duration;
    }

    POINT Screen(double x, double height) const {
        return {static_cast<LONG>(std::lround(g.work.left + x*g.dpiScale)),
            static_cast<LONG>(std::lround(g.work.bottom - height*g.dpiScale))};
    }

    void Sample(double x, double height, ULONGLONG elapsed = 16, bool available = true) {
        now += elapsed; pointer = Screen(x,height);
        const auto serial = g.motionSerial;
        UpdateCursorReactionSample(now, elapsed, pointer, available);
        // SetMotion reads the real system clock. Rebase only a new action
        // issued by this host call, never an externally replaced action.
        if (g.motionSerial != serial) {
            RebaseMotion();
            cursorInteraction.session.AdoptMotion(g.motionStart, g.waitingForStop, g.motionSerial);
        }
    }

    bool Frame(double x, double height, bool available = true) {
        Sample(x,height,16,available);
        cat::motion::AdvanceStopGate(g.waitingForStop, g.physics.CurrentPose().grounded,
            WalkingVelocity(g.physics.CurrentPose()), now, g.motionStart, g.nextDecision);
        const auto body = cat::nutrition::VisualModifiers(g.nutrition);
        g.physics.SetTargetVelocity(g.waitingForStop ? 0 : CursorReactionVelocity()*body.speed*
            cat::species::ProfileFor(g.preferences.character).movementSpeedMultiplier);
        if (!g.waitingForStop && g.jumpPending && now-g.motionStart >= PrimaryTakeoffMs(true)) {
            g.jumpPending = false;
            const auto before = g.physics.CurrentPose();
            if (TryCursorPounce(now)) {
                ++launches; g.jumpLaunched = true; g.airborneMotion = g.motion;
                const auto after = g.physics.CurrentPose();
                continuousTakeoff = continuousTakeoff && before.x == after.x && before.lift == after.lift;
            }
        }
        g.physics.Update(.016);
        const auto physical = g.physics.CurrentPose();
        const bool contact = UpdateCursorContact(now,physical);
        if (contact) { ++contacts; if (physical.landed) ++landingContacts; }
        g.centerX = g.work.left + g.physics.RenderPose().x*g.dpiScale;
        return contact;
    }

    const cat::sprite_assets::Descriptor* PunchArt() const {
        const auto* asset=LoadedCareSprite(cat::sprite_sequence::Clip::Punch,g.preferences.character);
        return asset && asset->layout->program.stepCount?asset->layout:nullptr;
    }
    ULONGLONG PawDuration() const {
        const auto* asset=PunchArt();
        return asset?cat::sprite_sequence::ProgramDuration(asset->program):
            cat::cursor::DurationMs(cat::cursor::AttackKind::Gentle);
    }
    unsigned PawFrames() const { return static_cast<unsigned>((PawDuration()+15)/16+2); }
    unsigned ExpectedPawContacts() const {
        const auto* asset=PunchArt();
        if(!asset)return 2;
        unsigned count=0;
        for(unsigned i=0;i<asset->program.cueCount;++i)
            if(asset->program.cues[i].events&cat::sprite_sequence::Contact)++count;
        return count;
    }
    ULONGLONG FirstPawContactMs() const {
        const auto* asset=PunchArt();
        if(!asset)return 220;
        for(unsigned i=0;i<asset->program.cueCount;++i) {
            const auto& cue=asset->program.cues[i];
            if(!(cue.events&cat::sprite_sequence::Contact))continue;
            ULONGLONG time=cue.offsetMs;
            for(unsigned step=0;step<cue.step;++step)time+=asset->program.steps[step].durationMs;
            return time;
        }
        return PawDuration();
    }
    unsigned PawContactIndex() const {
        if(now<g.motionStart)return 0;
        if(const auto* asset=PunchArt()) {
            cat::sprite_sequence::Options options;options.sheetFrames=asset->frameCount;
            return cat::sprite_sequence::ProgramContactIndex(cat::sprite_sequence::Clip::Punch,
                asset->program,now-g.motionStart,options);
        }
        return static_cast<unsigned>(cat::cursor::ContactIndex(cat::cursor::AttackKind::Gentle,now-g.motionStart));
    }
    double PawX() const {
        if(PunchArt())return g.physics.CurrentPose().x+g.direction*38.15*
            cat::nutrition::VisualModifiers(g.nutrition).bodyWidth*cat::GrowthScale(g.pet.bornUtc,UtcNow());
        const auto geometry = CursorAttackGeometry();
        return g.physics.CurrentPose().x + (geometry.pawAnchorX-.5)*geometry.width*
            cat::GrowthScale(g.pet.bornUtc,UtcNow());
    }
    double PawHeight() const {
        if(PunchArt())return 38.89*cat::nutrition::VisualModifiers(g.nutrition).bodyHeight*
            cat::GrowthScale(g.pet.bornUtc,UtcNow());
        return .04*CursorAttackGeometry().height*cat::GrowthScale(g.pet.bornUtc,UtcNow());
    }

    bool StartPaw(double height) {
        const double target = PawX();
        for (int frame = 0; frame < 40; ++frame) {
            Frame(target + (frame%2 ? 3 : -3),height);
            if (cursorInteraction.session.Action() == Action::Paw)
                return g.motion == Motion::ToyPaw && !g.waitingForStop && contacts == 0;
        }
        return false;
    }

    bool ReachPounce() {
        double chaseTarget = 0;
        bool chased = false, tapped = false, punched = false;
        for (int frame = 1; frame <= 600; ++frame) {
            double x = 428 + 15*std::sin(frame*.016*2*3.141592653589793*1.2);
            if (chased) x = chaseTarget;
            Frame(x,6);
            tapped = tapped || cursorInteraction.session.Action() == Action::Paw;
            punched = punched || cursorInteraction.session.Action() == Action::Punch;
            if (!chased && cursorInteraction.session.Action() == Action::Chase) {
                chased = true; chaseTarget = g.physics.CurrentPose().x + 105;
            }
            if (cursorInteraction.session.InFlight()) return tapped && punched && chased && launches == 1;
        }
        return false;
    }
};

int Run() {
    Host host;
    // Acquiring a cursor story cancels the brain's old follow-up exactly at
    // acquisition. Internal cursor phases then retain their own session.
    host.Reset();
    g.brain=std::make_unique<cat::behavior::Brain>(g.pet.bornUtc,13);
    g.brain->Fed(host.now,false);
    if(!g.brain->HasContinuation())return 290;
    for(unsigned frame=0;frame<100 && !EnvironmentBusy();++frame)
        host.Sample(host.PawX(),host.PawHeight());
    if(!EnvironmentBusy() || g.brain->HasContinuation())return 290;
    // A planted loaf completes before reactive play may acquire it, while
    // ordinary Idle/Watch remains immediately available to nearby pointers.
    host.Reset();SetMotion(Motion::Loaf,10000);host.RebaseMotion();
    // Poll continuously: a synthetic >250ms sample gap correctly invokes the
    // production cursor safety cooldown and would test a different condition.
    while(host.now+16<g.nextDecision) {
        host.Sample(host.PawX(),host.PawHeight());
        if(EnvironmentBusy() || g.motion!=Motion::Loaf)return 291;
    }
    host.Sample(host.PawX(),host.PawHeight(),g.nextDecision-host.now);
    for(unsigned frame=0;frame<100 && !EnvironmentBusy();++frame)
        host.Sample(host.PawX(),host.PawHeight());
    if(!EnvironmentBusy())return 291;
    // A cursor-owned approach uses the shipping motor and pose selector.
    // A neutral ground bridge must not paint a seated front view over a
    // moving chase, or retain an autonomous entrance's motor delay.
    host.Reset();
    for(unsigned seed=0;;++seed) {
        std::mt19937 preview(seed);
        if(std::uniform_real_distribution<double>(0,1)(preview)<.24){g.random.seed(seed);break;}
    }
    const double approachX=g.physics.CurrentPose().x+120;
    bool movingCursorPose=false;
    for(unsigned frame=0;frame<180;++frame) {
        host.Sample(approachX,6);AdvancePrimaryMotion(host.now,.016);
        const auto physical=g.physics.RenderPose();
        if(!EnvironmentBusy() || !cat::animation::Walking(g.motion) ||
                std::abs(WalkingVelocity(physical))<=2)continue;
        const auto pose=PrimaryDisplayedPose(host.now,physical);
        if(!pose.spriteTimed || (pose.spriteClip!=cat::sprite_sequence::Clip::Walk &&
                pose.spriteClip!=cat::sprite_sequence::Clip::Run))return 535;
        movingCursorPose=true;
    }
    if(!movingCursorPose)return 535;
    // Real legacy and new-art atlas dimensions drive the native paw geometry.
    for (const auto character : {cat::Character::Cream}) {
        host.Reset(character);
        const auto savedPet = cat::Serialize(g.pet);
        const auto savedMemory = cat::memory::Serialize(g.memory.state);
        const auto savedNutrition = cat::nutrition::Serialize(g.nutrition);
        if (CursorAttackGeometry().width <= 20 || CursorAttackGeometry().height <= 20) return 241;
        if (!host.StartPaw(host.PawHeight()) || g.lifeInteractionUntil != 0) return 242;
        bool touched = false;
        const auto expected=host.ExpectedPawContacts();
        if(!expected || (character==cat::Character::Rabbit && expected!=2))return 244;
        std::vector<bool> contactWindows(expected,false);
        for (unsigned frame = 0; frame < host.PawFrames() && cursorInteraction.session.Action() == Action::Paw; ++frame) {
            if (host.Frame(host.PawX(),host.PawHeight())) {
                touched = true;
                // A second callback in the same contact window cannot award
                // another interaction for the same physical paw strike.
                if (UpdateCursorContact(host.now,g.physics.CurrentPose())) return 243;
            }
            if(cursorInteraction.session.Action()==Action::Paw) {
                const auto index=host.PawContactIndex();
                if(index>expected)return 244;
                if(index)contactWindows[index-1]=true;
            }
        }
        if (!touched || host.contacts != expected ||
                std::find(contactWindows.begin(),contactWindows.end(),false)!=contactWindows.end() ||
                g.lifeInteractionUntil == 0) return 244;
        if (cat::Serialize(g.pet) != savedPet || cat::memory::Serialize(g.memory.state) != savedMemory ||
                cat::nutrition::Serialize(g.nutrition) != savedNutrition) return 245;
    }

    for(const double height:{6.,70.}) {
        host.Reset();
        if (!host.StartPaw(height)) return 246;
        bool sawMissWindow=false;
        for (unsigned frame = 0; frame < host.PawFrames(); ++frame) {
            host.Frame(host.PawX(),height);
            sawMissWindow=sawMissWindow || (cursorInteraction.session.Action()==Action::Paw && host.PawContactIndex()!=0);
        }
        // The provided punch's fixed paw cannot touch a floor cursor or a
        // pointer above its head; an elapsed cue alone awards nothing.
        if (!sawMissWindow || host.contacts || g.lifeInteractionUntil) return 247;
    }

    host.Reset();
    if (!host.StartPaw(host.PawHeight())) return 248;
    host.Sample(host.PawX(),host.PawHeight(),100);
    const auto beforePause = g.physics.CurrentPose();
    const auto phaseBefore = host.now-g.motionStart;
    g.menuOpen = true; host.now += 5000; g.motionStart += 5000; g.nextDecision += 5000;
    PauseCursorReaction(5000); g.physics.Update(0,true);
    if (UpdateCursorContact(host.now,g.physics.CurrentPose()) ||
            g.physics.CurrentPose().x != beforePause.x || g.physics.CurrentPose().lift != beforePause.lift ||
            host.now-g.motionStart != phaseBefore || cursorInteraction.session.Action() != Action::Paw) return 249;
    g.menuOpen = false;
    host.Frame(host.PawX(),host.PawHeight());
    if (host.contacts || cursorInteraction.session.Action() != Action::Paw) return 250;
    host.Sample(host.PawX(),host.PawHeight(),150);
    const auto contactTime = g.motionStart+host.FirstPawContactMs()+10;
    // These are production render/contact guards, tested before a valid
    // contact consumes the one-strike latch.
    for (bool* flag : {&g.pet.roamingPaused, &g.hidden, &g.menuOpen, &g.dragging, &g.closing, &updates.applying}) {
        *flag = true;
        cat::animation::Pose pose;
        if (ApplyCursorAttackPose(pose,contactTime,g.physics.CurrentPose()) ||
                UpdateCursorContact(contactTime,g.physics.CurrentPose())) return 251;
        *flag = false;
    }
    g.pendingCare = 0;
    if (UpdateCursorContact(contactTime,g.physics.CurrentPose())) return 252;
    g.pendingCare = -1; g.workInterference = false;
    if (UpdateCursorContact(contactTime,g.physics.CurrentPose())) return 252;
    host.Sample(host.PawX(),host.PawHeight());
    if (EnvironmentBusy() || TryCursorPounce(host.now) || g.lifeInteractionUntil) return 253;

    // Different and identically named replacement actions both own their
    // clocks, even when SetMotion happens within the same OS millisecond.
    for (const auto replacement : {Motion::Eat, Motion::ToyPaw}) {
        host.Reset();
        if (!host.StartPaw(host.PawHeight())) return 254;
        SetMotion(replacement,2100);
        const auto start = g.motionStart, end = g.nextDecision, serial = g.motionSerial;
        if (UpdateCursorContact(host.now+host.FirstPawContactMs()+10,g.physics.CurrentPose())) return 255;
        host.Sample(host.PawX(),host.PawHeight());
        if (EnvironmentBusy() || g.motion != replacement || g.motionStart != start ||
                g.nextDecision != end || g.motionSerial != serial || g.lifeInteractionUntil) return 256;
    }

    host.Reset();
    if (!host.ReachPounce() || !host.continuousTakeoff) return 257;
    const auto target = cursorInteraction.session.TargetX();
    const auto beforeFlightContacts = host.contacts;
    double peak = 0;
    for (int frame = 0; frame < 120; ++frame) {
        if (host.Frame(target,6) && g.physics.CurrentPose().landed &&
                UpdateCursorContact(host.now,g.physics.CurrentPose())) return 262;
        peak = std::max(peak,g.physics.CurrentPose().lift);
        if (g.physics.CurrentPose().grounded && !EnvironmentBusy()) break;
    }
    if (host.launches != 1 || host.landingContacts != 1 || host.contacts != beforeFlightContacts+1 ||
            peak < 15 || !g.physics.CurrentPose().grounded || EnvironmentBusy() ||
            std::abs(g.physics.CurrentPose().x-target) > 20) return 258;

    // Deterministically select the occasional slow approach through the real
    // production RNG and host dispatch. Each species uses its actual paw atlas.
    for (int index=0;index<10;++index) {
        host.Reset(static_cast<cat::Character>(index));
        for (unsigned seed=0;;++seed) {
            std::mt19937 preview(seed);
            if (std::uniform_real_distribution<double>(0,1)(preview)<.24) {g.random.seed(seed);break;}
        }
        const double x=g.physics.CurrentPose().x+120;
        const double contactHeight=host.PawHeight();
        double height=std::min(30.0,contactHeight);
        bool sneaked=false,pawed=false;
        for (int frame=0;frame<1800;++frame) {
            // The rare approach starts at the planner's low-cursor height.
            // Raise gently after discovery, below its120px/s departure limit,
            // so the new authored paw can actually touch the same pointer.
            if(sneaked)height=std::min(contactHeight,height+.64);
            host.Frame(x,height);
            const auto action=cursorInteraction.session.Action();
            if (action==Action::Sneak) {
                sneaked=true;
                if (g.motion!=Motion::Stalk || std::abs(CursorReactionVelocity())>14) return 263;
            }
            pawed=pawed||action==Action::Paw;
            if (action==Action::Sit || action==Action::ApproachSit) return 264;
            if (pawed&&!EnvironmentBusy())break;
        }
        if (!sneaked||!pawed||EnvironmentBusy()||host.contacts!=host.ExpectedPawContacts()||host.launches) return 265;
    }

    // OFF and a newer care action revoke contact/reward, not an impulse that
    // already happened. The controller must still finish its physical flight.
    for (bool care : {false,true}) {
        host.Reset();
        if (!host.ReachPounce()) return 259;
        const auto physical = g.physics.CurrentPose();
        const auto contacts = host.contacts;
        const auto landingTarget = cursorInteraction.session.TargetX();
        g.lifeInteractionUntil = 0;
        if (care) SetMotion(Motion::Eat,2100); else g.workInterference = false;
        const auto serial = g.motionSerial;
        for (int frame = 0; frame < 120; ++frame) {
            host.Frame(landingTarget,6);
            if (!g.physics.CurrentPose().grounded &&
                    std::abs(g.physics.CurrentPose().velocityX-physical.velocityX) > .001) return 260;
            if (g.physics.CurrentPose().grounded) break;
        }
        if (host.contacts != contacts || g.lifeInteractionUntil || !g.physics.CurrentPose().grounded ||
                (care && (g.motion != Motion::Eat || g.motionSerial != serial))) return 261;
    }
    return 0;
}
} // namespace native_cursor_tests

int NativeCursorHostTest(HINSTANCE instance) {
    g.instance = instance;
    ULONG_PTR token = 0; GdiplusStartupInput startup;
    if (GdiplusStartup(&token,&startup,nullptr) != Ok) return 239;
    const int result = LoadAtlas() ? native_cursor_tests::Run() : 240;
    ReleaseAtlases(); GdiplusShutdown(token);
    cursorInteraction = {}; g.initialized = false; g.window = nullptr;
    g.physics.SetTargetVelocity(0);
    return result;
}
