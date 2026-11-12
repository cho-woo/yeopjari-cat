#pragma once

// Production care commands, motor/Box2D updates, pose selection, registered
// PNG frames and GDI+ drawing. This is native offscreen output, not a desktop
// recording. Only a dedicated diagnostic fixture is saved by the meal command.
namespace native_video_motion_capture {
using Clip=cat::sprite_sequence::Clip;
constexpr unsigned Fps=60;
constexpr int Width=960,Height=500;
struct Frame {
    cat::animation::Pose pose;
    double x=0,velocity=0,phase=0,referenceHeight=0;
    unsigned source=0,resource=0;
    Motion requested=Motion::Idle,displayed=Motion::Idle;
    bool waiting=false;
};
struct Row {
    cat::Character character=cat::Character::Cream;
    cat::nutrition::Visual body;
    std::vector<Frame> frames;
    CarePresentationRenderer presentation;
    double startX=0,peakWalk=0,peakRun=0;
    unsigned eatFramesUsed=0,runFramesUsed=0;
    bool mealFinished=false,stopped=false;
};
struct Schedule {
    ULONGLONG meal=500,walk=0,run=0,recovery=0,duration=0;
    unsigned frames=0;
};

int Simulate(Row& row,const Schedule& schedule,std::string& context) {
    native_manual_motion_tests::Host host;
    host.Reset(row.character,true);g.workInterference=false;g.cursorPlay=false;
    // CareFood deliberately persists accepted meals. Its real dispatcher is
    // exercised against an isolated fixture, without loading a user profile.
    g.directory=L"render-self-test/motion-video/fixture-"+std::to_wstring(static_cast<unsigned>(row.character));
    if(!CreateDirectoryW(g.directory.c_str(),nullptr)&&GetLastError()!=ERROR_ALREADY_EXISTS)return 561;
    g.savePath=g.directory+L"/pet.ini";g.backupPath=g.directory+L"/pet.bak";
    g.nutritionPath=g.directory+L"/nutrition.ini";g.nutritionBackupPath=g.directory+L"/nutrition.bak";
    g.nutritionReady=true;g.nutrition.fullness=g.pet.hunger=50;
    g.companionStore.outcome=cat::companion::Outcome::Missing;
    const auto start=host.now;
    row.startX=g.physics.RenderPose().x;
    row.body=cat::appearance::VisualModifiers(g.nutrition,g.preferences.character);
    row.frames.reserve(schedule.frames);
    const auto* eat=LoadedCareSprite(Clip::Eat,row.character);
    const auto* run=LoadedCareSprite(Clip::Run,row.character);
    if(!eat||!run||!eat->layout->program.stepCount||!run->layout->program.loop.count||
            eat->layout->nominalFramesPerSecond!=24||run->layout->nominalFramesPerSecond!=24||
            std::string(eat->layout->sourcePath).find("assets/video-actions/")!=0||
            std::string(run->layout->sourcePath).find("assets/video-actions/")!=0) {
        context="capture requires actual 24 FPS source-video Eat and Run registrations";return 562;
    }
    std::vector<bool> seenEat(eat->layout->frameCount,false),seenRun(run->layout->frameCount,false);
    bool mealStarted=false,walkStarted=false,runStarted=false;
    for(unsigned index=0;index<schedule.frames;++index) {
        const auto elapsed=static_cast<ULONGLONG>(index)*1000/Fps;
        if(index)host.Frame(start+elapsed-host.now);
        context="native meal/walk/run character="+std::to_string(static_cast<unsigned>(row.character))+
            " frame="+std::to_string(index);
        if(!mealStarted&&elapsed>=schedule.meal) {
            CareFood(cat::feeding::FoodKind::Meal);host.Rebase();mealStarted=true;
            if(g.motion!=Motion::Eat||g.pet.feedCount!=1||g.pendingCare>=0||g.saveErrorShown)return 563;
        }
        if(!walkStarted&&elapsed>=schedule.walk) {
            if(g.motion==Motion::Eat||g.waitingForStop||g.groundArtBridge.Starting()||
                    g.groundArtBridge.Stopping())return 564;
            row.mealFinished=true;
            host.Command(kManualWalkCommand);walkStarted=true;
            if(!g.manualMotionActive||g.motion!=Motion::Walk)return 564;
        }
        if(!runStarted&&elapsed>=schedule.run) {
            if(g.manualMotionActive||std::abs(WalkingVelocity(g.physics.CurrentPose()))>.5)return 565;
            host.Command(kManualRunCommand);runStarted=true;
            if(!g.manualMotionActive||g.motion!=Motion::Zoomies)return 565;
        }
        const auto physical=g.physics.RenderPose();
        if(!physical.grounded||!std::isfinite(physical.x)||!std::isfinite(physical.velocityX))return 566;
        const auto displayed=cat::motion::DisplayedMotion(g.motion,g.walkingMotion,
            physical.grounded,g.waitingForStop,WalkingVelocity(physical));
        const auto pose=PrimaryDisplayedPose(host.now,physical);
        const auto* asset=CareSpriteFor(pose,row.character);
        const unsigned source=asset?SpriteFrameFor(pose,*asset->layout):static_cast<unsigned>(pose.frame);
        if(asset&&source>=asset->layout->frameCount)return 566;
        const double velocity=WalkingVelocity(physical);
        if(g.motion==Motion::Zoomies&&std::abs(velocity)>2&&
                g.gait.AuthoredMode()==cat::authored_locomotion::Mode::Run&&
                !g.groundArtBridge.Starting()&&pose.spriteClip!=Clip::Run) {
            context+=" sprint selected a non-Run loop pose";return 566;
        }
        if(pose.spriteClip==Clip::Eat&&!seenEat[source]){seenEat[source]=true;++row.eatFramesUsed;}
        if(pose.spriteClip==Clip::Run&&!seenRun[source]){seenRun[source]=true;++row.runFramesUsed;}
        if(g.motion==Motion::Walk)row.peakWalk=std::max(row.peakWalk,std::abs(velocity));
        if(g.motion==Motion::Zoomies)row.peakRun=std::max(row.peakRun,std::abs(velocity));
        if(elapsed>schedule.run+3200+schedule.recovery&&!g.manualMotionActive&&
                g.stopClip==Clip::None&&std::abs(velocity)<.5)row.stopped=true;
        row.frames.push_back({pose,physical.x,velocity,g.gait.AuthoredPhase(),
            asset?asset->layout->referenceHeight:CurrentArt(row.character).base.standingHeight,source,
            asset?static_cast<unsigned>(asset->layout->resourceId):static_cast<unsigned>(CurrentArt(row.character).base.baseResourceId),
            g.motion,displayed,g.waitingForStop});
    }
    std::vector<bool> requiredEat(eat->layout->frameCount,false),requiredRun(run->layout->frameCount,false);
    for(unsigned i=0;i<eat->layout->program.stepCount;++i)requiredEat[eat->layout->program.steps[i].frame]=true;
    for(unsigned i=0;i<run->layout->program.loop.count;++i)
        requiredRun[run->layout->program.steps[run->layout->program.loop.first+i].frame]=true;
    for(unsigned i=0;i<requiredEat.size();++i)if(requiredEat[i]&&!seenEat[i]) {
        context+=" missing meal source frame="+std::to_string(i);return 567;
    }
    for(unsigned i=0;i<requiredRun.size();++i)if(requiredRun[i]&&!seenRun[i]) {
        context+=" missing run loop frame="+std::to_string(i);return 567;
    }
    if(!row.mealFinished||!row.stopped||g.pet.feedCount!=1||g.pet.snackCount||
            row.peakWalk<RegisteredWalkSpeed()*1.03||row.peakWalk>RegisteredWalkSpeed()*1.28||
            row.peakRun<RegisteredRunSpeed()*1.03||row.peakRun>RegisteredRunSpeed()*1.28) {
        context+=" peak Walk="+std::to_string(row.peakWalk)+" Run="+std::to_string(row.peakRun)+
            " stopped="+std::to_string(row.stopped)+" remaining stop="+std::to_string(static_cast<unsigned>(g.stopClip))+
            " velocity="+std::to_string(WalkingVelocity(g.physics.CurrentPose()));return 568;
    }
    return 0;
}

int Run(std::string& context) {
    if(!LoadAtlas())return 560;
    for(const auto path:{L"render-self-test",L"render-self-test/motion-video"})
        if(!CreateDirectoryW(path,nullptr)&&GetLastError()!=ERROR_ALREADY_EXISTS)return 561;
    const auto* meal=LoadedCareSprite(Clip::Eat,cat::Character::Cream);
    const auto* walk=LoadedCareSprite(Clip::Walk,cat::Character::Cream);
    if(!meal||!walk||!meal->layout->program.stepCount)return 562;
    Schedule schedule;
    schedule.walk=schedule.meal+walk->layout->movementDelayMs+
        cat::sprite_sequence::ProgramDuration(meal->layout->program)+
        cat::sprite_sequence::Duration(walk->layout->program,walk->layout->program.outro)+600;
    schedule.run=schedule.walk+5000+cat::sprite_sequence::Duration(walk->layout->program,walk->layout->program.outro)+800;
    for(const auto clip:{Clip::Brake,Clip::Slide,Clip::Stumble}) {
        const auto* stop=LoadedCareSprite(clip,cat::Character::Cream);if(!stop)return 562;
        schedule.recovery=std::max<ULONGLONG>(schedule.recovery,cat::sprite_sequence::ProgramDuration(stop->layout->program));
    }
    // Capture the complete longest source recovery AND physical settling;
    // the former1800ms tail could randomly truncate a real skid/tumble.
    schedule.duration=schedule.run+3200+schedule.recovery+1400;
    if(schedule.duration>30000)return 562;
    schedule.frames=static_cast<unsigned>((schedule.duration*Fps+999)/1000);
    std::array<Row,2> rows{};rows[0].character=cat::Character::Cream;rows[1].character=cat::Character::Tuxedo;
    for(auto& row:rows)if(const int result=Simulate(row,schedule,context))return result;
    Surface canvas;if(!canvas.Create(Width,Height))return 561;
    std::ostringstream trace;trace.imbue(std::locale::classic());
    trace<<"{\"schema\":1,\"fps\":"<<Fps<<",\"frames\":[";
    for(unsigned index=0;index<schedule.frames;++index) {
        const auto elapsed=static_cast<ULONGLONG>(index)*1000/Fps;
        context="GDI+ meal/walk/run capture frame="+std::to_string(index);
        {
            Graphics graphics(canvas.image.get());
            graphics.SetCompositingMode(CompositingModeSourceCopy);graphics.Clear(Color(255,246,244,239));
            graphics.SetCompositingMode(CompositingModeSourceOver);
            graphics.SetCompositingQuality(CompositingQualityHighQuality);
            graphics.SetInterpolationMode(InterpolationModeHighQualityBicubic);graphics.SetSmoothingMode(SmoothingModeAntiAlias);
            graphics.SetPixelOffsetMode(PixelOffsetModeHighQuality);graphics.SetTextRenderingHint(TextRenderingHintAntiAliasGridFit);
            Font title(L"Segoe UI",20,FontStyleRegular,UnitPixel),small(L"Segoe UI",12,FontStyleRegular,UnitPixel);
            SolidBrush ink(Color(255,41,53,60));
            graphics.DrawString(L"Source-video meal, walk and run / actual Windows renderer",-1,&title,PointF(22,12),&ink);
            graphics.DrawString(L"60 FPS GDI+ + Box2D; 110 px characters. Offscreen native capture, not a desktop recording.",-1,
                &small,PointF(23,42),&ink);
            const wchar_t* stage=elapsed<schedule.meal?L"Rest":elapsed<schedule.walk?L"Meal / rest":
                elapsed<schedule.run?L"Walk / brake":L"Run / brake";
            graphics.DrawString(stage,-1,&small,PointF(820,18),&ink);
            if(index)trace<<',';
            trace<<"{\"frame\":"<<index<<",\"elapsed_ms\":"<<elapsed<<",\"characters\":[";
            for(unsigned r=0;r<rows.size();++r) {
                auto& row=rows[r];const auto& frame=row.frames[index];
                const REAL floor=r?460.0f:245.0f;
                const double worldCenter=300+frame.x-row.startX;
                const double camera=std::clamp(worldCenter,180.0,Width-180.0)-worldCenter;
                const REAL center=static_cast<REAL>(worldCenter+camera);
                Pen ground(Color(255,185,196,197),1);
                graphics.DrawLine(&ground,25.0f,floor+1.0f,static_cast<REAL>(Width-25),floor+1.0f);
                // Only the review camera pans. Markers keep ground travel
                // visible while physical world coordinates remain untouched.
                for(int mark=-2000;mark<=4000;mark+=40) {
                    const REAL x=static_cast<REAL>(300+mark-row.startX+camera);
                    if(x>=25&&x<=Width-25)graphics.DrawLine(&ground,x,floor+1,x,floor+6);
                }
                graphics.DrawString(r?L"Tuxedo":L"Cream",-1,&small,PointF(25,floor-152),&ink);
                DrawPetStyle(graphics,row.character,cat::Outfit::None,frame.pose,row.body,
                    center,floor,110,&row.presentation,elapsed,0,false);
                if(r)trace<<',';
                trace<<"{\"character\":"<<static_cast<unsigned>(row.character)<<",\"root_x\":"<<frame.x
                    <<",\"canvas_root_x\":"<<center<<",\"camera_offset_x\":"<<camera
                    <<",\"floor_y\":"<<floor<<",\"reference_height_px\":110"
                    <<",\"source_reference_height\":"<<frame.referenceHeight<<",\"velocity_x\":"<<frame.velocity
                    <<",\"phase\":"<<frame.phase<<",\"requested_motion\":"<<static_cast<unsigned>(frame.requested)
                    <<",\"displayed_motion\":"<<static_cast<unsigned>(frame.displayed)
                    <<",\"sprite_clip\":"<<static_cast<unsigned>(frame.pose.spriteClip)
                    <<",\"sprite_resource\":"<<frame.resource<<",\"sprite_frame\":"<<frame.source
                    <<",\"facing\":"<<(frame.pose.flip?-1:1)<<",\"waiting_for_stop\":"<<(frame.waiting?"true":"false")<<'}';
            }
            trace<<"]}";graphics.Flush(FlushIntentionSync);if(graphics.GetLastStatus()!=Ok)return 569;
        }
        const auto path=L"render-self-test/motion-video/frame-"+
            std::wstring(5-std::to_wstring(index).size(),L'0')+std::to_wstring(index)+L".png";
        if(!SaveNativeWalkingReview(*canvas.image,path.c_str()))return 569;
    }
    trace<<"]}\n";
    std::ostringstream metadata;metadata.imbue(std::locale::classic());
    metadata<<"{\"schema\":1,\"render_source\":\"Windows GDI+ / Box2D\",\"desktop_recording\":false,\"fps\":"<<Fps
        <<",\"frame_count\":"<<schedule.frames<<",\"canvas\":["<<Width<<','<<Height<<"],\"reference_height_px\":110"
        <<",\"camera\":\"display-only pan keeps the complete cat visible; trace retains unchanged Box2D world coordinates\""
        <<",\"scenario\":\"real meal command, complete meal, rest, manual walk, brake and rest, manual run, brake and rest\""
        <<",\"schedule_ms\":{\"meal\":"<<schedule.meal<<",\"walk\":"<<schedule.walk<<",\"run\":"<<schedule.run
        <<",\"recovery\":"<<schedule.recovery<<",\"duration\":"<<schedule.duration<<"},\"clips\":[";
    bool first=true;
    for(const auto clip:{Clip::Walk,Clip::Run,Clip::Eat}) {
        const auto* asset=LoadedCareSprite(clip,cat::Character::Cream);if(!asset)return 562;
        const auto& layout=*asset->layout;if(!first)metadata<<',';first=false;
        metadata<<"{\"clip\":"<<static_cast<unsigned>(clip)<<",\"resource\":"<<layout.resourceId
            <<",\"source_path\":\""<<layout.sourcePath<<"\",\"art_frames\":"<<layout.frameCount
            <<",\"loop_steps\":"<<layout.program.loop.count<<",\"source_fps\":"<<layout.nominalFramesPerSecond
            <<",\"reference_height\":"<<layout.referenceHeight<<",\"stride_pixels\":"<<layout.stridePixels<<'}';
    }
    metadata<<"],\"characters\":[";
    for(unsigned i=0;i<rows.size();++i) {
        if(i)metadata<<',';const auto& row=rows[i];
        metadata<<"{\"character\":"<<static_cast<unsigned>(row.character)<<",\"meal_finished\":true,\"stopped\":true"
            <<",\"peak_walk_speed\":"<<row.peakWalk<<",\"peak_run_speed\":"<<row.peakRun
            <<",\"meal_art_frames_used\":"<<row.eatFramesUsed<<",\"run_art_frames_used\":"<<row.runFramesUsed<<'}';
    }
    metadata<<"],\"trace\":\"trace.json\",\"profile\":\"isolated capture fixture only\"}\n";
    if(!WriteBytes(L"render-self-test/motion-video/trace.json",trace.str())||
            !WriteBytes(L"render-self-test/motion-video/capture.json",metadata.str()))return 570;
    context="complete actual source-video meal, manual Walk and Run selected for both coats at 60 FPS";return 0;
}
} // namespace native_video_motion_capture

int NativeVideoMotionCapture(HINSTANCE instance) {
    g.instance=instance;
    const HRESULT com=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);if(FAILED(com))return 560;
    ULONG_PTR token=0;GdiplusStartupInput startup;
    if(GdiplusStartup(&token,&startup,nullptr)!=Ok){CoUninitialize();return 560;}
    int result=0;std::string context="motion capture initialization";
    try{result=native_video_motion_capture::Run(context);}catch(...){result=579;}
    WriteBytes(L"render-self-test/motion-video-report.txt","exit="+std::to_string(result)+" "+context+"\n");
    ReleaseAtlases();ReleaseCarePresentation();g.styleBlendFrom.Reset();g.styleBlendTo.Reset();
    g.initialized=false;g.manualMotionActive=false;g.window=nullptr;cursorInteraction={};
    GdiplusShutdown(token);CoUninitialize();return result;
}
