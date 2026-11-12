#pragma once

// Native synthesis of the shipping toy/drive/Box2D/pose/GDI+ paths. The scene
// places one ball through PlaceToy, then supplies time only: no scripted kicks,
// post-placement ball warps, OS input or profile loading/saving are involved.
namespace native_ball_play_capture {
using Phase=cat::toy::Phase;
constexpr unsigned Fps=60,Count=3600;
constexpr int Width=900,Height=620;
struct Frame {
    cat::animation::Pose pose;
    cat::toy::BallPose ball,currentBall;
    double x=0,lift=0,vx=0,vy=0,pawX=0,pawHeight=0,kickDistance=0;
    std::uint64_t serial=0,actionElapsed=0,kicks=0;
    unsigned source=0,resource=0,revision=0;
    Phase phase=Phase::None;
    Motion motion=Motion::Idle;
    int facing=1,direction=1;
    bool grounded=true,waiting=false,braking=false,contact=false;
};
struct Row {
    cat::Character character=cat::Character::Cream;
    cat::nutrition::Visual body;
    std::vector<Frame> frames;
    CarePresentationRenderer presentation;
    unsigned contacts=0,pawEntries=0,walkFrames=0,brakingFrames=0,turns=0,ballBounces=0;
    double maxContactDistance=0,maxFrameRootDelta=0,ballTravel=0,catTravel=0,contactThreshold=0;
    bool sourceImmutable=false,profileUnchanged=false,duplicateContactRejected=true;
    std::uint64_t sourceHash=0;
};
const char* PhaseName(Phase phase) {
    switch(phase){case Phase::Discover:return "discover";case Phase::Approach:return "pursue";
    case Phase::Inspect:return "brake / inspect";case Phase::Interact:return "paw / bat";
    case Phase::Wait:return "watch / rest";default:return "transition";}
}
// At this edge a placed ball is initially too close for a stroke. The shipping
// chase must make room, stop and turn toward it before the first real contact.
void Configure(native_manual_motion_tests::Host& host,cat::Character character,double center=1184) {
    native_manual_motion_tests::keyboard_native::Clear();host.Reset(character,false);
    g.cursorPlay=false;g.workInterference=false;g.random.seed(5921);g.randomZoomiesRandom.seed(1831);
    g.work={0,0,1280,720};g.dpiScale=1;g.centerX=center;g.direction=g.facing=1;ResetMovement();
    for(unsigned step=0;step<12;++step)g.physics.Update(1./60);
    host.now=GetTickCount64();SetMotion(Motion::Idle,10000,host.now);host.Rebase();
}
int ContactGuards(std::string& context) {
    using cat::toy::BallPawContactAt;
    bool consumed=false;cat::toy::BallPose sample;sample.x=16.01;
    if(BallPawContactAt(400,consumed,0,12,sample,1,true)||consumed)return 797;
    sample.x=16;
    if(!BallPawContactAt(400,consumed,0,12,sample,1,true)||!consumed||
            BallPawContactAt(400,consumed,0,12,sample,1,true))return 797;
    consumed=false;if(BallPawContactAt(400,consumed,0,12,sample,1,false)||consumed)return 797;
    if(BallPawContactAt(cat::toy::kPawContactEndMs,consumed,0,12,sample,1,true)||!consumed)return 797;
    native_manual_motion_tests::Host host;Configure(host,cat::Character::Cream,400);
    const auto saved=host.Saved();
    g.toy=ToyKind::Ball;g.toySession.Start(g.toy);g.toySession.Set(Phase::Interact);
    g.toyEngaged=true;g.toyDispatching=true;SetMotion(Motion::ToyPaw,1050,host.now-400);g.toyDispatching=false;
    g.waitingForStop=false;g.toyContact=false;
    const auto physical=g.physics.CurrentPose();
    // The ball is close to the body but across from the reaching paw. The old
    // 55px body-proximity test would have applied an impulse here.
    g.toyBall.Reset(physical.x-20);
    const auto kicks=g.ballKickSerial;
    if(UpdateBallPlayContact(host.now,physical)||g.ballKickSerial!=kicks||g.toyBall.velocity!=0)return 798;
    if(!g.physics.Jump(55))return 798;g.physics.Update(.016);
    g.toyContact=false;
    if(UpdateBallPlayContact(host.now,g.physics.CurrentPose())||g.ballKickSerial!=kicks||g.toyBall.velocity!=0||host.Saved()!=saved)return 798;
    context="ball guards: exact circle threshold; once-only; late-window rejection; body-near/paw-far no kick; airborne no kick; no profile mutation";
    g.toy=ToyKind::None;g.toyEngaged=false;g.toySession.Cancel();
    return 0;
}
int Simulate(Row& row,std::string& context,bool edgeCase=true) {
    native_manual_motion_tests::Host host;Configure(host,row.character,edgeCase?1184:1000);
    const auto saved=host.Saved();row.body=cat::appearance::VisualModifiers(g.nutrition,row.character);
    row.contactThreshold=cat::toy::PhysicsBall::Radius+4*cat::GrowthScale(g.pet.bornUtc,UtcNow());
    const auto& source=CurrentArt(row.character).base.skins[4].source;
    row.sourceHash=cat::Checksum(std::string(reinterpret_cast<const char*>(source.data()),source.size()*4));
    PlaceToy(ToyKind::Ball);host.Rebase();g.bubbleUntil=0;
    const auto start=host.now;const auto baseKick=g.ballKickSerial;
    row.frames.reserve(Count);
    double previousRoot=g.physics.RenderPose().x,previousBall=g.toyBall.RenderPose().x;
    int previousFacing=g.facing,previousBallDirection=0;
    Phase priorPhase=Phase::None;
    std::uint64_t previousKick=baseKick;
    for(unsigned index=0;index<Count;++index) {
        const auto elapsed=static_cast<ULONGLONG>(index)*1000/Fps;
        const auto now=start+elapsed,step=now-host.now;host.now=now;
        context="ball play character="+std::to_string(static_cast<unsigned>(row.character))+" frame="+std::to_string(index);
        if(index) {
            AdvanceToyObjects(step/1000.);FinishManualMotion(now);
            AdvanceRandomZoomies(now,step/1000.);DispatchAutonomousMotion(now);
            AdvancePrimaryMotion(now,step/1000.);
        }
        const auto physical=g.physics.CurrentPose(),rendered=g.physics.RenderPose();
        const auto ball=g.toyBall.RenderPose(),currentBall=g.toyBall.CurrentPose();
        if(!std::isfinite(rendered.x)||!std::isfinite(rendered.lift)||!std::isfinite(ball.x)||
                !std::isfinite(ball.lift)||!std::isfinite(ball.velocity)||ball.lift<-.2)return 792;
        if(!rendered.grounded)return 792; // This ground toy never launches a cat.
        if(std::abs(rendered.velocityX)>2)++row.walkFrames;
        const bool braking=g.waitingForStop||g.groundArtBridge.Braking();
        if(braking)++row.brakingFrames;
        if(g.facing!=previousFacing)++row.turns;previousFacing=g.facing;
        const int ballDirection=ball.velocity>1?1:ball.velocity<-1?-1:0;
        if(ballDirection&&previousBallDirection&&ballDirection!=previousBallDirection)++row.ballBounces;
        if(ballDirection)previousBallDirection=ballDirection;
        if(g.toySession.phase!=priorPhase&&g.toySession.phase==Phase::Interact)++row.pawEntries;
        priorPhase=g.toySession.phase;
        const auto pose=PrimaryDisplayedPose(now,rendered);
        if(!cat::deform::Finite(pose.joints)||!std::isfinite(pose.x)||!std::isfinite(pose.lift))return 792;
        const auto* asset=CareSpriteFor(pose,row.character);
        const auto sourceFrame=asset?SpriteFrameFor(pose,*asset->layout):static_cast<unsigned>(pose.frame);
        if(asset&&sourceFrame>=asset->layout->frameCount)return 792;
        Frame frame;frame.pose=pose;frame.ball=ball;frame.currentBall=currentBall;
        frame.x=rendered.x;frame.lift=rendered.lift;frame.vx=rendered.velocityX;frame.vy=rendered.velocityY;
        frame.serial=g.motionSerial;frame.actionElapsed=now>=g.motionStart?now-g.motionStart:0;
        frame.kicks=g.ballKickSerial-baseKick;frame.contact=g.ballKickSerial>previousKick;
        const auto paw=BallToyPawWorldPoint(now,physical);frame.pawX=paw.x;frame.pawHeight=paw.y;
        frame.kickDistance=g.lastBallPawDistance;frame.source=sourceFrame;
        frame.resource=asset?asset->layout->resourceId:CurrentArt(row.character).base.baseResourceId;
        frame.phase=g.toySession.phase;frame.revision=g.toySession.revision;
        frame.motion=g.motion;frame.facing=g.facing;frame.direction=g.direction;
        frame.grounded=rendered.grounded;frame.waiting=g.waitingForStop;frame.braking=braking;
        if(frame.contact) {
            ++row.contacts;row.maxContactDistance=std::max(row.maxContactDistance,frame.kickDistance);
            const auto serial=g.ballKickSerial;
            if(UpdateBallPlayContact(now,physical)||g.ballKickSerial!=serial)row.duplicateContactRejected=false;
        }
        previousKick=g.ballKickSerial;
        row.maxFrameRootDelta=std::max(row.maxFrameRootDelta,std::abs(rendered.x-previousRoot));
        row.catTravel+=std::abs(rendered.x-previousRoot);row.ballTravel+=std::abs(ball.x-previousBall);
        previousRoot=rendered.x;previousBall=ball.x;row.frames.push_back(frame);
    }
    row.profileUnchanged=host.Saved()==saved;
    row.sourceImmutable=row.sourceHash==cat::Checksum(std::string(reinterpret_cast<const char*>(source.data()),source.size()*4));
    context+=" contacts="+std::to_string(row.contacts)+" entries="+std::to_string(row.pawEntries)+
        " walks="+std::to_string(row.walkFrames)+" brakes="+std::to_string(row.brakingFrames)+
        " turns="+std::to_string(row.turns)+" bounces="+std::to_string(row.ballBounces)+
        " balltravel="+std::to_string(row.ballTravel)+" cattravel="+std::to_string(row.catTravel)+
        " maxcontact="+std::to_string(row.maxContactDistance)+" phase="+std::to_string(static_cast<unsigned>(g.toySession.phase))+
        " elapsed="+std::to_string(g.toySession.elapsedMs)+" ball="+std::to_string(g.toyBall.x)+
        " root="+std::to_string(g.physics.CurrentPose().x)+" standoff="+std::to_string(BallPawStandOff())+
        " motion="+std::to_string(static_cast<unsigned>(g.motion))+" wait="+std::to_string(g.waitingForStop);
    // An interior placement must cover the full pursuit. At the constrained
    // edge require a real make-room step and two ball diameters of rolling;
    // that small scene deliberately exercises reversal rather than distance.
    const double minimumBallTravel=edgeCase?4*cat::toy::PhysicsBall::Radius:80;
    const double minimumCatTravel=edgeCase?cat::toy::PhysicsBall::Radius:40;
    if(row.contacts<3||row.pawEntries<3||row.walkFrames<(edgeCase?30U:60U)||row.ballTravel<minimumBallTravel||row.catTravel<minimumCatTravel||
            (edgeCase&&!row.turns)||!row.ballBounces||!row.brakingFrames||!row.profileUnchanged||!row.sourceImmutable||
            !row.duplicateContactRejected||row.contacts>row.pawEntries||row.maxFrameRootDelta>4)return 793;
    return 0;
}
int Run(std::string& context) {
    if(!LoadAtlas()||!EnsureCareCoats(cat::Character::Cream,cat::Character::Tuxedo))return 790;
    for(const auto path:{L"render-self-test",L"render-self-test/ball-play"})
        if(!CreateDirectoryW(path,nullptr)&&GetLastError()!=ERROR_ALREADY_EXISTS)return 791;
    if(const int guard=ContactGuards(context))return guard;
    std::array<Row,2> rows{};rows[1].character=cat::Character::Tuxedo;
    for(auto& row:rows) {
        Row interior;interior.character=row.character;
        if(const int result=Simulate(interior,context,false))return result;
        if(const int result=Simulate(row,context))return result;
    }
    if(!EnsureCareCoats(cat::Character::Cream,cat::Character::Tuxedo))return 790;
    Surface canvas;if(!canvas.Create(Width,Height))return 791;
    std::ostringstream trace;trace.imbue(std::locale::classic());trace<<"{\"schema\":1,\"fps\":60,\"frames\":[";
    std::array<bool,5> saved{};const std::array<const wchar_t*,5> names{{L"bat",L"pursue",L"brake",L"turn",L"repeat"}};
    for(unsigned index=0;index<Count;++index) {
        const auto elapsed=static_cast<ULONGLONG>(index)*1000/Fps;
        context="native ball GDI+ output frame="+std::to_string(index);
        {
            Graphics graphics(canvas.image.get());graphics.SetCompositingMode(CompositingModeSourceCopy);
            graphics.Clear(Color(255,246,244,239));graphics.SetCompositingMode(CompositingModeSourceOver);
            graphics.SetCompositingQuality(CompositingQualityHighQuality);graphics.SetInterpolationMode(InterpolationModeHighQualityBicubic);
            graphics.SetSmoothingMode(SmoothingModeAntiAlias);graphics.SetPixelOffsetMode(PixelOffsetModeHighQuality);
            graphics.SetTextRenderingHint(TextRenderingHintAntiAliasGridFit);
            Font title(L"Segoe UI",18,FontStyleRegular,UnitPixel),small(L"Segoe UI",12,FontStyleRegular,UnitPixel);
            SolidBrush ink(Color(255,41,53,60));
            graphics.DrawString(L"Ball play / actual Windows renderer",-1,&title,PointF(20,10),&ink);
            graphics.DrawString(L"60 FPS GDI+ / Box2D, 110 px cats. One placement; real paw impulses. Not a desktop recording.",-1,&small,PointF(20,36),&ink);
            if(index)trace<<',';trace<<"{\"frame\":"<<index<<",\"elapsed_ms\":"<<elapsed<<",\"characters\":[";
            for(unsigned rowIndex=0;rowIndex<rows.size();++rowIndex) {
                auto& row=rows[rowIndex];const auto& frame=row.frames[index];
                const REAL floor=rowIndex?580.f:305.f;
                const double camera=Width*.5-(frame.x+frame.ball.x)*.5;
                const REAL center=static_cast<REAL>(frame.x+camera),ballX=static_cast<REAL>(frame.ball.x+camera);
                Pen ground(Color(255,185,196,197),1);graphics.DrawLine(&ground,20.f,floor+1,Width-20.f,floor+1);
                for(int mark=0;mark<=1300;mark+=40){const REAL x=static_cast<REAL>(mark+camera);
                    if(x>20&&x<Width-20)graphics.DrawLine(&ground,x,floor+1,x,floor+5);}
                // DrawToyBackground uses exactly the product's rotating ball
                // art; replay only its recorded presentation fields here.
                DrawToyBall(graphics,ballX,floor,1,frame.ball);
                DrawPetStyle(graphics,row.character,cat::Outfit::None,frame.pose,row.body,center,
                    static_cast<REAL>(floor-frame.lift-frame.pose.lift),110,&row.presentation,elapsed);
                graphics.DrawString(rowIndex?L"Tuxedo":L"Cream",-1,&small,PointF(22,floor-215),&ink);
                const std::string label=std::string(PhaseName(frame.phase))+" / contacts "+std::to_string(frame.kicks);
                const std::wstring wide(label.begin(),label.end());graphics.DrawString(wide.c_str(),-1,&small,PointF(22,floor-197),&ink);
                if(rowIndex)trace<<',';
                trace<<"{\"character\":"<<static_cast<unsigned>(row.character)<<",\"phase\":\""<<PhaseName(frame.phase)
                    <<"\",\"phase_id\":"<<static_cast<unsigned>(frame.phase)<<",\"motion\":"<<static_cast<unsigned>(frame.motion)
                    <<",\"motion_serial\":"<<frame.serial<<",\"action_elapsed_ms\":"<<frame.actionElapsed
                    <<",\"root_x\":"<<frame.x<<",\"physical_lift\":"<<frame.lift<<",\"velocity_x\":"<<frame.vx
                    <<",\"floor_y\":"<<floor<<",\"canvas_root_x\":"<<center<<",\"camera_offset_x\":"<<camera
                    <<",\"reference_height_px\":110,\"sprite_clip\":"<<static_cast<unsigned>(frame.pose.spriteClip)
                    <<",\"sprite_resource\":"<<frame.resource<<",\"sprite_frame\":"<<frame.source
                    <<",\"ball_x\":"<<frame.ball.x<<",\"ball_lift\":"<<frame.ball.lift
                    <<",\"ball_velocity\":"<<frame.ball.velocity<<",\"ball_angle\":"<<frame.ball.angle
                    <<",\"current_ball_x\":"<<frame.currentBall.x<<",\"current_ball_lift\":"<<frame.currentBall.lift
                    <<",\"paw_world_x\":"<<frame.pawX<<",\"paw_world_height\":"<<frame.pawHeight
                    <<",\"canvas_ball_x\":"<<ballX<<",\"contact_this_frame\":"<<(frame.contact?"true":"false")
                    <<",\"contact_count\":"<<frame.kicks<<",\"last_contact_distance\":"<<frame.kickDistance
                    <<",\"contact_threshold\":"<<row.contactThreshold
                    <<",\"facing\":"<<frame.facing<<",\"direction\":"<<frame.direction
                    <<",\"braking\":"<<(frame.braking?"true":"false")
                    <<",\"waiting_for_stop\":"<<(frame.waiting?"true":"false")<<"}";
            }
            trace<<"]}";graphics.Flush(FlushIntentionSync);if(graphics.GetLastStatus()!=Ok)return 794;
        }
        const auto path=L"render-self-test/ball-play/frame-"+std::wstring(5-std::to_wstring(index).size(),L'0')+std::to_wstring(index)+L".png";
        if(!SaveNativeWalkingReview(*canvas.image,path.c_str()))return 794;
        const auto& frame=rows[0].frames[index];
        const bool turn=index&&frame.facing!=rows[0].frames[index-1].facing;
        const std::array<bool,5> representative{{frame.contact,frame.phase==Phase::Approach&&std::abs(frame.vx)>20,
            frame.braking,turn,frame.contact&&frame.kicks>=3}};
        for(unsigned review=0;review<saved.size();++review)if(!saved[review]&&representative[review]) {
            if(!SaveNativeWalkingReview(*canvas.image,(std::wstring(L"render-self-test/ball-play/review-")+names[review]+L".png").c_str()))return 794;
            saved[review]=true;
        }
    }
    if(std::any_of(saved.begin(),saved.end(),[](bool value){return !value;}))return 795;
    trace<<"]}\n";
    std::ostringstream report;report.imbue(std::locale::classic());
    report<<"{\"schema\":1,\"test\":\"ball-play-native\",\"status\":\"passed\",\"version\":\"" YEOPJARI_VERSION_STRING
        "\",\"render_source\":\"Windows GDI+ / Box2D\",\"desktop_recording\":false,\"os_input_injected\":false"
        ",\"fps\":60,\"frame_count\":"<<Count<<",\"canvas\":["<<Width<<','<<Height<<"],\"duration_seconds\":60"
        ",\"app_active_timer_ms\":16,\"reference_height_px\":110,\"profile\":\"no profile loaded or saved\""
        ",\"scenario\":\"one ball at the work-area edge: make paw room, planted turn, real bat, physical roll/bounce, pursuit, braking and repeat\""
        ",\"guard_cases\":{\"circle_threshold\":true,\"contact_once\":true,\"late_window_no_kick\":true,\"body_near_paw_far_no_kick\":true,\"airborne_no_kick\":true,\"profile_preserved\":true}"
        ",\"trace\":\"trace.json\",\"characters\":[";
    for(unsigned index=0;index<rows.size();++index){const auto& row=rows[index];if(index)report<<',';
        report<<"{\"character\":"<<static_cast<unsigned>(row.character)<<",\"contacts\":"<<row.contacts<<",\"paw_entries\":"<<row.pawEntries
            <<",\"walking_frames\":"<<row.walkFrames<<",\"braking_frames\":"<<row.brakingFrames<<",\"turns\":"<<row.turns
            <<",\"ball_reversals\":"<<row.ballBounces<<",\"ball_travel\":"<<row.ballTravel<<",\"cat_travel\":"<<row.catTravel
            <<",\"max_contact_distance\":"<<row.maxContactDistance<<",\"max_frame_root_delta\":"<<row.maxFrameRootDelta
            <<",\"contact_threshold\":"<<row.contactThreshold
            <<",\"duplicate_contact_rejected\":true,\"source_immutable\":true,\"profile_unchanged\":true}";}
    report<<"],\"review_images\":[\"review-bat.png\",\"review-pursue.png\",\"review-brake.png\",\"review-turn.png\",\"review-repeat.png\"]}\n";
    if(!WriteBytes(L"render-self-test/ball-play/trace.json",trace.str())||!WriteBytes(L"render-self-test/ball-play/capture.json",report.str()))return 796;
    context="Cream/Tuxedo production toy/physics/render path: one placement, real contacts/roll/pursuit/braking/turn/repeat; 3600 frames at 60 FPS";
    return 0;
}
} // namespace native_ball_play_capture
int NativeBallPlayCapture(HINSTANCE instance) {
    g.instance=instance;const HRESULT com=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);if(FAILED(com))return 790;
    ULONG_PTR token=0;GdiplusStartupInput startup;if(GdiplusStartup(&token,&startup,nullptr)!=Ok){CoUninitialize();return 790;}
    int result=0;std::string context="ball play capture initialization";
    try{result=native_ball_play_capture::Run(context);}catch(...){result=799;}
    WriteBytes(L"render-self-test/ball-play-report.txt","exit="+std::to_string(result)+" "+context+"\n");
    ReleaseAtlases();ReleaseCarePresentation();g.styleBlendFrom.Reset();g.styleBlendTo.Reset();
    g.initialized=false;g.manualMotionActive=false;g.window=nullptr;g.toy=ToyKind::None;g.toySession.Cancel();g.toyEngaged=false;
    GdiplusShutdown(token);CoUninitialize();return result;
}
