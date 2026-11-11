#pragma once

// --self-test only. Calls the real Win32 menu/dispatcher and the same motor,
// stop gate, Box2D takeoff/landing and gait host as Tick. No application files,
// windows, OS cursor movement or update workers are opened by these fixtures.
namespace native_manual_motion_tests {
struct Host {
    ULONGLONG now = 0;
    bool artworkReady=false;
    void Reset(cat::Character character = cat::Character::Cream, bool paused = false) {
        artworkReady=EnsureCareCoats(character);
        g.window = nullptr; g.initialized = true;
        g.hidden = g.menuOpen = g.dragging = g.closing = false;
        updates.applying = false;
        g.memory.outcome = cat::memory::storage::Outcome::Blocked;
        g.life.outcome = cat::life::storage::Outcome::Blocked;
        g.companionStore.state.enabled = false;
        g.brain.reset(); g.lifeSequenceCount = g.memorySequenceCount = 0;
        g.randomZoomies.Reset();g.randomZoomiesSerial=0;g.randomZoomiesMsRemainder=0;
        g.lifeCrossing = {}; g.smallBoxEvent = g.lifeDispatching = false;
        g.toy = ToyKind::None; g.toyEngaged = g.toyDispatching = g.approachingToy = false;
        g.toySession.Cancel(); g.automaticToy = {};
        g.toyRewardPending = false; g.approachingUser = false; g.pendingCare = -1;
        g.preferences = {character, cat::Outfit::None};
        const auto utc = UtcNow();
        g.pet = cat::NewPet(utc - 366 * cat::life::Day); g.pet.updatedUtc = utc;
        g.pet.roamingPaused = paused;
        g.nutrition = cat::nutrition::NewState(g.pet.bornUtc, utc, 80);
        g.nutrition.bodyCondition = 50;
        g.work = {120, -180, 2040, 1170}; g.dpiScale = 1.5;
        g.centerX = g.work.left + 400*g.dpiScale; g.direction = g.facing = 1;
        g.workInterference = true; g.cursorPlay=false;g.lifeInteractionUntil = 0;
        cursorInteraction = {}; ResetMovement();
        for (int i = 0; i < 12; ++i) g.physics.Update(1./60);
        now = GetTickCount64(); SetMotion(Motion::Idle, 8000); Rebase();
    }
    void Rebase() {
        const auto duration = g.nextDecision - g.motionStart;
        g.motionStart = now; g.nextDecision = now + duration;
        g.groundArtBridge.RebaseClock(now);
    }
    void Command(UINT id) {
        g.groundArtBridge.RebaseClock(now);
        RunCommand(id,now); Rebase();
    }
    void Frame(ULONGLONG stepMs=16) {
        now += stepMs;
        const double elapsed=stepMs/1000.;
        AdvanceAutomaticToy(elapsed);
        const auto position = g.physics.CurrentPose();
        POINT pointer{static_cast<LONG>(g.work.left+(position.x+35)*g.dpiScale),
            static_cast<LONG>(g.work.bottom-65*g.dpiScale)};
        UpdateCursorReactionSample(now,stepMs,pointer,true);
        auto serial = g.motionSerial;
        FinishManualMotion(now);
        if (g.motionSerial != serial) Rebase();
        AdvanceRandomZoomies(now,elapsed);
        serial = g.motionSerial; DispatchAutonomousMotion(now);
        if (g.motionSerial != serial) Rebase();
        serial = g.motionSerial; AdvancePrimaryMotion(now,elapsed);
        if (g.motionSerial != serial) Rebase();
    }
    std::string Saved() const {
        return cat::Serialize(g.pet) + cat::SerializePreferences(g.preferences) +
            cat::nutrition::Serialize(g.nutrition) + cat::memory::Serialize(g.memory.state) +
            cat::life::Serialize(g.life.state) + cat::companion::Serialize(g.companionStore.state);
    }
};

#include "native_jump_continuity_tests.hpp"
#include "native_random_zoomies_tests.hpp"
#include "native_keyboard_play_tests.hpp"

bool MenuCommands(bool enabled) {
    HMENU menu = BuildPetMenu(); if (!menu) return false;
    HMENU actions = nullptr;
    for (int i = 0; i < GetMenuItemCount(menu); ++i) {
        wchar_t label[80]{}; GetMenuStringW(menu,i,label,80,MF_BYPOSITION);
        if (std::wstring(label) == L"행동시키기") actions = GetSubMenu(menu,i);
    }
    bool valid = actions && GetMenuItemCount(actions) == 4;
    const UINT ids[]{kManualWalkCommand,kManualRunCommand,kManualJumpCommand,kWallClimbCommand};
    const wchar_t* labels[]{L"걷기",L"달리기",L"점프하기",L"오른쪽 가장자리 오르기"};
    for (unsigned i = 0; valid && i < 4; ++i) {
        wchar_t label[80]{}; GetMenuStringW(actions,i,label,80,MF_BYPOSITION);
        const auto state = GetMenuState(actions,ids[i],MF_BYCOMMAND);
        valid = GetMenuItemID(actions,i) == ids[i] && std::wstring(label) == labels[i] &&
            state != static_cast<UINT>(-1) && (((state & (MF_GRAYED|MF_DISABLED)) == 0) == enabled);
    }
    DestroyMenu(menu); return valid;
}

int Run(std::string& proof) {
    if(const int keyboardResult=keyboard_native::Run())return keyboardResult;
    if(!EnsureCareCoats(cat::Character::Cream,cat::Character::Tuxedo))return 520;
    if(const int randomZoomies=random_zoomies_native::Run())return randomZoomies;
    if(const int jumpContinuity=jump_continuity::Run())return jumpContinuity;
    Host host;
    const auto* walk = LoadedCareSprite(cat::sprite_sequence::Clip::Walk,cat::Character::Cream);
    if (!walk || !cat::sprite_assets::Valid(*walk->layout) || walk->layout->frameCount < 8 ||
            LoadedCareSprite(cat::sprite_sequence::Clip::Walk,cat::Character::Tuxedo) != walk ||
            g.meshWalkReview) return 520;
    const auto approvedWalkFrames = walk->layout->frameCount;
    const double nativeWalkSpeed=cat::sprite_assets::NominalLocomotionSpeed(*walk->layout);
    const double expectedWalkSpeed=nativeWalkSpeed>0?nativeWalkSpeed:22;
    const auto* run=LoadedCareSprite(cat::sprite_sequence::Clip::Run,cat::Character::Cream);
    const double nativeRunSpeed=run?cat::sprite_assets::NominalLocomotionSpeed(*run->layout):0;
    const double expectedRunSpeed=nativeRunSpeed>0?nativeRunSpeed:
        nativeWalkSpeed>0?std::max(90.0,nativeWalkSpeed*1.65):90;
    std::ostringstream characters;
    bool first = true;
    for (const auto character : cat::kAvailableCharacters) {
        if(!EnsureCareCoats(character))return 520;
        double distances[2]{}, peaks[2]{};
        for (unsigned action = 0; action < 2; ++action) {
            host.Reset(character,true);
            if (!MenuCommands(true)) return 521;
            const auto saved = host.Saved();
            const auto start = g.physics.CurrentPose().x;
            host.Command(action ? kManualRunCommand : kManualWalkCommand);
            if (!g.manualMotionActive || !g.pet.roamingPaused ||
                    g.motion != (action ? Motion::Zoomies : Motion::Walk)) return 522;
            unsigned frames = 0;
            while (g.manualMotionActive && frames++ < 500) {
                if (CanStartAutonomousStory(host.now) || PrimarySocialBoundary(host.now) ||
                        ToyInteractionAvailable() || MaybePlayWithToy(host.now)) return 523;
                host.Frame();
                peaks[action] = std::max(peaks[action],std::abs(WalkingVelocity(g.physics.CurrentPose())));
                if (EnvironmentBusy()) return 523;
            }
            distances[action] = g.physics.CurrentPose().x - start;
            if (g.manualMotionActive || g.motion != Motion::Idle || !g.autonomousResting ||
                    host.Saved() != saved || !g.pet.roamingPaused ||
                    distances[action] < (action ? 180 : 80) ||
                    peaks[action]<(action?expectedRunSpeed:expectedWalkSpeed)*1.03 ||
                    peaks[action]>(action?expectedRunSpeed:expectedWalkSpeed)*1.28) return 524;
            g.workInterference = false;
            // A source-backed skid or tumble owns up to2600ms of recovery.
            for (unsigned frame = 0; frame < 230; ++frame) host.Frame();
            if (std::abs(WalkingVelocity(g.physics.CurrentPose())) > .5 || host.Saved() != saved) return 525;
        }
        // A moving request preserves the actual velocity from this same
        // production host; there is no stop gate or air steering motor.
        host.Reset(character,true); const auto saved = host.Saved();
        host.Command(kManualRunCommand);
        for (unsigned frame = 0; frame < 120; ++frame) host.Frame();
        const auto requested=g.physics.CurrentPose();
        host.Command(kManualJumpCommand);
        if (g.waitingForStop || !g.runningJump.active || !g.jumpPending || !g.manualMotionActive ||
                g.physics.CurrentPose().x!=requested.x || std::abs(requested.velocityX)<8) return 526;
        bool launched = false, landed = false, continued=false;
        double lift = 0,flightStart=0,airDistance=0,maxDrift=0,launchSpeed=0;
        for (unsigned frame = 0; frame < 600 && (g.manualMotionActive || !landed); ++frame) {
            host.Frame(); const auto after = g.physics.CurrentPose();
            if (!launched && !after.grounded) {
                if (g.waitingForStop || host.now-g.motionStart < cat::running_jump::Plan::PreparationMs ||
                        std::abs(after.velocityX-requested.velocityX)>1) return 527;
                launched = g.jumpLaunched;flightStart=after.x;launchSpeed=after.velocityX;
            }
            if(launched&&!after.grounded)maxDrift=std::max(maxDrift,std::abs(after.velocityX-launchSpeed));
            if(launched&&after.landed) {
                landed=true;airDistance=after.x-flightStart;
                continued=cat::animation::Walking(g.motion)&&!g.waitingForStop&&!g.runningJump.active&&
                    !g.groundArtBridge.Stopping()&&std::abs(after.velocityX-launchSpeed)<1;
            }
            lift = std::max(lift,after.lift);
            if (!std::isfinite(after.x) || !std::isfinite(after.lift)) return 528;
        }
        if (!launched || !landed || !continued || maxDrift>1 || airDistance<launchSpeed*.55 || lift < 30 ||
                g.manualMotionActive || !g.physics.CurrentPose().grounded || g.motion != Motion::Idle || host.Saved() != saved) return 529;
        if (!first) characters << ','; first = false;
        characters << "{\"character\":" << static_cast<int>(character)
            << ",\"walk_distance\":" << distances[0] << ",\"run_distance\":" << distances[1]
            << ",\"walk_peak_speed\":" << peaks[0] << ",\"run_peak_speed\":" << peaks[1]
            << ",\"jump_max_lift\":" << lift << ",\"jump_landed\":true,\"moving_jump_momentum_preserved\":true"
            << ",\"moving_jump_air_distance\":" << airDistance << ",\"moving_jump_launch_speed\":" << launchSpeed
            << ",\"moving_jump_max_air_speed_drift\":" << maxDrift << ",\"moving_jump_gait_continued\":true}";
    }
    // Explicit movement interrupts safe floor stories, including a long held
    // sleep/box pose, without awarding care or modifying any serialized record.
    for (const auto prior : {Motion::Sleep,Motion::BoxSit,Motion::Walk}) {
        host.Reset(); SetMotion(prior,30000);
        g.toy = ToyKind::Box; g.toySession.Start(g.toy); g.toyEngaged = true;
        g.approachingToy = g.approachingUser = true;
        g.lifeSequenceCount = g.memorySequenceCount = 2; g.smallBoxEvent = true;
        const auto saved = host.Saved(); const auto before = g.physics.CurrentPose();
        host.Command(kManualWalkCommand);
        if (!g.manualMotionActive || g.toyEngaged || g.approachingToy || g.approachingUser ||
                g.lifeSequenceCount || g.memorySequenceCount || g.smallBoxEvent ||
                host.Saved() != saved || g.physics.CurrentPose().x != before.x ||
                g.physics.CurrentPose().lift != before.lift) return 530;
    }
    for (const UINT command : {kManualWalkCommand,kManualRunCommand}) {
        host.Reset(); const auto saved = host.Saved(); host.Command(command);
        for (unsigned frame = 0; frame < 500 && g.manualMotionActive; ++frame) host.Frame();
        if (g.manualMotionActive || g.motion != Motion::Idle || !g.autonomousResting ||
                g.pet.roamingPaused || host.Saved() != saved) return 530;
    }
    // Replacing a cursor-owned action, even the same named motion, must revoke
    // cursor velocity/contact before the next physical frame.
    native_cursor_tests::Host cursor;
    cursor.Reset(); if (!cursor.StartPaw(cursor.PawHeight())) return 531;
    g.initialized = true; host.now = GetTickCount64(); host.Command(kManualRunCommand);
    if (EnvironmentBusy() || !g.manualMotionActive) return 531;
    host.Frame(); if (EnvironmentBusy() || g.motion != Motion::Zoomies) return 531;

    // A menu/hidden interval uses the production freeze path, preserving the
    // remaining manual time as well as the stationary body's position.
    for (const bool hidden : {false,true}) {
        host.Reset(cat::Character::Cream,true); host.Command(kManualJumpCommand);
        for (unsigned frame = 0; frame < 5; ++frame) host.Frame();
        const auto remaining = g.nextDecision-host.now, age = host.now-g.motionStart;
        const auto before = g.physics.CurrentPose();
        g.hidden = hidden; g.menuOpen = !hidden; host.now += 60000;
        FreezePrimaryMotion(host.now,60000); FinishManualMotion(host.now);
        if (!g.manualMotionActive || g.nextDecision-host.now != remaining ||
                host.now-g.motionStart != age || g.physics.CurrentPose().x != before.x ||
                g.physics.CurrentPose().lift != before.lift || !g.jumpPending) return 532;
        g.hidden = g.menuOpen = false;
        bool airborne = false;
        for (unsigned frame = 0; frame < 500 && g.manualMotionActive; ++frame) {
            host.Frame(); airborne = airborne || g.physics.CurrentPose().lift > 5;
        }
        if (!airborne || g.manualMotionActive || !g.physics.CurrentPose().grounded || !g.pet.roamingPaused) return 532;
    }
    unsigned unsafe = 0;
    for (unsigned state = 0; state < 15; ++state) {
        host.Reset();
        switch (state) {
        case 0: g.physicsReady = false; break;
        case 1: g.hidden = true; break;
        case 2: g.menuOpen = true; break;
        case 3: g.dragging = true; break;
        case 4: g.closing = true; break;
        case 5: updates.applying = true; break;
        case 6: g.physics.Jump(55); g.physics.Update(.016); break;
        case 7: g.perch.active = true; break;
        case 8: g.perch.transitioning = true; break;
        case 9: g.perch.preparing = true; break;
        case 10: g.pendingCare = 0; break;
        case 11: g.lifeCrossing.Start(); break;
        case 12: g.initialized = false; break;
        case 13: case 14:
            if (!g.physics.LaunchToPlatform(350,700,120,400,45)) return 533;
            for (unsigned frame = 0; frame < 500 && !g.physics.OnPlatform(); ++frame) g.physics.Update(.016);
            if (!g.physics.OnPlatform()) return 533;
            if (state == 14 && !g.physics.BeginHang(350,120)) return 533;
            break;
        }
        if (ManualMotionAvailable() || !MenuCommands(false)) return 533;
        const auto saved = host.Saved(); const auto serial = g.motionSerial;
        const auto before = g.physics.CurrentPose();
        for (const UINT id : {kManualWalkCommand,kManualRunCommand,kManualJumpCommand,kWallClimbCommand}) RunCommand(id);
        const auto after = g.physics.CurrentPose();
        if (g.motionSerial != serial || g.manualMotionActive || host.Saved() != saved ||
                before.x != after.x || before.lift != after.lift ||
                before.velocityX != after.velocityX || before.velocityY != after.velocityY) return 534;
        ++unsafe;
    }
    if(!EnsureCareCoats(cat::Character::Cream,cat::Character::Tuxedo))return 520;
    proof = "{\"schema\":1,\"test\":\"manual-motion-native\",\"status\":\"passed\",\"version\":\""
        YEOPJARI_VERSION_STRING "\",\"mesh_walk_default\":false,\"approved_walk_frames\":" +
        std::to_string(approvedWalkFrames) + ",\"menu_commands\":[2800,2801,2802,2803],\"menu_labels_checked\":true,\"unsafe_states_checked\":" +
        std::to_string(unsafe) + ",\"pause_preference_preserved\":true,\"suspension_clock_preserved\":true,"
        "\"characters\":[" + characters.str() + "]}\n";
    return 0;
}
} // namespace native_manual_motion_tests

int NativeManualMotionTest(HINSTANCE instance) {
    g.instance = instance;
    ULONG_PTR token = 0; GdiplusStartupInput startup;
    if (GdiplusStartup(&token,&startup,nullptr) != Ok) return 519;
    std::string proof;
    const int result = LoadAtlas() ? native_manual_motion_tests::Run(proof) : 520;
    if (result) proof = "{\"schema\":1,\"test\":\"manual-motion-native\",\"status\":\"failed\",\"exit_code\":" + std::to_string(result) + "}\n";
    const bool written = WriteBytes(L"manual-motion-self-test.json",proof);
    ReleaseAtlases(); GdiplusShutdown(token);
    cursorInteraction = {}; g.manualMotionActive = false; g.initialized = false; g.window = nullptr;
    g.physics.SetTargetVelocity(0);
    return result ? result : written ? 0 : 535;
}
