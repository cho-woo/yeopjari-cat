#pragma once

// Included inside native_manual_motion_tests, after Host. Fake key edges run
// through the shipping keyboard owner, drive/stop gate, Box2D and Render.
// No OS keys are injected, no cursor moves and no user profile is loaded.
namespace keyboard_native {
void Clear() {
    StopKeyboardPlay(false,false);keyboardPlay.fixture=false;keyboardPlay.fixtureForeground=nullptr;
}
void Step(Host& host,unsigned count=1) {
    for(unsigned frame=0;frame<count;++frame) {
        host.now+=16;UpdateKeyboardPlay(host.now,.016);
        FinishManualMotion(host.now);DispatchAutonomousMotion(host.now);
        AdvancePrimaryMotion(host.now,.016);
    }
}
bool Start(Host& host,cat::Character character) {
    Clear();if(!EnsureCareCoats(character))return false;
    host.Reset(character,true);keyboardPlay.fixture=true;
    // Keep this speed fixture away from real edge braking even for the
    // registered fast run cadence. Edge ownership has separate safety tests.
    g.work.right=g.work.left+3300;ResetMovement();
    for(unsigned frame=0;frame<12;++frame)g.physics.Update(1./60);
    SetMotion(Motion::Idle,8000,host.now);host.Rebase();
    keyboardPlay.fixtureForeground=nullptr;
    return BeginKeyboardCapture(host.now)&&keyboardPlay.input.Active()&&!keyboardPlay.hook;
}
bool Key(unsigned key,bool down=true){return ApplyKeyboardPlayInput(key,down).consume;}
struct Jump {double peak=0;unsigned launches=0,landings=0;bool returned=false;};
Jump Hop(Host& host) {
    Jump jump;Key(VK_SPACE);Key(VK_SPACE,false);
    bool airborne=false;
    for(unsigned frame=0;frame<550;++frame) {
        const auto before=g.physics.CurrentPose();Step(host);const auto after=g.physics.CurrentPose();
        if(before.grounded&&!after.grounded)++jump.launches;
        if(!before.grounded&&after.grounded)++jump.landings;
        airborne=airborne||!after.grounded;jump.peak=std::max(jump.peak,after.lift);
        if(airborne&&after.grounded&&!keyboardPlay.jumping&&!keyboardPlay.returning&&!g.waitingForStop&&g.groundArtBridge.Neutral()) {
            jump.returned=true;break;
        }
    }
    return jump;
}
int HitTestScreens(Host& host,unsigned& clickThroughPixels) {
    CreateDirectoryW(L"render-self-test",nullptr);CreateDirectoryW(L"render-self-test/keyboard-play",nullptr);
    HWND hidden=CreateWindowExW(WS_EX_LAYERED|WS_EX_TOOLWINDOW|WS_EX_NOACTIVATE,L"STATIC",L"",WS_POPUP,
        0,0,480,450,nullptr,nullptr,g.instance,nullptr);
    if(!hidden)return 783;
    int result=0;
    for(const auto character:cat::kAvailableCharacters) {
        if(!EnsureCareCoats(character)){result=783;break;}
        Clear();host.Reset(character,true);g.window=hidden;
        if(!g.surface.Create(480,450)){result=783;break;}
        SetMotion(Motion::Sleep,60000);g.motionStart=GetTickCount64()-8000;g.bubbleUntil=0;
        const auto& source=CurrentArt(character).base.skins[6].source;
        const auto sourceBefore=cat::Checksum(std::string(reinterpret_cast<const char*>(source.data()),source.size()*4));
        Render();
        const auto path=L"render-self-test/keyboard-play/character"+std::to_wstring(static_cast<unsigned>(character))+L"-body.png";
        if(!SaveNativeWalkingReview(*g.surface.image,path.c_str())){result=784;break;}
        POINT origin{};ClientToScreen(hidden,&origin);
        bool bodyHit=false,emptyPass=false;
        for(int y=0;y<g.surface.height&&(!bodyHit||!emptyPass);++y)
            for(int x=0;x<g.surface.width&&(!bodyHit||!emptyPass);++x) {
                const auto index=(static_cast<std::size_t>(y)*g.surface.width+x)*4;
                const BYTE alpha=g.surface.pixels[index+3];
                if((!bodyHit&&alpha>245)||(!emptyPass&&alpha==0)) {
                    const auto hit=WindowProc(hidden,WM_NCHITTEST,0,MAKELPARAM(origin.x+x,origin.y+y));
                    if(alpha>245){if(hit!=HTCLIENT){result=785;break;}bodyHit=true;}
                    else{if(hit!=HTTRANSPARENT){result=785;break;}emptyPass=true;++clickThroughPixels;}
                }
            }
        if(result||!bodyHit||!emptyPass){result=result?result:785;break;}
        const auto sourceAfter=cat::Checksum(std::string(reinterpret_cast<const char*>(source.data()),source.size()*4));
        if(sourceBefore!=sourceAfter){result=786;break;}
    }
    g.window=nullptr;DestroyWindow(hidden);g.surface.Reset();Clear();
    return result;
}
struct MovingJump {double peak=0,distance=0,speed=0;bool returned=false;};
MovingJump HopMoving(Host& host) {
    MovingJump jump;jump.speed=WalkingVelocity(g.physics.CurrentPose());
    const double origin=g.physics.CurrentPose().x;
    Key(VK_SPACE);Key(VK_SPACE,false);
    bool airborne=false;
    for(unsigned frame=0;frame<160;++frame) {
        Step(host);const auto physical=g.physics.CurrentPose();
        jump.peak=std::max(jump.peak,physical.lift);
        if(!physical.grounded) {
            airborne=true;
            if(std::abs(physical.velocityX-jump.speed)>1)return jump;
        }
        if(airborne&&physical.landed) {
            jump.distance=physical.x-origin;
            jump.returned=!keyboardPlay.jumping&&!keyboardPlay.returning&&!g.waitingForStop&&
                cat::animation::Walking(g.motion)&&!g.groundArtBridge.Stopping()&&
                std::abs(physical.velocityX-jump.speed)<1;
            break;
        }
    }
    return jump;
}

int Run() {
    Host host;
    std::string characters;
    int result=0;unsigned clickThroughPixels=0;
    for(const auto character:cat::kAvailableCharacters) {
        if(!Start(host,character)){result=770;break;}
        const auto saved=host.Saved();const double x=g.physics.CurrentPose().x;
        const auto menu=BuildPetMenu();
        if(!menu||(GetMenuState(menu,kKeyboardPlayCommand,MF_BYCOMMAND)&MF_CHECKED)==0){if(menu)DestroyMenu(menu);result=771;break;}
        DestroyMenu(menu);
        if(!Key(VK_RIGHT)){result=771;break;}
        Step(host,200);
        const double walk=WalkingVelocity(g.physics.CurrentPose());
        if(walk<10||g.physics.CurrentPose().x-x<20||!g.manualMotionActive||EnvironmentBusy()||
            CanStartAutonomousStory(host.now)){result=772;break;}
        Key(VK_SHIFT);Step(host,150);const double run=WalkingVelocity(g.physics.CurrentPose());
        if(run<walk*1.4){result=773;break;}
        Key(VK_RIGHT,false);Key(VK_LEFT);
        int flips=0,previousFacing=g.facing;
        for(unsigned frame=0;frame<190;++frame) {
            const auto before=g.physics.CurrentPose(),shown=g.physics.RenderPose();Step(host);
            if(g.facing!=previousFacing){++flips;if(std::abs(WalkingVelocity(before))>.8||std::abs(WalkingVelocity(shown))>.8){result=774;break;}}
            previousFacing=g.facing;
        }
        if(result||flips!=1||WalkingVelocity(g.physics.CurrentPose())>-10){result=result?result:774;break;}
        Key(VK_LEFT,false);Key(VK_SHIFT,false);Step(host,170);
        if(std::abs(WalkingVelocity(g.physics.CurrentPose()))>.5||!g.groundArtBridge.Neutral()){result=775;break;}
        const auto normal=Hop(host);
        if(normal.launches!=1||normal.landings!=1||normal.peak<25||!normal.returned){result=776;break;}
        if(Key('X')||Key('X',false)){result=777;break;} // Retired transform key belongs to the desktop.
        RunCommand(2305);WindowProc(nullptr,WM_HOTKEY,0x2602,0);
        if(!KeyboardPlayActive()||keyboardPlay.input.Direction()){result=777;break;}
        Key(VK_RIGHT);Step(host,160);
        const auto walkingJump=HopMoving(host);
        if(!walkingJump.returned||walkingJump.distance<walkingJump.speed*.6||
                std::abs(walkingJump.peak-normal.peak)>2){result=778;break;}
        Key(VK_SHIFT);Step(host,150);
        const auto runningJump=HopMoving(host);
        if(!runningJump.returned||runningJump.distance<runningJump.speed*.6||
                runningJump.speed<walkingJump.speed*1.4||std::abs(runningJump.peak-normal.peak)>2){result=778;break;}
        Key(VK_RIGHT,false);Key(VK_SHIFT,false);Step(host,170);
        if(host.Saved()!=saved||!g.pet.roamingPaused){result=779;break;}
        Key(VK_RIGHT);Step(host,3);Key(VK_ESCAPE);
        if(KeyboardPlayActive()||keyboardPlay.hook||keyboardPlay.input.Direction()||keyboardPlay.input.Running()||g.manualMotionActive){result=780;break;}
        if(!characters.empty())characters+=",";
        characters+="{\"character\":"+std::to_string(static_cast<unsigned>(character))+",\"walk_speed\":"+
            std::to_string(walk)+",\"run_speed\":"+std::to_string(run)+
            ",\"normal_jump_peak\":"+std::to_string(normal.peak)+",\"walk_jump_distance\":"+std::to_string(walkingJump.distance)+
            ",\"run_jump_distance\":"+std::to_string(runningJump.distance)+"}";
    }
    unsigned safetyCases=0;
    if(!result)for(unsigned state=0;state<8;++state) {
        if(!Start(host,cat::Character::Cream)){result=770;break;}
        const auto saved=host.Saved();Key(VK_RIGHT);Step(host,20);
        switch(state){case 0:g.hidden=true;break;case 1:g.menuOpen=true;break;case 2:g.dragging=true;break;
        case 3:updates.applying=true;break;case 4:keyboardPlay.foreground=reinterpret_cast<HWND>(1);keyboardPlay.fixtureForeground=reinterpret_cast<HWND>(2);break;
        case 5:SetMotion(Motion::Eat,5000,host.now);break;case 6:ApplyKeyboardPlayInput(VK_LEFT,true,true);break;
        default:g.closing=true;break;}
        UpdateKeyboardPlay(host.now+16,.016);
        if(KeyboardPlayActive()||keyboardPlay.hook||keyboardPlay.input.Direction()||host.Saved()!=saved||
            (state==5&&g.motion!=Motion::Eat)){result=781;break;}
        ++safetyCases;
    }
    if(!result) {
        Clear();host.Reset(cat::Character::Cream,true);keyboardPlay.fixture=true;
        SetMotion(Motion::Sleep,60000,host.now);ToggleKeyboardPlay();
        if(!keyboardPlay.requested||keyboardPlay.input.Active()||keyboardPlay.hook||g.motion!=Motion::Stretch)result=782;
        else {for(unsigned frame=0;frame<600&&!keyboardPlay.input.Active();++frame)Step(host);
            if(!keyboardPlay.input.Active())result=782;}
    }
    Clear();
    if(!result)result=HitTestScreens(host,clickThroughPixels);
    CreateDirectoryW(L"render-self-test",nullptr);
    const std::string proof="{\"schema\":1,\"test\":\"keyboard-play-native\",\"version\":\"" YEOPJARI_VERSION_STRING
        "\",\"status\":\""+(result?std::string("failed"):std::string("passed"))+"\",\"exit_code\":"+std::to_string(result)+
        ",\"input\":\"fake key edges, no OS injection\",\"render\":\"production Render to hidden layered window; native GDI+, not desktop recording\","
        "\"controls\":\"CtrlAltM; arrows/AD; Shift; Space; Esc\",\"safety_cases\":"+std::to_string(safetyCases)+
        ",\"pause_preference_preserved\":true,\"real_box2d_jump\":true,\"planted_reversal\":true,\"same_coat_source_hash\":true,"
        "\"alpha_click_through_characters\":"+std::to_string(clickThroughPixels)+",\"characters\":["+characters+"]}\n";
    const bool written=WriteBytes(L"render-self-test/keyboard-play-report.json",proof);
    Clear();keyboardPlay.fixture=false;g.initialized=false;g.window=nullptr;g.hidden=g.menuOpen=g.dragging=g.closing=false;
    updates.applying=false;g.manualMotionActive=false;
    return result?result:written?0:787;
}
} // namespace keyboard_native
