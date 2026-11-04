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
        if (!g.waitingForStop && g.jumpPending && now-g.motionStart >= 240) {
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

    double PawX() const {
        const auto geometry = CursorAttackGeometry();
        return g.physics.CurrentPose().x + (geometry.pawAnchorX-.5)*geometry.width*
            cat::GrowthScale(g.pet.bornUtc,UtcNow());
    }
    double PawHeight() const {
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
    // Real legacy and new-art atlas dimensions drive the native paw geometry.
    for (const auto character : {cat::Character::Cream, cat::Character::Rabbit}) {
        host.Reset(character);
        const auto savedPet = cat::Serialize(g.pet);
        const auto savedMemory = cat::memory::Serialize(g.memory.state);
        const auto savedNutrition = cat::nutrition::Serialize(g.nutrition);
        if (CursorAttackGeometry().width <= 20 || CursorAttackGeometry().height <= 20) return 241;
        if (!host.StartPaw(host.PawHeight()) || g.lifeInteractionUntil != 0) return 242;
        bool touched = false;
        for (int frame = 0; frame < 48 && cursorInteraction.session.Action() == Action::Paw; ++frame) {
            if (host.Frame(host.PawX(),host.PawHeight())) {
                touched = true;
                // A second callback in the same contact window cannot award
                // another interaction for the same physical paw strike.
                if (UpdateCursorContact(host.now,g.physics.CurrentPose())) return 243;
            }
        }
        if (!touched || host.contacts != 2 || g.lifeInteractionUntil == 0) return 244;
        if (cat::Serialize(g.pet) != savedPet || cat::memory::Serialize(g.memory.state) != savedMemory ||
                cat::nutrition::Serialize(g.nutrition) != savedNutrition) return 245;
    }

    host.Reset();
    if (!host.StartPaw(70)) return 246;
    for (int frame = 0; frame < 48; ++frame) host.Frame(host.PawX(),70);
    if (host.contacts || g.lifeInteractionUntil) return 247; // Selected attack is a real miss.

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
    const auto contactTime = g.motionStart+270;
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
        if (UpdateCursorContact(host.now+270,g.physics.CurrentPose())) return 255;
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
