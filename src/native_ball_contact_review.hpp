#pragma once
namespace cat::companion {
struct BallReviewAccess {
    static const cat::toy::PhysicsBall& Ball(const Model& m){return m.ball_;}
    static std::uint64_t Contact(const Model& m){return m.ballKickSerial_;}
    static unsigned Phase(const Model& m){return static_cast<unsigned>(m.ballSession_.phase);}
    static const cat::toy::Session& Session(const Model& m){return m.ballSession_;}
    static unsigned Bridge(const Model& m){return static_cast<unsigned>(m.groundArtBridge_.CurrentPhase());}
    static void Seed(Model& m,double x,double vx=0){m.ball_.Reset(x,0,vx);}
    static cat::physics::Controller& Physics(Model& m){return m.physics_;}
};
}
namespace native_ball_contact_review {
// Exercise the actual layered-window host, not just the scene compositor.
// An off-screen work area keeps this contract independent of the user's apps.
int LayerContract() {
    const auto work=g.work;const double dpi=g.dpiScale;const bool hidden=g.hidden;g.hidden=false;
    int result=0;
    for(double scale:{1.,1.5}) {
        g.dpiScale=scale;g.work={-4000,-4000,-2400,-3000};
        cat::companion::Snapshot s;s.enabled=s.ballVisible=true;s.ball.x=400;s.ball.lift=17;
        RenderCompanionBall(s);RECT a{},b{};
        if(!g.companionBallWindow||!GetWindowRect(g.companionBallWindow,&a)){result=957;break;}
        const auto& surface=g.companionBallSurface;
        const int expectedX=int(std::lround(g.work.left+400*scale-surface.width/2.));
        if(a.left!=expectedX||a.top!=g.work.bottom-surface.height){result=957;break;}
        s.physical.x=850;s.physical.lift=100;RenderCompanionBall(s);GetWindowRect(g.companionBallWindow,&b);
        if(!EqualRect(&a,&b)){result=957;break;}
        double x=0,y=0,count=0;
        for(int yy=0;yy<surface.height;++yy)for(int xx=0;xx<surface.width;++xx) {
            const auto* pixel=surface.pixels+(yy*surface.width+xx)*4;
            if(pixel[2]>200&&pixel[2]>pixel[1]*1.4){x+=xx;y+=yy;++count;}
        }
        if(count<100||std::abs(x/count-surface.width/2.)>1||
                std::abs(y/count-(surface.height-1-29*scale))>1.5){result=957;break;}
        const int floorIndex=((surface.height-int(3*scale))*surface.width+surface.width/2)*4;
        if(surface.pixels[floorIndex+3]==0){result=957;break;}
        s.ballVisible=false;RenderCompanionBall(s);
        if(IsWindowVisible(g.companionBallWindow)){result=957;break;}
    }
    if(g.companionBallWindow)DestroyWindow(g.companionBallWindow);
    g.companionBallWindow=nullptr;g.companionBallSurface.Reset();g.work=work;g.dpiScale=dpi;g.hidden=hidden;
    return result;
}
int Run(std::string& context,unsigned coat,bool companion,bool film,unsigned scenario=0) {
    using Clip=cat::sprite_sequence::Clip;
    const auto character=static_cast<cat::Character>(coat);
    if(!cat::CharacterAvailable(character)||!EnsureCareCoats(character))return 950;
    const std::wstring stem=L"render-self-test/ball-contact/"+std::wstring(companion?L"companion":L"primary")+L"-c"+std::to_wstring(coat)+(scenario?L"-s"+std::to_wstring(scenario):L"");
    CreateDirectoryW(L"render-self-test",nullptr);CreateDirectoryW(L"render-self-test/ball-contact",nullptr);CreateDirectoryW(stem.c_str(),nullptr);
    native_manual_motion_tests::Host host;native_ball_play_capture::Configure(host,character,350);
    g.work={0,0,960,720};g.centerX=scenario==4?850:350;
    if(scenario==6){g.dpiScale=1.5;g.work={-1440,-300,0,780};g.centerX=g.work.left+350*g.dpiScale;}
    ResetMovement();
    if(scenario==7)g.pet.bornUtc=UtcNow()-30*86400;
    const double origin=g.work.left/g.dpiScale;
    for(unsigned i=0;i<12;++i)g.physics.Update(1./60);
    const auto saved=host.Saved();const auto start=host.now;
    cat::companion::State state=cat::companion::NewState(UtcNow()-366*86400);
    state.preferences.character=character;state.pet.position=scenario==4?(850.-96)/768:.33;
    if(scenario==7)state.pet.bornUtc=UtcNow()-30*86400;
    cat::companion::Model model;
    if(companion) {model.Bind(state);model.Reset(96,864,700);if(!model.Perform(Clip::BallPlay,start))return 951;}
    else {PlaceToy(ToyKind::Ball);host.Rebase();g.bubbleUntil=0;}

    auto& physics=companion?cat::companion::BallReviewAccess::Physics(model):g.physics;
    if(scenario>=1&&scenario<=4) {
        const double x=physics.CurrentPose().x;
        const double target=scenario==1?x+20:scenario==2?700:scenario==3?x+250:864;
        const double speed=scenario==3?-160:scenario==4?100:0;
        if(companion)cat::companion::BallReviewAccess::Seed(model,target,speed);
        else {g.toyBall.Reset(origin+target,0,speed);g.toyX=(origin+target)*g.dpiScale;}
    }
    if(scenario==5) {
        physics.SetPlatform(220,520,100);
        if(!physics.LaunchTo(350,100))return 956;
        for(unsigned step=0;step<300&&!physics.OnPlatform();++step)physics.Update(1./60);
        if(!physics.OnPlatform())return 956;
    }
    Surface scene,actor,prior;
    if(!scene.Create(960,300)||!actor.Create(280,235)||!prior.Create(960,300))return 952;
    CarePresentationRenderer presentation;
    for(unsigned attempt=0;attempt<1000;++attempt) {
        const bool prepared=PrepareBallMovie(companion,coat);
        const bool local=presentation.ballMovie.Prepare(g.instance,coat);
        if(prepared&&local)break;
        if(attempt==999)return 958;
        Sleep(10);
    }
    std::ostringstream trace;trace.imbue(std::locale::classic());
    trace<<"frame,ms,actor,coat,phase,clip,source,serial,root_x,root_lift,vx,ball_visible,ball_x,ball_lift,ball_vx,ball_vy,paw_x,paw_height,contact,contact_distance,cat_step,ball_step,alpha_delta,pounce,action_ms,bridge\n";
    unsigned hits=0,visibleFrames=0,missFrames=0,warps=0,sourceChanges=0,invalidContacts=0,airHits=0,strokes=0,recoveries=0;
    bool priorStroke=false,priorPounce=false,strokeConsumed=false;unsigned previousBallSource=0,priorContactStroke=0;
    double lastCat=0,lastBall=0,maxCatStep=0,maxBallStep=0,maxGap=0;
    std::uint64_t previousConsumed=companion?cat::companion::BallReviewAccess::Contact(model):g.ballKickSerial;bool had=false;unsigned lastSource=0;
    for(unsigned index=0;index<3600;++index) {
        const auto elapsed=index*1000ULL/60,now=start+elapsed;
        const auto dt=index?(elapsed-(index-1)*1000ULL/60)/1000.:0;
        host.now=now;
        if(scenario==5&&index==240)physics.RemovePlatform();
        cat::animation::Pose pose;cat::physics::Pose body;cat::toy::BallPose ball;
        cat::nutrition::Visual visual;double growth=1;bool visible=false,contact=false;unsigned phase=0;
        std::uint64_t serial=0;
        cat::companion::Snapshot snapshot;
        if(companion) {
            cat::companion::Context c;c.ticks=now;c.nowUtc=UtcNow();c.elapsedSeconds=dt;c.primaryVisible=false;
            c.cursorEnabled=c.cursorAvailable=false;c.supportsBallMovie=c.ballMovieReady=true;model.Tick(c);snapshot=model.View(now);
            pose=snapshot.animation;body=snapshot.physical;visual=snapshot.body;growth=snapshot.growthScale;
            visible=snapshot.ballVisible;ball=cat::companion::BallReviewAccess::Ball(model).RenderPose();
            const auto consumed=cat::companion::BallReviewAccess::Contact(model);
            contact=consumed>previousConsumed;previousConsumed=consumed;serial=pose.presentationSerial;phase=cat::companion::BallReviewAccess::Phase(model);
        } else {
            if(index){AdvanceToyObjects(dt);FinishManualMotion(now);AdvanceRandomZoomies(now,dt);DispatchAutonomousMotion(now);AdvancePrimaryMotion(now,dt);}
            body=g.physics.RenderPose();pose=PrimaryDisplayedPose(now,body);visual=cat::appearance::VisualModifiers(g.nutrition,character);
            growth=cat::GrowthScale(g.pet.bornUtc,UtcNow());visible=g.toy==ToyKind::Ball;ball=g.toyBall.RenderPose();ball.x-=origin;
            const auto consumed=g.ballKickSerial;contact=consumed>previousConsumed;previousConsumed=consumed;
            phase=static_cast<unsigned>(g.toySession.phase);serial=g.motionSerial;
        }
        const auto* art=CareSpriteFor(pose,character);const unsigned source=pose.ballPounce?cat::ball_movie::Frame(pose.spriteElapsedMs):art?SpriteFrameFor(pose,*art->layout):0;
        const auto& session=companion?cat::companion::BallReviewAccess::Session(model):g.toySession;
        cat::deform::Point paw{};bool pawValid=pose.ballPounce||(art&&pose.spriteClip==Clip::BallPlay);
        if(pawValid) {paw=cat::toy::PlayPaw(session,coat,source);paw.x=body.x+(pose.flip?-1:1)*paw.x*growth*visual.bodyWidth;paw.y=body.lift+paw.y*growth*visual.bodyHeight;}
        const double distance=pawValid?std::hypot(ball.x-paw.x,ball.lift+12-paw.y):0;
        if(pawValid&&!priorStroke)++strokes;
        const auto contactStroke=cat::toy::PlayContactStroke(session,coat);
        if(pawValid&&(!priorStroke||contactStroke!=priorContactStroke))strokeConsumed=false;
        priorContactStroke=contactStroke;
        if(!pawValid&&priorStroke&&previousBallSource>=(priorPounce?123u:96u))++recoveries;
        if(pawValid){previousBallSource=source;priorPounce=pose.ballPounce;}priorStroke=pawValid;
        if(contact){strokeConsumed=true;++hits;maxGap=std::max(maxGap,distance);
            if(!pawValid||distance>cat::toy::PhysicsBall::Radius+4*growth+.25)++invalidContacts;
            if(!body.grounded||body.onPlatform)++airHits;
        }
        if(visible)++visibleFrames;
        if(pawValid&&visible&&source*1000/24>=cat::toy::PlayContactStart(session,coat)&&
            source*1000/24<cat::toy::PlayContactEnd(session,coat)&&distance>40*growth&&!strokeConsumed)++missFrames;
        const double catStep=had?std::abs(body.x-lastCat):0,ballStep=had?std::abs(ball.x-lastBall):0;
        maxCatStep=std::max(maxCatStep,catStep);maxBallStep=std::max(maxBallStep,ballStep);
        if(had&&(catStep>std::abs(body.velocityX)*dt+3||ballStep>std::abs(ball.velocity)*dt+4))++warps;
        if(had&&source!=lastSource)++sourceChanges;lastSource=source;
        double delta=0;
        {
            Graphics out(scene.image.get());out.SetCompositingMode(CompositingModeSourceCopy);out.Clear(Color(255,240,237,246));
            out.SetCompositingMode(CompositingModeSourceOver);out.SetInterpolationMode(InterpolationModeHighQualityBicubic);out.SetSmoothingMode(SmoothingModeAntiAlias);
            Pen floor(Color(255,120,111,138),1);out.DrawLine(&floor,0.f,260.f,960.f,260.f);
            if(companion) {
                PaintCompanion(nullptr,actor.pixels,280,235,snapshot,1);
                const int inset=CompanionSpriteBottomInset(snapshot,1);
                out.DrawImage(actor.image.get(),REAL(body.x-140),REAL(260-body.lift-235+inset));
#ifdef YEOPJARI_WORLD_BALL_WINDOW
                if(visible)DrawToyBall(out,REAL(ball.x),260.f,1,ball);
#endif
            } else {
                if(visible)DrawToyBall(out,REAL(ball.x),260.f,1,ball);
                DrawPetStyle(out,character,cat::Outfit::None,pose,visual,REAL(body.x+pose.x*growth),REAL(260-body.lift-pose.lift*growth),REAL(110*growth),&presentation,now);
            }
            Font font(L"Segoe UI",14,FontStyleRegular,UnitPixel);SolidBrush ink(Color(255,42,34,56));
            const std::wstring label=std::wstring(L"v" YEOPJARI_VERSION_WSTRING)+L" / "+(companion?L"companion":L"primary")+L" c"+std::to_wstring(coat)+L" / "+std::to_wstring(index/60)+L"s / shipping GDI+ renderer";
            out.DrawString(label.c_str(),-1,&font,PointF(12,12),&ink);out.Flush(FlushIntentionSync);
            if(had)for(int p=0;p<960*300*4;++p)delta+=std::abs(int(scene.pixels[p])-int(prior.pixels[p]));
            delta/=960*300*4*255.;std::memcpy(prior.pixels,scene.pixels,960*300*4);
            if((film&&index%2==0)||(!film&&(contact||index%600==0))) {wchar_t name[100]{};swprintf(name,100,L"/frame-%05u.png",index/2);if(!SaveNativeWalkingReview(*scene.image, (stem+name).c_str()))return 953;}
        }
        trace<<index<<','<<elapsed<<','<<(companion?1:0)<<','<<coat<<','<<phase<<','<<static_cast<unsigned>(pose.spriteClip)<<','<<source<<','<<serial<<','<<body.x<<','<<body.lift<<','<<body.velocityX<<','<<visible<<','<<ball.x<<','<<ball.lift<<','<<ball.velocity<<','<<ball.verticalVelocity<<','<<paw.x<<','<<paw.y<<','<<contact<<','<<distance<<','<<catStep<<','<<ballStep<<','<<delta<<','<<pose.ballPounce<<','<<session.elapsedMs<<','<<(companion?cat::companion::BallReviewAccess::Bridge(model):static_cast<unsigned>(g.groundArtBridge.CurrentPhase()))<<'\n';
        had=true;lastCat=body.x;lastBall=ball.x;
    }
    const bool unchanged=host.Saved()==saved;
    std::ostringstream report;report<<"{\"scenario\":"<<scenario<<",\"strokes\":"<<strokes<<",\"complete_recoveries\":"<<recoveries<<",\"invalid_contacts\":"<<invalidContacts<<",\"air_hits\":"<<airHits<<",\"version\":\"" YEOPJARI_VERSION_STRING "\",\"coat\":"<<coat<<",\"companion\":"<<companion<<",\"seconds\":60,\"simulation_hz\":60,\"film_fps\":30,\"fixed_camera\":true,\"desktop_recording\":false,\"hits\":"<<hits<<",\"visible_frames\":"<<visibleFrames<<",\"distant_contact_window_frames\":"<<missFrames<<",\"suspected_warps\":"<<warps<<",\"max_cat_step\":"<<maxCatStep<<",\"max_ball_step\":"<<maxBallStep<<",\"max_contact_distance\":"<<maxGap<<",\"source_changes\":"<<sourceChanges<<",\"profile_unchanged\":"<<unchanged<<"}";
    if(!WriteBytes((stem+L"/trace.csv").c_str(),trace.str())||!WriteBytes((stem+L"/report.json").c_str(),report.str()))return 954;
    context="ball contact review / "+std::to_string(coat)+(companion?" companion":" primary")+" scenario="+std::to_string(scenario);return unchanged&&hits>=3&&warps==0&&invalidContacts==0&&airHits==0&&missFrames==0&&recoveries>=3?0:955;
}
}
int NativeBallContactReview(HINSTANCE instance,unsigned coat,bool companion,bool film=true,unsigned scenario=0) {
    g.instance=instance;ULONG_PTR token=0;GdiplusStartupInput input;
    if(GdiplusStartup(&token,&input,nullptr)!=Ok)return 950;const auto com=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
    std::string context;int result=LoadAtlas()?native_ball_contact_review::Run(context,coat,companion,film,scenario):950;
    ReleaseAtlases();ReleaseCarePresentation();g.styleBlendFrom.Reset();g.styleBlendTo.Reset();
    GdiplusShutdown(token);if(SUCCEEDED(com))CoUninitialize();return result;
}

int NativeBallContactSuite(HINSTANCE instance) {
    g.instance=instance;ULONG_PTR token=0;GdiplusStartupInput input;
    if(GdiplusStartup(&token,&input,nullptr)!=Ok)return 950;
    const auto com=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
    std::string context;int result=LoadAtlas()?native_ball_contact_review::LayerContract():950;
    for(const auto character:cat::kAvailableCharacters)for(unsigned actor=0;actor<2&&!result;++actor)
        for(unsigned scenario=0;scenario<8&&!result;++scenario)
            result=native_ball_contact_review::Run(context,static_cast<unsigned>(character),actor!=0,false,scenario);
    WriteBytes(L"render-self-test/ball-contact-suite.txt","exit="+std::to_string(result)+" "+context+"\n");
    ReleaseAtlases();ReleaseCarePresentation();g.styleBlendFrom.Reset();g.styleBlendTo.Reset();
    GdiplusShutdown(token);if(SUCCEEDED(com))CoUninitialize();return result;
}
