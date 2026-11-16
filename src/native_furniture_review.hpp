#pragma once
// Synthetic input/time, shipping owner/motor/Box2D/pose/movie/GDI+ paths.
// Does not load a profile, move the OS pointer or replace the user's running pet.
namespace native_furniture_review {
int Run(unsigned coat,bool companion,unsigned kind,unsigned scenario,bool film) {
    const auto character=static_cast<cat::Character>(coat);
    const auto action=static_cast<cat::furniture::Kind>(kind);
    if(!cat::furniture::Find(action,coat))return 970;
    native_manual_motion_tests::Host host;
    g.furniture.Cancel();ReleaseFurnitureMovie(false);ReleaseFurnitureMovie(true);
    PrimaryCarePresentation().Reset();CompanionCarePresentation().Reset();
    native_ball_play_capture::Configure(host,character,scenario?850:350);
    g.work={0,0,960,720};g.centerX=(scenario==1||scenario==2)?850:350;ResetMovement();
    for(unsigned i=0;i<12;++i)g.physics.Update(1./60);
    BeginAutonomousRest(host.now);g.bubbleUntil=0;
    if(scenario==2){g.pet.bornUtc=UtcNow()-30*86400;g.dpiScale=1.5;g.work={-1440,-300,0,780};g.centerX=-1440+850*1.5;ResetMovement();}
    cat::companion::State state=cat::companion::NewState(UtcNow()-366*86400);
    state.preferences.character=character;state.pet.position=(scenario==1||scenario==2)?(850.-96)/768:(350.-96)/768;
    if(scenario==2)state.pet.bornUtc=UtcNow()-30*86400;
    cat::companion::Model model;model.Bind(state);model.Reset(96,864,700);
    const auto saved=host.Saved();
    if(companion&&scenario!=3){if(!model.RequestFurniture(action))return 971;}
    else if(scenario!=3)RunCommand(kFurnitureFirst+kind-1,host.now);
    const auto begin=GetTickCount64();
    while(!PrepareFurnitureMovie(companion,action,coat)) {
        if(FurnitureMovieFailed(companion)||GetTickCount64()-begin>30000)return 972;
        Sleep(2);
    }
    auto& player=(companion?CompanionCarePresentation():PrimaryCarePresentation()).furnitureMovie;
    const auto* movie=player.Movie();const unsigned decoded=movie?static_cast<unsigned>(movie->frames.size()):0;
    const auto bytes=movie?movie->Bytes():0;
    const std::wstring stem=L"render-self-test/furniture/"+std::wstring(companion?L"companion":L"primary")+
        L"-c"+std::to_wstring(coat)+L"-k"+std::to_wstring(kind)+L"-s"+std::to_wstring(scenario);
    CreateDirectoryW(L"render-self-test",nullptr);CreateDirectoryW(L"render-self-test/furniture",nullptr);CreateDirectoryW(stem.c_str(),nullptr);
    Surface scene,actor;scene.Create(960,420);
    const double dpi=scenario==2?1.5:1.;actor.Create(int(640*dpi),int(440*dpi));
    std::ostringstream trace;trace.imbue(std::locale::classic());
    trace<<"frame,ms,phase,source_frame,root_x,root_lift,vx,scene_ms,drawn,paused\n";
    std::vector<bool> seen(decoded,false);bool played=false,complete=false;unsigned completedAt=0,playFrames=0,warps=0;
    double root=0,lastX=0,maxMovieDrift=0,maxStep=0;unsigned missing=0;
    bool requested=scenario!=3,drained=false,early=false;std::uint64_t earliest=0;
    if(scenario==3)g.cursorPlay=true;
    for(unsigned i=0;i<2400;++i) {
        const auto step=(i+1)*1000/60-i*1000/60;host.now+=step;const double dt=step/1000.;
        const bool paused=scenario==2&&i>=300&&i<330;
        cat::animation::Pose pose;cat::physics::Pose body;cat::nutrition::Visual visual;
        double growth=1;cat::companion::Snapshot snapshot;
        if(companion) {
            cat::companion::Context context;context.ticks=host.now;context.elapsedSeconds=dt;
            context.supportsFurniture=true;context.nowUtc=UtcNow();context.primaryVisible=false;context.furnitureReady=true;context.paused=paused;
            if(scenario==3){context.cursorEnabled=context.cursorAvailable=true;
                context.cursorX=model.Physical().x+25;context.cursorWorldHeight=model.Physical().lift+12;}
            model.Tick(context);snapshot=model.View(host.now);pose=snapshot.animation;body=snapshot.physical;
            visual=snapshot.body;growth=snapshot.growthScale;
        } else {
            if(paused)FreezePrimaryMotion(host.now,16);
            if(requested)UpdatePrimaryFurniture(host.now,paused?0:dt);
            if(scenario==3&&!paused) {
                const auto at=g.physics.CurrentPose();
                POINT mouse{LONG(g.work.left+(at.x+25)*g.dpiScale),LONG(g.work.bottom-(at.lift+12)*g.dpiScale)};
                UpdateCursorReactionSample(host.now,step,mouse,true);
            }
            if(!paused) {
                if(!g.furniture.Owned())DispatchAutonomousMotion(host.now);
                AdvancePrimaryMotion(host.now,dt);
            }
            body=g.physics.RenderPose();pose=PrimaryDisplayedPose(host.now,body);
            visual=cat::appearance::VisualModifiers(g.nutrition,character);growth=cat::GrowthScale(g.pet.bornUtc,UtcNow());
        }
        if(scenario==3&&!requested&&pose.spriteTimed&&pose.spriteElapsedMs>=200&&
                (pose.spriteClip==cat::sprite_sequence::Clip::Punch||pose.spriteClip==cat::sprite_sequence::Clip::Reach)) {
            const auto* clip=CareSpriteFor(pose,character);if(!clip)return 976;
            earliest=i*1000/60+cat::sprite_sequence::ProgramDuration(clip->layout->program)-pose.spriteElapsedMs;
            if(companion)model.RequestFurniture(action);else RunCommand(kFurnitureFirst+kind-1,host.now);
            requested=drained=true;
        }
        const auto& session=companion?model.furniture:g.furniture;
        const bool playing=pose.furnitureScene!=cat::furniture::Kind::None;
        unsigned source=0;
        if(playing) {
            const auto* frame=player.Movie()?player.Movie()->frames.data():nullptr;
            if(!frame)return 973;
            const auto time=pose.furnitureElapsedMs*10000;
            const auto* data=player.Movie();
            while(source+1<data->frames.size()&&data->frames[source+1].time100ns<=time)++source;
            seen[source]=true;++playFrames;
            if(!played){played=true;root=body.x;if(scenario==3&&i*1000/60+32<earliest)early=true;}
            maxMovieDrift=std::max(maxMovieDrift,std::abs(body.x-root));
        }
        if(i) {const auto step=std::abs(body.x-lastX);maxStep=std::max(maxStep,step);if(step>std::abs(body.velocityX)*dt+3)++warps;}
        lastX=body.x;
        if(played&&!session.Requested()&&!complete){complete=true;completedAt=i;}
        {
            Graphics out(scene.image.get());out.SetCompositingMode(CompositingModeSourceCopy);out.Clear(Color(255,240,237,246));
            out.SetCompositingMode(CompositingModeSourceOver);out.SetInterpolationMode(InterpolationModeHighQualityBicubic);
            Pen line(Color(255,150,141,168),1);out.DrawLine(&line,0.f,385.f,960.f,385.f);
            if(companion) {
                PaintCompanion(nullptr,actor.pixels,actor.width,actor.height,snapshot,dpi);
                out.DrawImage(actor.image.get(),RectF(REAL(body.x-320),REAL(385-body.lift-440),640,440),0,0,actor.width,actor.height,UnitPixel);
            } else DrawPetStyle(out,character,cat::Outfit::None,pose,visual,REAL(body.x),REAL(385-body.lift),REAL(110*growth),
                &PrimaryCarePresentation(),host.now);
            Font font(L"Segoe UI",14,FontStyleRegular,UnitPixel);SolidBrush ink(Color(255,42,34,56));
            const auto label=L"v"+std::wstring(YEOPJARI_VERSION_WSTRING)+L" / "+(companion?L"companion":L"primary")+
                L" c"+std::to_wstring(coat)+L" / furniture "+std::to_wstring(kind)+L" / "+std::to_wstring(i*16/1000)+L"s";
            out.DrawString(label.c_str(),-1,&font,PointF(12,12),&ink);out.Flush(FlushIntentionSync);
            if((film&&i%2==0)||(!film&&(i%180==0))) {
                wchar_t name[80]{};swprintf(name,80,L"/frame-%05u.png",i/2);
                if(!SaveNativeWalkingReview(*scene.image,(stem+name).c_str()))return 974;
            }
        }
        trace<<i<<','<<i*1000/60<<','<<static_cast<unsigned>(session.State())<<','<<source<<','<<body.x<<','<<body.lift<<','<<body.velocityX<<','<<pose.furnitureElapsedMs<<','<<playing<<','<<paused<<'\n';
        if(complete&&i>completedAt+90)break;
    }
    for(bool observed:seen)if(!observed)++missing;
    const bool unchanged=host.Saved()==saved;
    const bool passed=played&&complete&&missing==0&&warps==0&&maxMovieDrift<.05&&unchanged&&(scenario!=3||(drained&&!early));
    std::ostringstream report;report<<"{\"passed\":"<<passed<<",\"version\":\"" YEOPJARI_VERSION_STRING "\",\"coat\":"<<coat<<",\"companion\":"<<companion
        <<",\"kind\":"<<kind<<",\"scenario\":"<<scenario<<",\"source_frames\":"<<decoded<<",\"unseen_source_frames\":"<<missing
        <<",\"drain_request_in_paw\":"<<drained<<",\"started_before_paw_end\":"<<early<<",\"movie_bytes\":"<<bytes<<",\"complete\":"<<complete<<",\"play_frames\":"<<playFrames<<",\"warps\":"<<warps<<",\"max_step\":"<<maxStep
        <<",\"movie_root_drift\":"<<maxMovieDrift<<",\"profile_unchanged\":"<<unchanged<<",\"desktop_recording\":false,\"shipping_renderer\":true}";
    WriteBytes((stem+L"/trace.csv").c_str(),trace.str());WriteBytes((stem+L"/report.json").c_str(),report.str());
    ReleaseFurnitureMovie(companion);return passed?0:975;
}
}
int NativeFurnitureReview(HINSTANCE instance,unsigned coat,bool companion,unsigned kind,unsigned scenario,bool film) {
    g.instance=instance;ULONG_PTR token=0;GdiplusStartupInput input;
    if(GdiplusStartup(&token,&input,nullptr)!=Ok)return 970;
    const auto com=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
    const int result=LoadAtlas()?native_furniture_review::Run(coat,companion,kind,scenario,film):970;
    ReleaseAtlases();ReleaseCarePresentation();g.styleBlendFrom.Reset();g.styleBlendTo.Reset();
    GdiplusShutdown(token);if(SUCCEEDED(com))CoUninitialize();return result;
}
