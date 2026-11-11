#pragma once

namespace native_fly_review {
using Clip=cat::sprite_sequence::Clip;
int Run(std::string& context) {
    CreateDirectoryW(L"render-self-test",nullptr);CreateDirectoryW(L"render-self-test/fly-review",nullptr);
    Surface actor,board;
    if(!actor.Create(340,240)||!board.Create(2720,720))return 870;
    {Graphics graphics(board.image.get());graphics.Clear(Color(255,245,247,249));}
    std::ostringstream report;report<<"{\"schema\":1,\"capture\":\"native Windows GDI+ / Box2D\",\"characters\":[";
    unsigned row=0;
    for(const auto character:cat::kAvailableCharacters) {
        native_manual_motion_tests::Host host;host.Reset(character,true);
        if(!host.artworkReady)return 871;
        g.cursorPlay=g.workInterference=false;
        const auto saved=host.Saved();host.Command(kFlyHuntCommand);
        const auto* sprite=LoadedCareSprite(Clip::FlyHunt,character);
        if(!sprite||!g.flyActive||!g.manualMotionActive||g.motion!=Motion::Watch||
                sprite->layout->frameCount!=166||sprite->layout->program.cueCount!=4)return 872;
        const auto duration=cat::sprite_sequence::ProgramDuration(sprite->layout->program);
        const auto origin=g.physics.CurrentPose().x;
        std::vector<bool> visited(sprite->layout->frameCount,false);unsigned distinct=0,sample=0;
        bool complete=false;const auto start=host.now;
        for(unsigned tick=0;tick<650;++tick) {
            if(tick)host.Frame(start+static_cast<ULONGLONG>(tick)*1000/60-host.now);
            const auto physical=g.physics.RenderPose();const auto pose=PrimaryDisplayedPose(host.now,physical);
            if(!physical.grounded||std::abs(physical.x-origin)>.05||physical.lift>.1)return 873;
            if(pose.spriteClip==Clip::FlyHunt) {
                const auto index=SpriteFrameFor(pose,*sprite->layout);
                if(index>=visited.size()||!g.flyActive||pose.fishMeal)return 874;
                const bool firstDrawing=!visited[index];
                if(firstDrawing){visited[index]=true;++distinct;}
                {
                    Graphics graphics(actor.image.get());graphics.Clear(Color(0,0,0,0));
                    graphics.SetInterpolationMode(InterpolationModeHighQualityBicubic);
                    DrawPetStyle(graphics,character,cat::Outfit::None,pose,
                        cat::appearance::VisualModifiers(g.nutrition,character),130,220,110);
                    graphics.Flush(FlushIntentionSync);if(graphics.GetLastStatus()!=Ok)return 875;
                }
                unsigned painted=0;
                for(int y=0;y<actor.height;++y)for(int x=0;x<actor.width;++x) {
                    const auto* pixel=actor.pixels+(static_cast<std::size_t>(y)*actor.width+x)*4;
                    if(pixel[0]>pixel[3]||pixel[1]>pixel[3]||pixel[2]>pixel[3])return 875;
                    if(pixel[3]>16) {
                        ++painted;if(x==0||y==0||x==actor.width-1||y==actor.height-1)return 875;
                    }
                }
                if(painted<500)return 875;
                if(firstDrawing) {
                    wchar_t path[180]{};swprintf(path,180,L"render-self-test/fly-review/c%u-frame-%03u.png",
                        static_cast<unsigned>(character),index);
                    if(!SaveNativeWalkingReview(*actor.image,path))return 876;
                }
                if(sample<8&&index>=((sprite->layout->frameCount-1)*sample/7)) {
                    Graphics graphics(board.image.get());graphics.DrawImage(actor.image.get(),static_cast<int>(sample*340),static_cast<int>(row*240));++sample;
                }
            }
            if(!g.flyActive&&distinct==visited.size()){complete=true;break;}
        }
        context="fly character="+std::to_string(static_cast<unsigned>(character))+" visited="+std::to_string(distinct)+
            " source="+std::to_string(sprite->layout->frameCount)+" complete="+std::to_string(complete);
        if(distinct!=visited.size()||sample!=8||!complete||host.Saved()!=saved||g.manualMotionActive)return 877;
        if(row)report<<',';
        report<<"{\"character\":"<<static_cast<unsigned>(character)<<",\"source_frames\":"<<distinct
            <<",\"duration_ms\":"<<duration<<",\"strikes\":4,\"ground_root_fixed\":true,\"save_unchanged\":true}";
        ++row;
    }
    // An already moving animal finishes its physical brake before the hunt clock.
    native_manual_motion_tests::Host host;host.Reset();host.Command(kManualRunCommand);
    for(unsigned i=0;i<75;++i)host.Frame();
    host.Command(kFlyHuntCommand);bool stopped=false;
    for(unsigned i=0;i<300;++i) {
        const auto physical=g.physics.RenderPose();const auto pose=PrimaryDisplayedPose(host.now,physical);
        if(g.waitingForStop&&pose.spriteClip==Clip::FlyHunt)return 878;
        if(!g.waitingForStop&&pose.spriteClip==Clip::FlyHunt){stopped=true;break;}
        host.Frame();
    }
    if(!stopped)return 878;
    SetMotion(Motion::Groom,6000,host.now);
    if(g.flyActive)return 879;
    for(unsigned state=0;state<3;++state) {
        host.Reset();g.hidden=state==0;g.menuOpen=state==1;g.dragging=state==2;
        const auto serial=g.motionSerial;RunCommand(kFlyHuntCommand,host.now);
        if(g.flyActive||g.motionSerial!=serial)return 879;
    }
    report<<"],\"moving_brake_checked\":true,\"interrupt_checked\":true,\"unsafe_states_checked\":3}\n";
    return SaveNativeWalkingReview(*board.image,L"render-self-test/fly-review/fly-board.png")&&
        WriteBytes(L"render-self-test/fly-review/report.json",report.str())?0:876;
}
}
int NativeFlyReview(HINSTANCE instance) {
    g.instance=instance;ULONG_PTR token=0;GdiplusStartupInput startup;
    if(GdiplusStartup(&token,&startup,nullptr)!=Ok)return 869;
    std::string context="source-video fly review";
    const int result=LoadAtlas()?native_fly_review::Run(context):870;
    WriteBytes(L"render-self-test/fly-review-report.txt","exit="+std::to_string(result)+" "+context+"\n");
    ReleaseAtlases();ReleaseCarePresentation();g.brain.reset();GdiplusShutdown(token);return result;
}
