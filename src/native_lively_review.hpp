#pragma once

// Disposable native fixtures exercise the shipping command, motor, contact,
// source-program sampler and GDI+ painter. They never load/save a user profile.
namespace native_lively_review {
int Run() {
    CreateDirectoryW(L"render-self-test",nullptr);
    CreateDirectoryW(L"render-self-test/lively",nullptr);
    Surface frame,board;
    if(!frame.Create(900,340)||!board.Create(1500,900))return 840;
    std::ostringstream trace;trace<<"{\"schema\":1,\"fps\":30,\"frames\":[";
    unsigned written=0;bool comma=false;
    unsigned row=0;
    for(const auto character:{cat::Character::Cream,cat::Character::Tuxedo,cat::Character::OddEye}) {
        native_manual_motion_tests::Host host;host.Reset(character,false);
        if(!host.artworkReady)return 841;
        g.cursorPlay=g.workInterference=false;g.work={0,0,1000,800};g.dpiScale=1;
        g.centerX=870;ResetMovement();
        for(int i=0;i<12;++i)g.physics.Update(1./60);
        SetMotion(Motion::Idle,10000,host.now);host.Rebase();
        const auto saved=host.Saved();host.Command(kWallClimbCommand);
        if(g.wallPhase!=App::WallPhase::Approach)return 842;
        bool gripped=false,released=false,landed=false,returned=false;
        double peak=0,maxStep=0,previous=g.physics.CurrentPose().x;
        unsigned sample=0;std::string events;auto priorPhase=g.wallPhase;auto priorSerial=g.motionSerial;
        for(unsigned i=0;i<540;++i) {
            UpdateLifeEvent(host.now,.033);host.Frame(33);
            if(g.wallPhase!=priorPhase||g.motionSerial!=priorSerial) {
                events+=" frame="+std::to_string(i)+" phase="+std::to_string(static_cast<unsigned>(g.wallPhase))+
                    " motion="+std::to_string(static_cast<unsigned>(g.motion))+" age="+std::to_string(host.now-g.wallStarted)+
                    " grip="+std::to_string(g.physics.WallClimbing())+" lift="+std::to_string(g.physics.CurrentPose().lift);
                priorPhase=g.wallPhase;priorSerial=g.motionSerial;
            }
            const auto physical=g.physics.CurrentPose(),rendered=g.physics.RenderPose();
            const auto pose=PrimaryDisplayedPose(host.now,rendered);
            if(std::abs(physical.x-previous)>maxStep) {
                maxStep=std::abs(physical.x-previous);
                if(maxStep>4)events+=" large-step-frame="+std::to_string(i)+" x="+std::to_string(physical.x)+" old="+std::to_string(previous);
            }
            previous=physical.x;
            peak=std::max(peak,physical.lift);gripped=gripped||g.physics.WallClimbing();
            released=released||(gripped&&!g.physics.WallClimbing()&&!physical.grounded);
            landed=landed||(released&&physical.grounded);
            returned=returned||(landed&&g.wallPhase==App::WallPhase::None&&!g.wallBounds);
            if(g.physics.WallClimbing()&&(pose.spriteClip!=cat::sprite_sequence::Clip::Climb||ManualMotionAvailable()))return 843;
            {
                Graphics graphics(frame.image.get());graphics.Clear(Color(255,245,247,249));
                graphics.SetInterpolationMode(InterpolationModeHighQualityBicubic);
                Pen edge(Color(255,150,160,170),2);graphics.DrawLine(&edge,0,300,880,300);graphics.DrawLine(&edge,880,20,880,300);
                Font font(L"Segoe UI",13,FontStyleRegular,UnitPixel);SolidBrush ink(Color(255,40,55,70));
                graphics.DrawString(L"Dropshot source motion / native GDI+ + Box2D / right monitor edge",-1,&font,PointF(20,10),&ink);
                DrawPetStyle(graphics,character,cat::Outfit::None,pose,
                    cat::appearance::VisualModifiers(g.nutrition,character),
                    static_cast<REAL>(rendered.x-120),static_cast<REAL>(300-rendered.lift),110);
                graphics.Flush(FlushIntentionSync);
            }
            wchar_t path[180]{};swprintf(path,180,L"render-self-test/lively/frame-%05u.png",written);
            if(!SaveNativeWalkingReview(*frame.image,path))return 844;
            if(comma)trace<<',';comma=true;
            trace<<"{\"frame\":"<<written++<<",\"character\":"<<static_cast<unsigned>(character)
                <<",\"x\":"<<physical.x<<",\"lift\":"<<physical.lift<<",\"vx\":"<<physical.velocityX
                <<",\"vy\":"<<physical.velocityY<<",\"grip\":"<<(g.physics.WallClimbing()?"true":"false")<<'}';
            if(g.wallPhase==App::WallPhase::Climb&&sample<5&&host.now-g.wallStarted>=sample*1400) {
                Graphics graphics(board.image.get());graphics.DrawImage(frame.image.get(),Rect(300*sample,300*row,300,300),
                    580,0,300,300,UnitPixel);++sample;
            }
            if(returned&&i>400)break;
        }
        if(!gripped||!released||!landed||!returned||peak<100||peak>160||maxStep>4||host.Saved()!=saved) {
            WriteBytes(L"render-self-test/lively/failure.txt","grip="+std::to_string(gripped)+" release="+std::to_string(released)+
                " land="+std::to_string(landed)+" return="+std::to_string(returned)+" peak="+std::to_string(peak)+" step="+std::to_string(maxStep)+events);
            return 845;
        }
        ++row;
    }
    trace<<"]}\n";
    if(!SaveNativeWalkingReview(*board.image,L"render-self-test/lively/climb-board.png")||
            !WriteBytes(L"render-self-test/lively/trace.json",trace.str()))return 846;
    // All three stop outcomes must remain reachable through the real rest
    // dispatcher and play to completion while the motor brakes its own body.
    for(const auto wanted:{cat::sprite_sequence::Clip::Brake,cat::sprite_sequence::Clip::Slide,cat::sprite_sequence::Clip::Stumble}) {
        bool found=false;
        for(unsigned seed=1;seed<=80&&!found;++seed) {
            native_manual_motion_tests::Host host;host.Reset();g.random.seed(seed);
            g.work={0,0,1800,800};g.dpiScale=1;g.centerX=400;ResetMovement();
            SetMotion(Motion::Zoomies,4000,host.now);g.physics.SetTargetVelocity(120);
            for(int i=0;i<90;++i)g.physics.Update(1./60);
            BeginAutonomousRest(host.now);if(g.stopClip!=wanted)continue;found=true;
            const auto start=g.stopStarted,end=g.stopUntil;const auto saved=host.Saved();
            for(unsigned i=0;i<5;++i) {
                const auto target=start+(end-start-1)*i/4;
                while(host.now<target)host.Frame(std::min<ULONGLONG>(16,target-host.now));
                const auto physical=g.physics.RenderPose();const auto pose=PrimaryDisplayedPose(host.now,physical);
                if(pose.spriteClip!=wanted||!CareSpriteFor(pose,g.preferences.character))return 847;
                Graphics graphics(board.image.get());
                const int y=(static_cast<unsigned>(wanted)-static_cast<unsigned>(cat::sprite_sequence::Clip::Brake))*300;
                SolidBrush background(Color(255,245,247,249));graphics.FillRectangle(&background,i*300,y,300,300);
                DrawPetStyle(graphics,g.preferences.character,cat::Outfit::None,pose,
                    cat::appearance::VisualModifiers(g.nutrition,g.preferences.character),static_cast<REAL>(i*300+150),static_cast<REAL>(y+240),110);
            }
            if(host.Saved()!=saved)return 848;
        }
        if(!found)return 849;
    }
    if(!SaveNativeWalkingReview(*board.image,L"render-self-test/lively/stops-board.png"))return 850;
    row=0;
    for(const auto character:{cat::Character::Cream,cat::Character::Tuxedo,cat::Character::OddEye}) {
        native_manual_motion_tests::Host host;host.Reset(character,false);
        g.cursorPlay=g.workInterference=false;g.work={0,0,1600,800};g.dpiScale=1;g.centerX=600;ResetMovement();
        for(int i=0;i<12;++i)g.physics.Update(1./60);
        SetMotion(Motion::Idle,10000,host.now);host.Rebase();
        const auto saved=host.Saved();PlaceToy(ToyKind::FishingRod);host.Rebase();g.bubbleUntil=0;
        const auto* source=LoadedCareSprite(cat::sprite_sequence::Clip::ToyPlay,character);
        if(!source)return 851;
        const auto duration=cat::sprite_sequence::ProgramDuration(source->layout->program);
        unsigned sample=0;const ULONGLONG times[]{0,duration/4,duration/2,duration*3/4,duration-50};
        std::vector<bool> visited(source->layout->frameCount,false);bool entered=false,finished=false;
        for(unsigned i=0;i<1200;++i) {
            AdvanceToyObjects(.033);host.Frame(33);
            const auto physical=g.physics.RenderPose();const auto pose=PrimaryDisplayedPose(host.now,physical);
            if(entered&&g.toySession.phase!=cat::toy::Phase::Interact){finished=true;break;}
            if(pose.spriteClip==cat::sprite_sequence::Clip::ToyPlay) {
                entered=true;visited[SpriteFrameFor(pose,*source->layout)]=true;
            }
            if(sample==5)continue;
            if(pose.spriteClip!=cat::sprite_sequence::Clip::ToyPlay||g.toySession.elapsedMs<times[sample])continue;
            const auto* sprite=CareSpriteFor(pose,character);
            if(!sprite||SpriteFrameFor(pose,*sprite->layout)>=sprite->layout->frameCount||!physical.grounded)return 851;
            Graphics graphics(board.image.get());
            SolidBrush background(Color(255,245,247,249));graphics.FillRectangle(&background,sample*300,row*300,300,300);
            const REAL root=static_cast<REAL>(sample*300+150),floor=static_cast<REAL>(row*300+260);
            const REAL toyX=root+static_cast<REAL>(g.toyX-physical.x);
            DrawToyBackground(graphics,toyX,floor,host.now);
            DrawPetStyle(graphics,character,cat::Outfit::None,pose,
                cat::appearance::VisualModifiers(g.nutrition,character),root,floor,110);
            ++sample;
        }
        if(sample!=5||!finished||std::find(visited.begin(),visited.end(),false)!=visited.end()||host.Saved()!=saved)return 852;
        ++row;
    }
    return SaveNativeWalkingReview(*board.image,L"render-self-test/lively/toy-board.png")?0:853;
}
}
int NativeLivelyReview(HINSTANCE instance) {
    g.instance=instance;ULONG_PTR token=0;GdiplusStartupInput startup;
    if(GdiplusStartup(&token,&startup,nullptr)!=Ok)return 839;
    const int result=LoadAtlas()?native_lively_review::Run():840;
    WriteBytes(L"render-self-test/lively-report.txt","exit="+std::to_string(result)+" native lively motion review\n");
    ReleaseAtlases();ReleaseCarePresentation();GdiplusShutdown(token);return result;
}
