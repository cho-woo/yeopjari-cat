#pragma once

// Included after the production host functions, only for --self-test. These
// fixtures use the shipping sample, motor, pose and contact functions without
// OS cursor movement, a visible window, AppData loading or enabled storage.
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
        // Nonzero monitor origin and DPI exercise the production screen map.
        g.work = {120, -180, 2040, 1170}; g.dpiScale = 1.5;
        g.centerX = g.work.left + 400*g.dpiScale; g.direction = g.facing = 1;
        g.cursorPlay = true; g.workInterference = false; g.lifeInteractionUntil = 0;
        g.random.seed(33);
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
        return {static_cast<LONG>(std::lround(g.work.left+x*g.dpiScale)),
            static_cast<LONG>(std::lround(g.work.bottom-height*g.dpiScale))};
    }
    void Sample(double x, double height, ULONGLONG elapsed = 16, bool available = true) {
        now += elapsed; pointer = Screen(x,height);
        const auto serial = g.motionSerial;
        UpdateCursorReactionSample(now, elapsed, pointer, available);
        // Keep a newly issued action aligned with this sampled host clock;
        // an externally replacing action must retain its own clock.
        if (g.motionSerial != serial) {
            RebaseMotion();
            cursorInteraction.session.AdoptMotion(g.motionStart,g.waitingForStop,g.motionSerial);
        }
    }
    bool Frame(double x, double height, bool available = true,ULONGLONG step=16) {
        Sample(x,height,step,available);
        const auto beforeRendered=g.physics.RenderPose();
        const bool launchedBefore = g.jumpLaunched;
        const auto creditedBefore = cursorInteraction.awardedContacts;
        AdvancePrimaryMotion(now,step*.001);
        const auto physical = g.physics.CurrentPose();
        const auto rendered=g.physics.RenderPose();
        if (!launchedBefore && g.jumpLaunched) {
            ++launches;
            continuousTakeoff = continuousTakeoff &&
                std::abs(rendered.x-beforeRendered.x) <= std::abs(physical.velocityX)*step*.001+1 &&
                std::abs(rendered.lift-beforeRendered.lift) <= std::abs(physical.velocityY)*step*.001+2;
        }
        const auto credited = cursorInteraction.awardedContacts-creditedBefore;
        contacts += credited;
        if (physical.landed) landingContacts += credited;
        return credited != 0;
    }
    ULONGLONG PawDuration() const {
        const auto* source=LoadedCareSprite(cat::sprite_sequence::Clip::Punch,g.preferences.character);
        return source?cat::sprite_sequence::ProgramDuration(source->layout->program):850;
    }
    ULONGLONG FirstPawContactMs() const { return g.preferences.character==cat::Character::Tuxedo?667:500; }
    cat::deform::Point PawTarget() const {
        const auto points=cat::video_actions::PunchPaws(static_cast<unsigned>(g.preferences.character),
            g.preferences.character==cat::Character::Tuxedo?18:14);
        const auto body=cat::appearance::VisualModifiers(g.nutrition,g.preferences.character);
        return {g.direction*points[0].x*body.bodyWidth,points[0].y*body.bodyHeight};
    }
    double PawX() const {
        return g.physics.CurrentPose().x+PawTarget().x*cat::GrowthScale(g.pet.bornUtc,UtcNow());
    }
    double PawHeight() const {
        return PawTarget().y*cat::GrowthScale(g.pet.bornUtc,UtcNow());
    }
    bool StartPaw(double height) {
        const double target = PawX();
        for (int frame = 0; frame < 120; ++frame) {
            Frame(target,height);
            if (cursorInteraction.session.Action() == Action::Paw)
                return g.motion == Motion::ToyPaw && !g.waitingForStop && contacts == 0;
        }
        return false;
    }
    bool ReachPounce() {
        const double target = g.physics.CurrentPose().x+20;
        for (int frame = 0; frame < 360; ++frame) {
            Frame(target,205);
            if (cursorInteraction.session.InFlight()) return launches == 1;
        }
        return false;
    }
};

