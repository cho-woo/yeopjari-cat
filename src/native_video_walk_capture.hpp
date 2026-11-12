#pragma once

// Offline diagnostics only. The shipping dispatcher/motor/gait and actual
// registered GDI+ textures produce every captured image. No profile is opened,
// no update worker/window is created, and no pose is interpolated by a movie
// tool. This is an offscreen native capture, not a desktop recording.
namespace native_video_walk_capture {
constexpr unsigned Fps=60, Count=1860;
constexpr int Width=960, Height=500;
using Clip=cat::sprite_sequence::Clip;
// These are the actual source-video drawings packed in the registered atlas,
// not generated in-betweens. The registered loop retains the source movie.
int SourceVideoFrame(unsigned source,unsigned registeredCount,unsigned character=0) {
    return source<registeredCount?static_cast<int>(cat::video_actions::WalkSourceFrame(character,source)):-1;
}

struct Frame {
    cat::animation::Pose pose;
    double x=0, velocity=0, phase=0;
    unsigned source=0;
    Motion requested=Motion::Idle, displayed=Motion::Idle;
    bool waiting=false;
};
struct Row {
    cat::Character character=cat::Character::Cream;
    cat::nutrition::Visual body;
    std::vector<Frame> frames;
    double startX=0, peakSpeed=0;
    unsigned sourceChanges=0;
    bool stopped=false, reversed=false;
};

int Simulate(Row& row,std::string& context) {
    native_manual_motion_tests::Host host;
    host.Reset(row.character,true);
    g.centerX=g.work.left+600*g.dpiScale;ResetMovement();
    for(unsigned i=0;i<12;++i)g.physics.Update(1./60);
    g.workInterference=false;g.cursorPlay=false;
    const auto saved=host.Saved();
    const auto start=host.now;
    row.startX=g.physics.RenderPose().x;
    row.body=cat::appearance::VisualModifiers(g.nutrition,g.preferences.character);
    row.frames.reserve(Count);
    const auto* walk=LoadedCareSprite(Clip::Walk,row.character);
    if(!walk || !walk->layout->program.loop.count || walk->layout->nominalFramesPerSecond!=24 ||
            std::string(walk->layout->sourcePath).find("assets/video-actions/")!=0) {
        context="capture requires the registered source-video walk";return 542;
    }
    std::vector<bool> visited(walk->layout->program.loop.count,false);
    unsigned distinct=0, previous=0;
    bool previousWalk=false;
    for(unsigned index=0;index<Count;++index) {
        context="native video walk character="+std::to_string(static_cast<unsigned>(row.character))+
            " frame="+std::to_string(index);
        const auto now=start+static_cast<ULONGLONG>(index)*1000/Fps;
        if(index)host.Frame(now-host.now);
        if(index==30 || index==1260) {
            host.Command(kManualWalkCommand);
            if(index==30)g.nextDecision=host.now+16000; // Cover the complete six-second source twice.
        }
        const auto physical=g.physics.RenderPose();
        if(!physical.grounded || !std::isfinite(physical.x) || !std::isfinite(physical.velocityX))return 542;
        const auto displayed=cat::motion::DisplayedMotion(g.motion,g.walkingMotion,
            physical.grounded,g.waitingForStop,WalkingVelocity(physical));
        const auto pose=PrimaryDisplayedPose(host.now,physical);
        const auto* asset=CareSpriteFor(pose,row.character);
        const unsigned source=asset?SpriteFrameFor(pose,*asset->layout):static_cast<unsigned>(pose.frame);
        const double velocity=WalkingVelocity(physical);
        if(g.motion==Motion::Walk && std::abs(velocity)>2 && pose.spriteClip!=Clip::Walk) {
            context+=" requested Walk selected clip="+std::to_string(static_cast<unsigned>(pose.spriteClip));return 543;
        }
        if(pose.spriteClip==Clip::Walk) {
            if(source>=walk->layout->frameCount)return 544;
            if(source<visited.size() && !visited[source]){visited[source]=true;++distinct;}
            if(source<visited.size() && previousWalk && source!=previous)++row.sourceChanges;
            previous=source;
        }
        previousWalk=pose.spriteClip==Clip::Walk && source<visited.size();
        row.peakSpeed=std::max(row.peakSpeed,std::abs(velocity));
        if(index>=1140 && index<1260 && std::abs(velocity)<.5)row.stopped=true;
        if(index>1280 && velocity<-5)row.reversed=true;
        row.frames.push_back({pose,physical.x,velocity,g.gait.AuthoredPhase(),source,g.motion,displayed,g.waitingForStop});
    }
    if(host.Saved()!=saved){context="capture changed an owned in-memory save fixture";return 548;}
    const auto expectedSpeed=cat::sprite_assets::NominalLocomotionSpeed(*walk->layout);
    if(distinct!=visited.size() || expectedSpeed<=0 || row.peakSpeed<expectedSpeed*1.03 || row.peakSpeed>expectedSpeed*1.28 ||
            !row.stopped || !row.reversed || row.sourceChanges<visited.size()*2) {
        context="native walk coverage/speed/rest/reversal: visited="+std::to_string(distinct)+
            " peak="+std::to_string(row.peakSpeed)+" changes="+std::to_string(row.sourceChanges)+
            " stopped="+std::to_string(row.stopped)+" reversed="+std::to_string(row.reversed);return 545;
    }
    return 0;
}

int Run(std::string& context) {
    if(!LoadAtlas()){context="capture atlas load";return 540;}
    for(const auto path:{L"render-self-test",L"render-self-test/walk-video"})
        if(!CreateDirectoryW(path,nullptr)&&GetLastError()!=ERROR_ALREADY_EXISTS)return 541;
    std::array<Row,2> rows{};rows[0].character=cat::Character::Cream;rows[1].character=cat::Character::Tuxedo;
    for(auto& row:rows)if(const int result=Simulate(row,context))return result;
    Surface canvas;
    if(!canvas.Create(Width,Height))return 541;
    std::ostringstream trace;trace.imbue(std::locale::classic());
    trace<<"{\"schema\":1,\"fps\":"<<Fps<<",\"frames\":[";
    for(unsigned index=0;index<Count;++index) {
        context="native GDI+ source-video capture frame="+std::to_string(index);
        {
        Graphics graphics(canvas.image.get());
        graphics.SetCompositingMode(CompositingModeSourceCopy);graphics.Clear(Color(255,246,244,239));
        graphics.SetCompositingMode(CompositingModeSourceOver);
        graphics.SetCompositingQuality(CompositingQualityHighQuality);
        graphics.SetInterpolationMode(InterpolationModeHighQualityBicubic);
        graphics.SetSmoothingMode(SmoothingModeAntiAlias);graphics.SetPixelOffsetMode(PixelOffsetModeHighQuality);
        graphics.SetTextRenderingHint(TextRenderingHintAntiAliasGridFit);
        Font title(L"Segoe UI",20,FontStyleRegular,UnitPixel),small(L"Segoe UI",12,FontStyleRegular,UnitPixel);
        SolidBrush ink(Color(255,41,53,60));
        graphics.DrawString(L"Source-video walk / actual Windows renderer",-1,&title,PointF(22,12),&ink);
        graphics.DrawString(L"60 FPS GDI+ + Box2D; 110 px characters. Offscreen native capture, not a desktop recording.",-1,
            &small,PointF(23,42),&ink);
        const wchar_t* phase=index<30?L"Rest":index<990?L"Walk":index<1260?L"Brake / rest":L"Turn / walk";
        graphics.DrawString(phase,-1,&small,PointF(800,18),&ink);
        if(index)trace<<',';
        trace<<"{\"frame\":"<<index<<",\"elapsed_ms\":"<<static_cast<unsigned long long>(index)*1000/Fps<<",\"characters\":[";
        for(unsigned rowIndex=0;rowIndex<rows.size();++rowIndex) {
            const auto& row=rows[rowIndex];const auto& frame=row.frames[index];
            const REAL floor=rowIndex?460.0f:245.0f;
            const REAL center=static_cast<REAL>(140+frame.x-row.startX);
            Pen ground(Color(255,185,196,197),1);
            graphics.DrawLine(&ground,25.0f,floor+1.0f,static_cast<REAL>(Width-25),floor+1.0f);
            graphics.DrawString(rowIndex?L"Tuxedo":L"Cream",-1,&small,PointF(25,floor-152),&ink);
            DrawPetStyle(graphics,row.character,cat::Outfit::None,frame.pose,row.body,center,floor,110);
            if(rowIndex)trace<<',';
            trace<<"{\"character\":"<<static_cast<unsigned>(row.character)<<",\"root_x\":"<<frame.x
                <<",\"velocity_x\":"<<frame.velocity<<",\"phase\":"<<frame.phase
                <<",\"requested_motion\":"<<static_cast<unsigned>(frame.requested)
                <<",\"displayed_motion\":"<<static_cast<unsigned>(frame.displayed)
                <<",\"sprite_clip\":"<<static_cast<unsigned>(frame.pose.spriteClip)
                <<",\"sprite_frame\":"<<frame.source<<",\"input_video_frame\":";
            const auto* walk=LoadedCareSprite(Clip::Walk,row.character);
            const int videoFrame=frame.pose.spriteClip==Clip::Walk && walk?
                SourceVideoFrame(frame.source,walk->layout->frameCount,static_cast<unsigned>(row.character)):-1;
            if(videoFrame>=0)trace<<videoFrame;else trace<<"null";
            trace<<",\"input_video_file\":";
            if(videoFrame>=0)trace<<'"'<<cat::video_actions::WalkSourceFile(static_cast<unsigned>(row.character),frame.source)<<'"';else trace<<"null";
            trace<<",\"facing\":"<<(frame.pose.flip?-1:1)
                <<",\"waiting_for_stop\":"<<(frame.waiting?"true":"false")<<'}';
        }
        trace<<"]}";
        graphics.Flush(FlushIntentionSync);
        if(graphics.GetLastStatus()!=Ok)return 546;
        }
        const auto path=L"render-self-test/walk-video/frame-"+
            std::wstring(5-std::to_wstring(index).size(),L'0')+std::to_wstring(index)+L".png";
        if(!SaveNativeWalkingReview(*canvas.image,path.c_str()))return 546;
    }
    trace<<"]}\n";
    std::ostringstream metadata;metadata.imbue(std::locale::classic());
    const auto* walk=LoadedCareSprite(Clip::Walk,cat::Character::Cream);
    if(!walk)return 542;
    const auto cycleFrames=walk->layout->program.loop.count;
    metadata<<"{\"schema\":1,\"render_source\":\"Windows GDI+ / Box2D\",\"desktop_recording\":false,\"fps\":"<<Fps
        <<",\"frame_count\":"<<Count<<",\"canvas\":["<<Width<<','<<Height
        <<"],\"reference_height_px\":110,\"source_video_frames\":"<<cycleFrames<<",\"source_video_fps\":24,\"source_video_frame_indices\":[";
    for(unsigned index=0;index<cycleFrames;++index){if(index)metadata<<',';metadata<<cat::video_actions::WalkSourceFrame(0,index);}
    metadata<<"],\"cycle_frames\":"<<cycleFrames<<",\"registered_walk_frames\":"<<walk->layout->frameCount
        <<",\"registered_walk_source_indices\":[";
    for(unsigned index=0;index<walk->layout->frameCount;++index) {
        if(index)metadata<<',';
        const int source=SourceVideoFrame(index,walk->layout->frameCount);
        if(source>=0)metadata<<source;else metadata<<"null";
    }
    metadata<<"],\"characters\":[";
    for(unsigned index=0;index<rows.size();++index) {
        if(index)metadata<<',';
        const auto& row=rows[index];
        metadata<<"{\"character\":"<<static_cast<unsigned>(row.character)<<",\"peak_walk_speed\":"<<row.peakSpeed
            <<",\"source_frame_changes\":"<<row.sourceChanges<<",\"all_source_frames_used\":true,\"stopped\":true,\"reversed\":true}";
    }
    metadata<<"],\"trace\":\"trace.json\",\"scenario\":\"rest, manual walk, brake and rest, reversed manual walk\"}\n";
    if(!WriteBytes(L"render-self-test/walk-video/trace.json",trace.str()) ||
            !WriteBytes(L"render-self-test/walk-video/capture.json",metadata.str()))return 547;
    context="source-video walk selected for both coats; actual motion, rest and reverse captured at 60 FPS";
    return 0;
}
} // namespace native_video_walk_capture

int NativeVideoWalkCapture(HINSTANCE instance) {
    g.instance=instance;
    const HRESULT com=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
    if(FAILED(com))return 540;
    ULONG_PTR token=0;GdiplusStartupInput startup;
    if(GdiplusStartup(&token,&startup,nullptr)!=Ok){CoUninitialize();return 540;}
    int result=0;std::string context="capture initialization";
    try {result=native_video_walk_capture::Run(context);}catch(...){result=549;}
    WriteBytes(L"render-self-test/walk-video-report.txt","exit="+std::to_string(result)+" "+context+"\n");
    ReleaseAtlases();g.styleBlendFrom.Reset();g.styleBlendTo.Reset();
    g.initialized=false;g.manualMotionActive=false;g.window=nullptr;cursorInteraction={};
    GdiplusShutdown(token);CoUninitialize();
    return result;
}
