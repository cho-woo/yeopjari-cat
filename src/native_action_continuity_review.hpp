#pragma once

// Complete dispatcher timelines, including entry and return, at 60 Hz. The
// actor is painted by the shipping renderer; the world root is recorded
// separately so a changed drawing cannot hide a physical teleport.
namespace native_action_continuity {
int Run(std::string& context,bool detailed=false) {
    CreateDirectoryW(L"render-self-test",nullptr);
    CreateDirectoryW(L"render-self-test/action-continuity",nullptr);
    Surface image,previous;
    constexpr int W=260,H=260;
    if(!image.Create(W,H)||!previous.Create(W,H))return 930;
    std::ostringstream trace,report;
    trace.imbue(std::locale::classic());report.imbue(std::locale::classic());
    trace<<"coat,motion,frame,elapsed_ms,clip,source,legacy,x,lift,vx,vy,alpha_x,alpha_y,alpha_delta,root_step,serial\n";
    report<<"{\"schema\":1,\"version\":\"" YEOPJARI_VERSION_STRING "\",\"fps\":60,\"cases\":[";
    bool comma=false;
    for(const auto coat:cat::kAvailableCharacters)for(unsigned motion=0;motion<35;++motion) {
        if(detailed&&motion!=static_cast<unsigned>(Motion::BellyRoll))continue;
        native_manual_motion_tests::Host host;host.Reset(coat,true);
        if(!host.artworkReady)return 931;
        g.cursorPlay=g.workInterference=false;g.random.seed(137);
        g.work={0,0,3000,900};g.dpiScale=1;g.centerX=1500;ResetMovement();
        for(unsigned i=0;i<12;++i)g.physics.Update(1./60);
        const auto saved=host.Saved();const auto start=host.now;
        CarePresentationRenderer presentation;
        double peakImage=0,peakRoot=0,peakAlpha=0,lastX=0,lastY=0;
        unsigned worst=0,teleports=0,frames=0;
        cat::physics::Pose last{};bool had=false,returned=false;
        ULONGLONG end=0;bool stopRequested=false;
        const auto wantedStop=motion==32?cat::sprite_sequence::Clip::Brake:
            motion==33?cat::sprite_sequence::Clip::Slide:cat::sprite_sequence::Clip::Stumble;
        for(unsigned index=0;index<1100;++index) {
            if(index==24) {
                const auto selected=motion>=32?Motion::Zoomies:static_cast<Motion>(motion);
                if(selected==Motion::Walk||selected==Motion::Zoomies) {
                    host.Command(selected==Motion::Walk?kManualWalkCommand:kManualRunCommand);
                    g.nextDecision=host.now+(motion>=32?20000:8000);
                } else {SetMotion(selected,8000,host.now);host.Rebase();}
                end=g.nextDecision;
            }
            if(motion>=32&&index==144) {
                unsigned seed=0;
                for(;;++seed) {
                    auto random=g.random;random.seed(seed);(void)random();
                    if(cat::lively::StopClip(random())==wantedStop)break;
                    if(seed>1000)return 937;
                }
                g.random.seed(seed);g.manualMotionActive=false;
                BeginAutonomousRest(host.now);host.Rebase();
                if(g.stopClip!=wantedStop)return 938;
                stopRequested=true;end=0;
            }
            if(stopRequested&&g.stopClip==cat::sprite_sequence::Clip::None&&!returned) {
                if(g.groundArtBridge.Starting()||g.groundArtBridge.Stopping()||
                   g.nextDecision<host.now+cat::behavior::Brain::NeutralRestMinMs-20)return 939;
                returned=true;end=host.now;
            }
            if(end&&host.now>end+900&&!returned&&g.physics.RenderPose().grounded&&
                    (!g.wakingFromSleep||g.motion!=Motion::Stretch||host.now>=g.nextDecision)&&
                    g.stopClip==cat::sprite_sequence::Clip::None&&!g.runningTurn.Active()&&
                    !g.groundArtBridge.Starting()&&!g.groundArtBridge.Stopping()&&!PrimaryAuthoredCareHolding(host.now)) {
                SetMotion(Motion::Idle,8000,host.now);host.Rebase();returned=true;end=host.now;
            }
            const auto physical=g.physics.RenderPose();
            const auto pose=PrimaryDisplayedPose(host.now,physical);
            const auto* asset=CareSpriteFor(pose,coat);
            const unsigned source=asset?SpriteFrameFor(pose,*asset->layout):static_cast<unsigned>(pose.frame);
            {
                Graphics graphics(image.image.get());graphics.SetCompositingMode(CompositingModeSourceCopy);
                graphics.Clear(Color(0,0,0,0));graphics.SetCompositingMode(CompositingModeSourceOver);
                graphics.SetInterpolationMode(InterpolationModeHighQualityBicubic);graphics.SetSmoothingMode(SmoothingModeAntiAlias);
                DrawPetStyle(graphics,coat,cat::Outfit::None,pose,
                    cat::appearance::VisualModifiers(g.nutrition,coat),130,210,110,&presentation,host.now);
                graphics.Flush(FlushIntentionSync);
            }
            double mass=0,cx=0,cy=0,delta=0,active=0;
            for(int y=0;y<H;++y)for(int x=0;x<W;++x) {
                const auto p=(y*W+x)*4;const double a=image.pixels[p+3];
                mass+=a;cx+=x*a;cy+=y*a;
                if(had&&std::max(image.pixels[p+3],previous.pixels[p+3])>16) {
                    for(unsigned c=0;c<4;++c)delta+=std::abs(int(image.pixels[p+c])-previous.pixels[p+c]);
                    ++active;
                }
            }
            if(!mass||!std::isfinite(physical.x)||!std::isfinite(physical.lift))return 932;
            cx/=mass;cy/=mass;delta=active?delta/(active*4*255):0;
            if(detailed) {
                const auto path=L"render-self-test/action-continuity/c"+std::to_wstring(static_cast<unsigned>(coat))+
                    L"-f"+std::to_wstring(index)+L".png";
                if(!SaveNativeWalkingReview(*image.image,path.c_str()))return 933;
            }
            const double imageStep=had?std::hypot(cx-lastX,cy-lastY):0;
            const double rootStep=had?std::hypot(physical.x-last.x,physical.lift-last.lift):0;
            if(had&&(std::abs(physical.x-last.x)>std::max(std::abs(physical.velocityX),std::abs(last.velocityX))/60+2||
                     std::abs(physical.lift-last.lift)>std::max(std::abs(physical.velocityY),std::abs(last.velocityY))/60+2))++teleports;
            peakRoot=std::max(peakRoot,rootStep);peakAlpha=std::max(peakAlpha,delta);
            if(imageStep>peakImage) {
                peakImage=imageStep;worst=index;
                const auto stem=L"render-self-test/action-continuity/c"+std::to_wstring(static_cast<unsigned>(coat))+L"-m"+std::to_wstring(motion);
                if(had&&!SaveNativeWalkingReview(*previous.image,(stem+L"-before.png").c_str()))return 933;
                if(!SaveNativeWalkingReview(*image.image,(stem+L"-after.png").c_str()))return 933;
            }
            trace<<static_cast<unsigned>(coat)<<','<<motion<<','<<index<<','<<host.now-start<<','<<static_cast<unsigned>(pose.spriteClip)<<','<<source<<','<<pose.frame<<','
                <<physical.x<<','<<physical.lift<<','<<physical.velocityX<<','<<physical.velocityY<<','<<cx<<','<<cy<<','<<delta<<','<<rootStep<<','<<g.motionSerial<<'\n';
            std::memcpy(previous.pixels,image.pixels,W*H*4);had=true;lastX=cx;lastY=cy;last=physical;++frames;
            if(returned&&host.now>end+700)break;
            const auto elapsed=index*1000ULL/60;host.Frame((index+1)*1000ULL/60-elapsed);
        }
        if(!returned)return 940;
        if(host.Saved()!=saved)return 934;
        if(comma)report<<',';comma=true;
        report<<"{\"coat\":"<<static_cast<unsigned>(coat)<<",\"motion\":"<<motion<<",\"frames\":"<<frames
            <<",\"max_image_center_step\":"<<peakImage<<",\"max_root_step\":"<<peakRoot<<",\"max_alpha_delta\":"<<peakAlpha
            <<",\"worst_frame\":"<<worst<<",\"physical_discontinuities\":"<<teleports<<'}';
        context="complete action continuity coat="+std::to_string(static_cast<unsigned>(coat))+" motion="+std::to_string(motion);
        if(teleports||peakImage>16) {
            WriteBytes(L"render-self-test/action-continuity/failure.txt",context+" root_discontinuities="+std::to_string(teleports)+
                " image_step="+std::to_string(peakImage)+" frame="+std::to_string(worst));
            return 936;
        }
    }
    report<<"],\"motion_states\":"<<(detailed?1:32)<<",\"extra_stop_variants\":"<<(detailed?0:3)<<",\"coats\":[0,8,10],\"profile_unchanged\":true,\"os_input_injected\":false}";
    return WriteBytes(L"render-self-test/action-continuity/trace.csv",trace.str())&&
        WriteBytes(L"render-self-test/action-continuity/report.json",report.str())?0:935;
}
}
int NativeActionContinuityReview(HINSTANCE instance,bool detailed=false) {
    g.instance=instance;ULONG_PTR token=0;GdiplusStartupInput startup;
    if(GdiplusStartup(&token,&startup,nullptr)!=Ok)return 930;
    const HRESULT com=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
    std::string context;int result=LoadAtlas()?native_action_continuity::Run(context,detailed):931;
    WriteBytes(L"render-self-test/action-continuity-result.txt","exit="+std::to_string(result)+" "+context);
    ReleaseAtlases();ReleaseCarePresentation();g.styleBlendFrom.Reset();g.styleBlendTo.Reset();
    g.surface.Reset();g.blendFrom.Reset();g.blendTo.Reset();g.toySurface.Reset();
    if(g.preciseTimer){timeEndPeriod(1);g.preciseTimer=false;}
    g.initialized=false;g.window=nullptr;cursorInteraction={};GdiplusShutdown(token);if(SUCCEEDED(com))CoUninitialize();return result;
}