int MovingPursuit() {
    Host host;std::ostringstream evidence;
    evidence<<"{\"schema\":1,\"test\":\"production-pointer-pursuit\",\"runs\":[";
    bool comma=false;
    for(const auto character:cat::kAvailableCharacters)for(const ULONGLONG step:{8ULL,16ULL,33ULL}) {
        host.Reset(character);
        if(!EnsureCareCoats(character))return 262;
        const auto saved=cat::Serialize(g.pet)+cat::memory::Serialize(g.memory.state)+cat::nutrition::Serialize(g.nutrition);
        unsigned chase=0,turn=0,movingLaunch=0,flight=0,swat=0;
        bool finite=true,ballistic=true,connected=true;
        double lastX=g.physics.CurrentPose().x;
        for(ULONGLONG elapsed=0;elapsed<42000;elapsed+=step) {
            const double seconds=elapsed*.001;
            const double x=elapsed<20000?800+300*std::sin(seconds*.55):
                (static_cast<unsigned>((elapsed-20000)/5500)%2?100:1100);
            const double height=elapsed<20000?110+35*std::sin(seconds*.9):8;
            const auto before=g.physics.CurrentPose();
            const bool launched=g.jumpLaunched;
            host.Frame(x,height,true,step);
            const auto physical=g.physics.CurrentPose();
            const auto displayed=PrimaryDisplayedPose(host.now,g.physics.RenderPose());
            chase+=cursorInteraction.session.Action()==Action::Chase;
            turn+=displayed.spriteClip==cat::sprite_sequence::Clip::RunningTurn;
            swat+=displayed.spriteClip==cat::sprite_sequence::Clip::Punch;
            flight+=!physical.grounded;
            if(!launched&&g.jumpLaunched&&std::abs(physical.velocityX)>20) {
                ++movingLaunch;connected=connected&&!g.waitingForStop&&cursorInteraction.runningTakeoff;
            }
            if(!before.grounded&&!physical.grounded)
                ballistic=ballistic&&std::abs(physical.velocityX-before.velocityX)<.01;
            const auto bounds=MovementBoundsLogical();
            finite=finite&&std::isfinite(physical.x)&&std::isfinite(physical.lift)&&
                physical.x>=bounds.first-.1&&physical.x<=bounds.second+.1&&physical.lift>=0&&
                std::abs(physical.x-lastX)<=450*step*.001+2;
            lastX=physical.x;
        }
        if(comma)evidence<<',';comma=true;
        evidence<<"{\"character\":"<<static_cast<unsigned>(character)<<",\"step_ms\":"<<step<<",\"chase_frames\":"<<chase
            <<",\"turn_frames\":"<<turn<<",\"moving_launches\":"<<movingLaunch<<",\"airborne_frames\":"<<flight
            <<",\"swat_frames\":"<<swat<<",\"finite\":"<<finite<<",\"ballistic\":"<<ballistic<<",\"connected\":"<<connected
            <<",\"continuous_takeoff\":"<<host.continuousTakeoff<<",\"profile_unchanged\":"<<
                (saved==cat::Serialize(g.pet)+cat::memory::Serialize(g.memory.state)+cat::nutrition::Serialize(g.nutrition))<<'}';
        if(chase<100||!turn||!movingLaunch||!flight||!finite||!ballistic||!connected||!host.continuousTakeoff||
                saved!=cat::Serialize(g.pet)+cat::memory::Serialize(g.memory.state)+cat::nutrition::Serialize(g.nutrition)) {
            CreateDirectoryW(L"render-self-test",nullptr);
            WriteBytes(L"render-self-test/pointer-pursuit-report.json",evidence.str()+"] ,\"passed\":false}\n");return 263;
        }
    }
    // A predeclared straight pointer path, independent of the rendered toe,
    // must be catchable by each coat and by an ordinary/heavier adult.
    for(const auto character:cat::kAvailableCharacters)for(const double condition:{50.,80.}) {
        host.Reset(character);if(!EnsureCareCoats(character))return 262;
        g.nutrition.bodyCondition=condition;
        unsigned airContacts=0;
        for(ULONGLONG elapsed=0;elapsed<8000;elapsed+=16) {
            const auto before=host.contacts;
            host.Frame(560+72*elapsed*.001,120);
            if(!g.physics.CurrentPose().grounded)airContacts+=host.contacts-before;
        }
        evidence<<",{\"character\":"<<static_cast<unsigned>(character)<<",\"condition\":"<<condition
            <<",\"linear_pointer_speed\":72,\"linear_pointer_height\":120,\"air_contacts\":"<<airContacts<<'}';
        if(!airContacts) {
            CreateDirectoryW(L"render-self-test",nullptr);
            WriteBytes(L"render-self-test/pointer-pursuit-report.json",evidence.str()+"],\"passed\":false}\n");return 264;
        }
    }
    CreateDirectoryW(L"render-self-test",nullptr);
    WriteBytes(L"render-self-test/pointer-pursuit-report.json",evidence.str()+"],\"passed\":true}\n");
    return 0;
}

