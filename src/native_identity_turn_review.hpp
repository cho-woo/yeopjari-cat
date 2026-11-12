#pragma once

namespace native_identity_turn_review {
using Clip=cat::sprite_sequence::Clip;
int Run(std::string& context) {
    CreateDirectoryW(L"render-self-test",nullptr);CreateDirectoryW(L"render-self-test/identity-turn",nullptr);
    CreateDirectoryW(L"render-self-test/identity-turn/burst",nullptr);
    Surface actor,board,world;
    if(!actor.Create(340,260)||!board.Create(2720,780)||!world.Create(960,280))return 880;
    {Graphics graphics(board.image.get());graphics.Clear(Color(255,245,247,249));}
    std::ostringstream report;report<<"{\"schema\":1,\"version\":\"" YEOPJARI_VERSION_STRING
        "\",\"capture\":\"production Windows GDI+ / Box2D\",\"characters\":[";
    unsigned row=0,worldFrames=0;
    for(const auto character:cat::kAvailableCharacters) {
        native_manual_motion_tests::Host host;host.Reset(character,true);
        if(!host.artworkReady)return 881;
        g.cursorPlay=g.workInterference=false;
        g.work.right=g.work.left+static_cast<LONG>(900*g.dpiScale);ResetMovement();
        for(unsigned i=0;i<12;++i)g.physics.Update(1./60);
        host.Command(kManualRunCommand);g.nextDecision=host.now+32000;
        const auto saved=host.Saved();const auto* sprite=LoadedCareSprite(Clip::RunningTurn,character);
        if(!sprite||sprite->layout->frameCount!=110||cat::sprite_sequence::ProgramDuration(sprite->layout->program)!=
                cat::running_turn::Turn::BrakeMs+cat::running_turn::Turn::PivotMs+cat::running_turn::Turn::LaunchMs)return 882;
        std::vector<bool> seen(sprite->layout->frameCount,false);unsigned distinct=0,turns=0,sample=0;bool previousTurning=false;
        double maxStep=0;const auto start=host.now;bool rested=false;
        for(unsigned tick=0;tick<3200;++tick) {
            const auto before=g.physics.RenderPose();
            if(tick)host.Frame(start+static_cast<ULONGLONG>(tick)*1000/60-host.now);
            const auto physical=g.physics.RenderPose();const auto pose=PrimaryDisplayedPose(host.now,physical);
            maxStep=std::max(maxStep,std::abs(physical.x-before.x));
            if(!physical.grounded||physical.lift>.1||maxStep>5){
                context="tick="+std::to_string(tick)+" grounded="+std::to_string(physical.grounded)+
                    " lift="+std::to_string(physical.lift)+" step="+std::to_string(maxStep)+
                    " x="+std::to_string(physical.x)+" vx="+std::to_string(physical.velocityX);
                return 883;
            }
            const bool turning=g.runningTurn.Active();if(turning&&!previousTurning)++turns;previousTurning=turning;
            if(!row&&tick%2==0) {
                {Graphics graphics(world.image.get());graphics.Clear(Color(255,245,247,249));
                    graphics.SetInterpolationMode(InterpolationModeHighQualityBicubic);
                    Pen floor(Color(255,210,214,220));graphics.DrawLine(&floor,0,241,960,241);
                    DrawPetStyle(graphics,character,cat::Outfit::None,pose,
                        cat::appearance::VisualModifiers(g.nutrition,character),static_cast<REAL>(30+physical.x),240,110);
                    graphics.Flush(FlushIntentionSync);}
                wchar_t path[180]{};swprintf(path,180,L"render-self-test/identity-turn/burst/frame-%04u.png",worldFrames++);
                if(!SaveNativeWalkingReview(*world.image,path))return 887;
            }
            if(turning) {
                if(g.motion!=Motion::Zoomies||pose.spriteClip!=Clip::RunningTurn||g.groundArtBridge.Starting()||g.groundArtBridge.Stopping())return 884;
                if(!g.runningTurn.Released()&&WalkingVelocity(physical)*g.runningTurn.From()<-2)return 884;
                const auto index=SpriteFrameFor(pose,*sprite->layout);
                if(index>=seen.size())return 885;
                const bool first=!seen[index];if(first){seen[index]=true;++distinct;}
                {Graphics graphics(actor.image.get());graphics.Clear(Color(0,0,0,0));
                    graphics.SetInterpolationMode(InterpolationModeHighQualityBicubic);
                    DrawPetStyle(graphics,character,cat::Outfit::None,pose,
                        cat::appearance::VisualModifiers(g.nutrition,character),160,240,110);
                    graphics.Flush(FlushIntentionSync);if(graphics.GetLastStatus()!=Ok)return 886;}
                unsigned painted=0;
                for(int y=0;y<actor.height;++y)for(int x=0;x<actor.width;++x) {
                    const auto* pixel=actor.pixels+(static_cast<std::size_t>(y)*actor.width+x)*4;
                    if(pixel[0]>pixel[3]||pixel[1]>pixel[3]||pixel[2]>pixel[3])return 886;
                    if(pixel[3]>16){++painted;if(!x||!y||x==actor.width-1||y==actor.height-1)return 886;}
                }
                if(painted<500)return 886;
                if(first) {wchar_t path[180]{};swprintf(path,180,L"render-self-test/identity-turn/c%u-turn-%03u.png",
                    static_cast<unsigned>(character),index);if(!SaveNativeWalkingReview(*actor.image,path))return 887;}
                if(sample<8&&index>=(sprite->layout->frameCount-1)*sample/7){Graphics graphics(board.image.get());graphics.DrawImage(actor.image.get(),static_cast<int>(sample*340),static_cast<int>(row*260));++sample;}
            }
            if(!g.manualMotionActive&&!turning&&g.stopClip==Clip::None&&!g.waitingForStop&&
                    g.groundArtBridge.Neutral()&&host.now-start>32000){rested=true;break;}
        }
        context="turn character="+std::to_string(static_cast<unsigned>(character))+" turns="+std::to_string(turns)+
            " frames="+std::to_string(distinct)+" rested="+std::to_string(rested);
        if(!rested||turns<2||distinct!=sprite->layout->frameCount||sample!=8||host.Saved()!=saved)return 888;
        const auto restSerial=g.motionSerial,restDeadline=g.nextDecision;
        for(unsigned i=0;i<1200;++i)host.Frame(16);
        if(g.motionSerial!=restSerial||g.nextDecision!=restDeadline||host.Saved()!=saved)return 889;
        if(row)report<<',';
        report<<"{\"character\":"<<static_cast<unsigned>(character)<<",\"turns\":"<<turns<<",\"registered_frames\":"<<distinct
            <<",\"max_root_step\":"<<maxStep<<",\"no_seated_reversal\":true,\"quiet_after_burst\":true,\"save_unchanged\":true}";
        ++row;
    }
    report<<"],\"burst_fps\":30,\"burst_frame_count\":"<<worldFrames<<"}\n";
    return SaveNativeWalkingReview(*board.image,L"render-self-test/identity-turn/turn-board.png")&&
        WriteBytes(L"render-self-test/identity-turn/report.json",report.str())?0:887;
}
int IdentityBoard() {
    Surface actor,board;if(!actor.Create(320,240)||!board.Create(320*10,240*6))return 890;
    {Graphics graphics(board.image.get());graphics.Clear(Color(255,245,247,249));}
    unsigned row=0;
    for(const auto character:cat::kAvailableCharacters) {
        native_manual_motion_tests::Host host;host.Reset(character,true);
        unsigned ordinal=0;
        for(const auto clip:cat::sprite_sequence::kAuthoredClips) {
            const auto* asset=LoadedCareSprite(clip,character);if(!asset)return 891;
            cat::animation::Pose pose;pose.spriteClip=clip;pose.spriteTimed=false;pose.spriteFrame=asset->layout->frameCount/2;
            cat::animation::UseAuthoredSprite(pose);
            {Graphics graphics(actor.image.get());graphics.Clear(Color(0,0,0,0));graphics.SetInterpolationMode(InterpolationModeHighQualityBicubic);
                DrawPetStyle(graphics,character,cat::Outfit::None,pose,
                    cat::appearance::VisualModifiers(g.nutrition,character),140,220,110);graphics.Flush(FlushIntentionSync);}
            {Graphics graphics(board.image.get());graphics.DrawImage(actor.image.get(),static_cast<int>((ordinal%10)*320),static_cast<int>((row*2+ordinal/10)*240));}
            ++ordinal;
        }
        ++row;
    }
    return SaveNativeWalkingReview(*board.image,L"render-self-test/identity-turn/identity-board.png")?0:892;
}
}
int NativeIdentityTurnReview(HINSTANCE instance) {
    g.instance=instance;ULONG_PTR token=0;GdiplusStartupInput startup;
    if(GdiplusStartup(&token,&startup,nullptr)!=Ok)return 879;
    std::string context="canonical identity and running turn";
    int result=LoadAtlas()?native_identity_turn_review::Run(context):880;
    if(!result)result=native_identity_turn_review::IdentityBoard();
    WriteBytes(L"render-self-test/identity-turn-report.txt","exit="+std::to_string(result)+" "+context+"\n");
    ReleaseAtlases();ReleaseCarePresentation();g.brain.reset();GdiplusShutdown(token);return result;
}