int Run() {
    Host host;
    // Acquiring a nearby pointer interrupts the old brain follow-up. Cursor
    // play remains enabled while the separate walking interference setting is off.
    host.Reset();
    g.brain = std::make_unique<cat::behavior::Brain>(g.pet.bornUtc,13);
    g.brain->Fed(host.now,false);
    if (!g.brain->HasContinuation()) return 290;
    for (unsigned frame = 0; frame < 100 && !EnvironmentBusy(); ++frame)
        host.Frame(host.PawX(),host.PawHeight());
    if (!EnvironmentBusy() || g.brain->HasContinuation() || g.workInterference) return 290;

    // A planted loaf owns its full duration; ordinary Idle/Watch can acquire.
    host.Reset(); SetMotion(Motion::Loaf,1800); host.RebaseMotion();
    while (host.now+16 < g.nextDecision) {
        host.Frame(host.PawX(),host.PawHeight());
        if (EnvironmentBusy() || g.motion != Motion::Loaf) return 291;
    }
    host.Sample(host.PawX(),host.PawHeight(),g.nextDecision-host.now);
    for (unsigned frame = 0; frame < 100 && !EnvironmentBusy(); ++frame)
        host.Frame(host.PawX(),host.PawHeight());
    if (!EnvironmentBusy()) return 291;

    // A fixed pointer keeps the native motor/session playing, with a visible
    // Rest between attempts. Repeated callbacks in one paw cue cannot award twice.
    host.Reset();
    const auto savedPet = cat::Serialize(g.pet);
    const auto savedMemory = cat::memory::Serialize(g.memory.state);
    const auto savedNutrition = cat::nutrition::Serialize(g.nutrition);
    const double fixedX = host.PawX(), fixedHeight = 40;
    Action previous = Action::None;
    unsigned attempts = 0, paws = 0, rests = 0;
    ULONGLONG restAt = 0;
    bool strong = false;
    for (unsigned frame = 0; frame < 4000; ++frame) {
        const bool credited = host.Frame(fixedX,fixedHeight);
        const auto action = cursorInteraction.session.Action();
        if (credited && UpdateCursorContact(host.now,g.physics.CurrentPose())) return 243;
        if (action != previous) {
            if (action == Action::Rest) { ++rests; restAt = host.now; }
            if (action == Action::Paw || action == Action::Punch) {
                ++attempts; paws += action == Action::Paw;
                strong = strong || action == Action::Punch;
                if (restAt && host.now-restAt < 280) return 244;
            }
            previous = action;
        }
        if (std::abs(g.physics.CurrentPose().x-400) > 80 || host.launches) return 245;
    }
    if (attempts < 4 || paws < 2 || rests < 3 || !strong || !EnvironmentBusy()) return 244;
    if (cat::Serialize(g.pet) != savedPet || cat::memory::Serialize(g.memory.state) != savedMemory ||
            cat::nutrition::Serialize(g.nutrition) != savedNutrition) return 245;

    // Source-video paws and both extension windows. A live pointer following
    // the actual toe can contact once per stroke while roaming is paused.
    host.Reset(); g.pet.roamingPaused = true;
    const double pawHeight = host.PawHeight();
    if (!host.StartPaw(pawHeight)) return 246;
    while (cursorInteraction.session.Action() == Action::Paw) {
        cat::animation::Pose visible;
        ApplyCursorAttackPose(visible,host.now+16,g.physics.CurrentPose());
        const auto toe=cat::cursor::PawPositions(visible,CursorAttackGeometry())[0];
        const auto body=cat::appearance::VisualModifiers(g.nutrition,g.preferences.character);
        if (host.Frame(g.physics.CurrentPose().x+toe.x*body.bodyWidth,toe.y*body.bodyHeight) &&
                UpdateCursorContact(host.now,g.physics.CurrentPose())) return 243;
    }
    if (host.contacts != 2 || !g.lifeInteractionUntil) return 247;
    // An elapsed cue with a distant pointer cannot manufacture a contact.
    host.Reset(); if (!host.StartPaw(host.PawHeight())) return 246;
    cursorInteraction.cursorX += 200;
    if (UpdateCursorContact(g.motionStart+host.FirstPawContactMs()+10,g.physics.CurrentPose()) ||
            g.lifeInteractionUntil) return 247;

    // Pausing rebases both clocks. Playback and contact guards must agree at a
    // valid gentle contact time before the strike latch has been consumed.
    host.Reset(); if (!host.StartPaw(host.PawHeight())) return 248;
    host.Sample(host.PawX(),host.PawHeight(),100);
    const auto beforePause = g.physics.CurrentPose();
    const auto phaseBefore = host.now-g.motionStart;
    g.menuOpen = true; host.now += 5000; g.motionStart += 5000; g.nextDecision += 5000;
    PauseCursorReaction(5000); g.physics.Update(0,true);
    if (UpdateCursorContact(host.now,g.physics.CurrentPose()) ||
            g.physics.CurrentPose().x != beforePause.x || g.physics.CurrentPose().lift != beforePause.lift ||
            host.now-g.motionStart != phaseBefore || cursorInteraction.session.Action() != Action::Paw) return 249;
    g.menuOpen = false;
    const auto contactTime = g.motionStart+host.FirstPawContactMs()+10;
    for (bool* flag : {&g.hidden,&g.menuOpen,&g.dragging,&g.closing,&updates.applying,&g.manualMotionActive}) {
        *flag = true;
        cat::animation::Pose pose;
        if (CursorPlaybackAllowed() || ApplyCursorAttackPose(pose,contactTime,g.physics.CurrentPose()) ||
                UpdateCursorContact(contactTime,g.physics.CurrentPose())) return 251;
        *flag = false;
    }
    g.pendingCare = 0;
    if (UpdateCursorContact(contactTime,g.physics.CurrentPose())) return 252;
    g.pendingCare = -1; g.cursorPlay = false;
    if (UpdateCursorContact(contactTime,g.physics.CurrentPose())) return 252;
    host.Frame(host.PawX(),host.PawHeight());
    if (EnvironmentBusy() || TryCursorPounce(host.now) || g.lifeInteractionUntil) return 253;

    // A newer action, including the same named action, owns its serial and clock.
    for (const auto replacement : {Motion::Eat,Motion::ToyPaw,Motion::Sleep}) {
        host.Reset(); if (!host.StartPaw(host.PawHeight())) return 254;
        SetMotion(replacement,2100);
        const auto start = g.motionStart, end = g.nextDecision, serial = g.motionSerial;
        if (UpdateCursorContact(host.now+host.FirstPawContactMs()+10,g.physics.CurrentPose())) return 255;
        host.Sample(host.PawX(),host.PawHeight());
        if (EnvironmentBusy() || g.motion != replacement || g.motionStart != start ||
                g.nextDecision != end || g.motionSerial != serial || g.lifeInteractionUntil) return 256;
    }

    // High stationary pointers use real vertical hops while roaming is paused. No repeat can begin before landing and its recovery/rest.
    host.Reset(); g.pet.roamingPaused = true;
    const double highX = g.physics.CurrentPose().x+20;
    unsigned lastLaunches = 0, physicalLandings = 0;
    ULONGLONG lastLanding = 0;
    double peak = 0;
    for (unsigned frame = 0; frame < 1600; ++frame) {
        host.Frame(highX,205);
        const auto physical = g.physics.CurrentPose();
        peak = std::max(peak,physical.lift);
        if (physical.landed) { ++physicalLandings; lastLanding = host.now; }
        if (host.launches != lastLaunches) {
            if (lastLaunches && (!lastLanding || host.now-lastLanding < 2140)) return 257;
            lastLaunches = host.launches;
        }
        if (std::abs(physical.x-400) > .02) return 258;
    }
    if (host.launches < 3 || physicalLandings < 2 || peak < 90 || !host.continuousTakeoff || host.landingContacts) return 258;

    // Departure, OFF and care revoke contact, while Box2D finishes the impulse.
    for (int reason = 0; reason < 5; ++reason) {
        host.Reset(); g.pet.roamingPaused = true; if (!host.ReachPounce()) return 259;
        const auto physical = g.physics.CurrentPose();
        const auto contacts = host.contacts;
        double x = highX, height = 205;
        bool available = true;
        g.lifeInteractionUntil = 0;
        if (reason == 0) g.cursorPlay = false;
        if (reason == 1) available = false;
        if (reason == 2) x += 500;
        if (reason == 3) height += 500;
        if (reason == 4) SetMotion(Motion::Eat,2100);
        const auto serial = g.motionSerial;
        for (unsigned frame = 0; frame < 240; ++frame) {
            host.Frame(x,height,available);
            if (!g.physics.CurrentPose().grounded &&
                    std::abs(g.physics.CurrentPose().velocityX-physical.velocityX) > .001) return 260;
            if (cursorInteraction.session.ContactAllowed()) return 260;
            if (g.physics.CurrentPose().grounded) break;
        }
        if (host.contacts != contacts || g.lifeInteractionUntil || !g.physics.CurrentPose().grounded ||
                (reason == 4 && (g.motion != Motion::Eat || g.motionSerial != serial))) return 261;
    }
    return MovingPursuit();
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
