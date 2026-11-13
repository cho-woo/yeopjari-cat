#pragma once

// Memory-only pointer samples enter the shipping CursorSession, motor, Box2D,
// pose selector and cached GDI+ renderer. No OS pointer is moved and no window,
// user profile, input hook, timer thread or alternative animation is created.
namespace native_cursor_play_capture {
using Action=cat::environment::CursorAction;
using Clip=cat::sprite_sequence::Clip;
constexpr unsigned Fps=60,Count=3600;
constexpr int Width=1440,Height=1060,ActorWidth=360,ActorHeight=420,ActorFloor=390;
enum class Stage { Low,Tiptoe,BothPaws,Hop,Far,Care,Resume };
Stage StageAt(ULONGLONG elapsed) {
    return elapsed<8000?Stage::Low:elapsed<20000?Stage::Tiptoe:elapsed<38000?Stage::BothPaws:
        elapsed<48000?Stage::Hop:elapsed<50000?Stage::Far:elapsed<55000?Stage::Care:Stage::Resume;
}
const char* StageName(Stage stage) {
    switch(stage) {
    case Stage::Low:return "stationary low pointer / repeated paw";
    case Stage::Tiptoe:return "escaping pointer / running catch";
    case Stage::BothPaws:return "pointer reversal / planted turn / reacquire";
    case Stage::Hop:return "stationary high pointer / physical hop / roaming paused";
    case Stage::Far:return "pointer unavailable / near re-entry";
    case Stage::Care:return "care action owns the cat";
    default:return "stationary pointer over body / full paw lowering";
    }
}
const char* ActionName(Action action) {
    switch(action) {
    case Action::None:return "none";case Action::Watch:return "watch";case Action::Hunt:return "hunt";
    case Action::Sniff:return "sniff";case Action::Paw:return "paw";case Action::Abandon:return "abandon";
    case Action::ApproachSit:return "approach_sit";case Action::Sit:return "sit";case Action::Punch:return "punch";
    case Action::Chase:return "chase";case Action::Pounce:return "pounce";case Action::Sneak:return "sneak";
    case Action::Tiptoe:return "tiptoe";case Action::BothPaws:return "both_paws";case Action::Rest:return "rest";
    }
    return "invalid";
}
struct Frame {
    cat::animation::Pose pose;
    double x=0,lift=0,velocityX=0,velocityY=0,pointerX=0,pointerHeight=0,referenceHeight=0;
    std::array<cat::deform::Point,2> paws{};
    unsigned source=0,resource=0,contacts=0;
    std::uint64_t serial=0,actorHash=0,actionElapsed=0;
    Motion motion=Motion::Idle;
    Action action=Action::None;
    Stage stage=Stage::Low;
    bool grounded=true,menu=false,drag=false,roamingPaused=false,waiting=false,contactAllowed=false;
};
struct Row {
    cat::Character character=cat::Character::Cream;
    cat::nutrition::Visual body;
    std::vector<Frame> frames;
    CarePresentationRenderer presentation;
    std::array<unsigned,15> entries{};
    double startX=0,peakLift=0,pausedRootX=0,pausedRootDelta=0,maxFrameRootDelta=0;
    unsigned hops=0,landings=0,awards=0,duplicateAwards=0,menuFrames=0,dragFrames=0,careFrames=0;
    unsigned pausedGroundReaches=0,pausedHopFrames=0,resumePaws=0;
    unsigned chaseFrames=0,movingLaunches=0,turnFrames=0,pounceDrawings=0;
    bool menuHeld=true,dragHeld=true,farCancelled=false,careInterruptedActive=false,careProtected=true,sourceImmutable=false;
    std::uint64_t sourceHash=0;
};
std::uint64_t BaseSourceHash(cat::Character character) {
    const auto& source=CurrentArt(character).base.skins[4].source;
    return cat::Checksum(std::string(reinterpret_cast<const char*>(source.data()),source.size()*sizeof(source[0])));
}
bool DrawActor(Surface& actor,Row& row,const Frame& frame,ULONGLONG elapsed) {
    Graphics graphics(actor.image.get());graphics.SetCompositingMode(CompositingModeSourceCopy);
    graphics.Clear(Color(0,0,0,0));graphics.SetCompositingMode(CompositingModeSourceOver);
    graphics.SetCompositingQuality(CompositingQualityHighQuality);
    graphics.SetInterpolationMode(InterpolationModeHighQualityBicubic);graphics.SetSmoothingMode(SmoothingModeAntiAlias);
    graphics.SetPixelOffsetMode(PixelOffsetModeHighQuality);
    // This is the same screen-space root as BuildPlacement plus the fixed
    // diagnostic canvas offset. Physical and cosmetic lift are applied once.
    DrawPetStyle(graphics,row.character,cat::Outfit::None,frame.pose,row.body,ActorWidth*.5f,
        static_cast<REAL>(ActorFloor-frame.lift-frame.pose.lift),110,&row.presentation,elapsed,0,frame.menu||frame.drag);
    graphics.Flush(FlushIntentionSync);
    return graphics.GetLastStatus()==Ok&&native_coat_tests::ValidPixels(
        reinterpret_cast<const std::uint32_t*>(actor.pixels),ActorWidth,ActorHeight,true);
}
std::wstring ActorCachePath(cat::Character character,unsigned index) {
    return L"render-self-test/cursor-actors/c"+std::to_wstring(static_cast<unsigned>(character))+L"-"+std::to_wstring(index)+L".rgba";
}
bool CacheActor(const Surface& actor,cat::Character character,unsigned index) {
    const auto* pixels=reinterpret_cast<const std::uint32_t*>(actor.pixels);
    unsigned left=ActorWidth,top=ActorHeight,right=0,bottom=0;
    for(unsigned y=0;y<ActorHeight;++y)for(unsigned x=0;x<ActorWidth;++x)if(pixels[y*ActorWidth+x]>>24) {
        left=std::min(left,x);right=std::max(right,x+1);top=std::min(top,y);bottom=std::max(bottom,y+1);
    }
    if(left>=right||top>=bottom)return false;
    const std::array<unsigned,4> rect{{left,top,right-left,bottom-top}};
    std::string bytes(reinterpret_cast<const char*>(rect.data()),sizeof(rect));
    for(unsigned y=top;y<bottom;++y)bytes.append(reinterpret_cast<const char*>(pixels+y*ActorWidth+left),(right-left)*4);
    const auto path=ActorCachePath(character,index);
    HANDLE file=CreateFileW(path.c_str(),GENERIC_WRITE,0,nullptr,CREATE_ALWAYS,FILE_ATTRIBUTE_TEMPORARY,nullptr);
    if(file==INVALID_HANDLE_VALUE)return false;
    DWORD written=0;const bool ok=WriteFile(file,bytes.data(),static_cast<DWORD>(bytes.size()),&written,nullptr)&&written==bytes.size();
    return CloseHandle(file)&&ok;
}
bool RestoreActor(Surface& actor,cat::Character character,unsigned index) {
    // Store the already rendered premultiplied crop verbatim. This preserves
    // the normal two-material resident budget while comparing all three coats.
    const auto path=ActorCachePath(character,index);
    HANDLE file=CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_TEMPORARY,nullptr);
    if(file==INVALID_HANDLE_VALUE)return false;
    const DWORD size=GetFileSize(file,nullptr);
    if(size<16||size>ActorWidth*ActorHeight*4+16){CloseHandle(file);return false;}
    std::string bytes(size,'\0');DWORD read=0;
    const bool ok=ReadFile(file,bytes.data(),size,&read,nullptr)&&read==size;CloseHandle(file);
    if(!ok)return false;
    std::array<unsigned,4> rect;std::memcpy(rect.data(),bytes.data(),16);
    const auto [x,y,width,height]=rect;
    if(x+width>ActorWidth||y+height>ActorHeight||bytes.size()!=16+width*height*4)return false;
    std::memset(actor.pixels,0,ActorWidth*ActorHeight*4);
    auto* pixels=reinterpret_cast<std::uint32_t*>(actor.pixels);
    for(unsigned row=0;row<height;++row)
        std::memcpy(pixels+(y+row)*ActorWidth+x,bytes.data()+16+row*width*4,width*4);
    DeleteFileW(ActorCachePath(character,index).c_str());return true;
}
int Simulate(Row& row,std::string& context) {
    native_manual_motion_tests::Host host;host.Reset(row.character,false);
    g.cursorPlay=true;g.workInterference=false;g.random.seed(74021);g.randomZoomiesRandom.seed(1936);
    g.direction=g.facing=1;g.nextDecision=host.now+120000;
    const auto saved=host.Saved();const auto start=host.now;
    row.startX=g.physics.RenderPose().x;row.body=cat::appearance::VisualModifiers(g.nutrition,row.character);
    row.sourceHash=BaseSourceHash(row.character);row.frames.reserve(Count);
    const auto geometry=cat::cursor::Bounded(CursorAttackGeometry());
    const double lowDx=35,lowHeight=40;
    double pointerX=row.startX+lowDx,pointerHeight=lowHeight,previousX=row.startX;
    Stage previousStage=Stage::Low;
    Action previousAction=Action::None;
    bool previousGrounded=true,nearBeforeCare=false;
    cat::animation::Pose held;
    Surface actor;if(!actor.Create(ActorWidth,ActorHeight))return 681;
    std::uint64_t previousHash=0,pauseHash=0;
    for(unsigned index=0;index<Count;++index) {
        const auto elapsed=static_cast<ULONGLONG>(index)*1000/Fps;
        const auto now=start+elapsed,step=now-host.now;
        const Stage stage=StageAt(elapsed);
        context="cursor play character="+std::to_string(static_cast<unsigned>(row.character))+" frame="+
            std::to_string(index)+" stage="+StageName(stage);
        if(stage!=previousStage) {
            const auto root=g.physics.RenderPose();
            pointerX=root.x+(stage==Stage::Resume?0:stage==Stage::Low?lowDx:stage==Stage::Far?360:32);
            pointerHeight=stage==Stage::Resume?65:stage==Stage::Tiptoe?90:stage==Stage::BothPaws?135:stage==Stage::Hop?190:lowHeight;
            g.pet.roamingPaused=stage==Stage::Hop;
            if(stage==Stage::Hop)row.pausedRootX=root.x;
            if(stage==Stage::Care) {
                row.careInterruptedActive=cursorInteraction.session.Active();
                SetMotion(Motion::Eat,5000,now);
            }
            if(stage==Stage::Resume)SetMotion(Motion::Idle,10000,now);
            previousStage=stage;
        }
        if(stage==Stage::Tiptoe) {
            const double seconds=(elapsed-8000)*.001;
            pointerX=800+300*std::sin(seconds*.55);
            pointerHeight=110+35*std::sin(seconds*.9);
        }
        if(stage==Stage::Low)pointerX=row.startX+35+6*std::sin(elapsed*.001*2*cat::animation::kPi*3);
        if(stage==Stage::BothPaws) {
            const double seconds=(elapsed-20000)*.001;
            pointerX=static_cast<unsigned>(seconds/5.5)%2?100:1100;
            pointerHeight=8;
        }
        const bool available=!(stage==Stage::BothPaws&&elapsed>=35000)&&
            !(stage==Stage::Far&&elapsed<49000);
        if(stage==Stage::Far&&elapsed>=49000&&!nearBeforeCare) {
            pointerX=g.physics.RenderPose().x+lowDx;pointerHeight=lowHeight;nearBeforeCare=true;
        }
        const bool menu=elapsed>=900&&elapsed<1300;
        const bool drag=elapsed>=3200&&elapsed<3600;
        const bool paused=menu||drag;
        const bool wasPaused=index&&(row.frames.back().menu||row.frames.back().drag);
        g.menuOpen=menu;g.dragging=drag;host.now=now;
        if(index) {
            if(paused)FreezePrimaryMotion(now,step);
            else {
                const POINT pointer{static_cast<LONG>(std::lround(g.work.left+pointerX*g.dpiScale)),
                    static_cast<LONG>(std::lround(g.work.bottom-pointerHeight*g.dpiScale))};
                // Same active portion of Tick, with only GetCursorPos replaced
                // by this explicit in-memory sample. SetMotion uses this clock.
                UpdateCursorReactionSample(now,step,pointer,available);
                FinishManualMotion(now);AdvanceRandomZoomies(now,step/1000.);
                DispatchAutonomousMotion(now);AdvancePrimaryMotion(now,step/1000.);
            }
        }
        const auto physical=g.physics.CurrentPose(),rendered=g.physics.RenderPose();
        if(!std::isfinite(rendered.x)||!std::isfinite(rendered.lift)||!std::isfinite(rendered.velocityX)||
                !std::isfinite(rendered.velocityY)||rendered.lift<-.1)return 682;
        const auto pose=paused?held:PrimaryDisplayedPose(now,rendered);
        if(!paused)held=pose;
        if(!cat::deform::Finite(pose.joints)||!std::isfinite(pose.x)||!std::isfinite(pose.lift))return 682;
        const auto action=cursorInteraction.session.Action();
        if(action!=previousAction) {
            const auto value=static_cast<unsigned>(action);if(value>=row.entries.size())return 682;
            ++row.entries[value];
            if(stage==Stage::Resume&&action==Action::Tiptoe)++row.resumePaws;
            previousAction=action;
        }
        if(previousGrounded&&!physical.grounded) {
            ++row.hops;if(std::abs(physical.velocityX)>20)++row.movingLaunches;
        }
        if(!previousGrounded&&physical.grounded)++row.landings;
        previousGrounded=physical.grounded;
        row.peakLift=std::max(row.peakLift,physical.lift);
        if(action==Action::Chase)++row.chaseFrames;
        if(pose.spriteClip==Clip::RunningTurn)++row.turnFrames;
        if(pose.spriteClip==Clip::CursorPounce)++row.pounceDrawings;
        row.maxFrameRootDelta=std::max(row.maxFrameRootDelta,std::abs(rendered.x-previousX));previousX=rendered.x;
        if(stage==Stage::Hop) {
            row.pausedRootDelta=std::max(row.pausedRootDelta,std::abs(rendered.x-row.pausedRootX));
            if(!physical.grounded)++row.pausedHopFrames;
            if(action==Action::Tiptoe||action==Action::BothPaws||action==Action::Paw)++row.pausedGroundReaches;
        }
        // An already launched ballistic hop finishes safely. Moving the
        // pointer away must cancel its contact immediately, not teleport the
        // body to the floor merely to make the session inactive.
        if(stage==Stage::Far&&elapsed>=48700&&elapsed<49000&&
                (!cursorInteraction.session.Active()||
                    (cursorInteraction.session.InFlight()&&!cursorInteraction.session.ContactAllowed())))row.farCancelled=true;
        if(stage==Stage::Care) {
            ++row.careFrames;
            row.careProtected=row.careProtected&&!cursorInteraction.session.Active()&&g.motion==Motion::Eat;
        }
        const auto awarded=cursorInteraction.awardedContacts;
        // The real update already visited contact. Re-visiting an identical
        // physics/action sample must not award memory or effects a second time.
        if(index&&!paused&&UpdateCursorContact(now,physical))++row.duplicateAwards;
        if(cursorInteraction.awardedContacts!=awarded)++row.duplicateAwards;
        const auto* asset=CareSpriteFor(pose,row.character);
        const unsigned source=asset?SpriteFrameFor(pose,*asset->layout):static_cast<unsigned>(pose.frame);
        if(asset&&source>=asset->layout->frameCount)return 682;
        Frame frame;frame.pose=pose;frame.x=rendered.x;frame.lift=rendered.lift;
        frame.velocityX=rendered.velocityX;frame.velocityY=rendered.velocityY;
        frame.pointerX=pointerX;frame.pointerHeight=pointerHeight;frame.paws=cat::cursor::PawPositions(pose,geometry);
        frame.referenceHeight=asset?asset->layout->referenceHeight:CurrentArt(row.character).base.standingHeight;
        frame.source=source;frame.resource=asset?asset->layout->resourceId:CurrentArt(row.character).base.baseResourceId;
        frame.contacts=cursorInteraction.awardedContacts;frame.serial=g.motionSerial;frame.motion=g.motion;
        frame.actionElapsed=now>=g.motionStart?now-g.motionStart:0;
        frame.action=action;frame.stage=stage;frame.grounded=rendered.grounded;
        frame.menu=menu;frame.drag=drag;frame.roamingPaused=g.pet.roamingPaused;frame.waiting=g.waitingForStop;
        frame.contactAllowed=cursorInteraction.session.ContactAllowed();
        if(!DrawActor(actor,row,frame,elapsed))return 683;
        frame.actorHash=cat::Checksum(std::string(reinterpret_cast<const char*>(actor.pixels),ActorWidth*ActorHeight*4));
        if(!CacheActor(actor,row.character,index))return 683;
        if(paused) {
            if(!wasPaused)pauseHash=previousHash;
            if(menu){++row.menuFrames;row.menuHeld=row.menuHeld&&frame.actorHash==pauseHash;}
            if(drag){++row.dragFrames;row.dragHeld=row.dragHeld&&frame.actorHash==pauseHash;}
        }
        previousHash=frame.actorHash;row.frames.push_back(frame);
    }
    row.awards=cursorInteraction.awardedContacts;row.sourceImmutable=BaseSourceHash(row.character)==row.sourceHash;
    g.menuOpen=g.dragging=false;g.pet.roamingPaused=false;
    const auto entries=[&](Action action){return row.entries[static_cast<unsigned>(action)];};
    context+=" paws="+std::to_string(entries(Action::Paw))+" tiptoe="+std::to_string(entries(Action::Tiptoe))+
        " both="+std::to_string(entries(Action::BothPaws))+" hops="+std::to_string(row.hops)+
        " landings="+std::to_string(row.landings)+" awards="+std::to_string(row.awards)+
        " pausedRootDelta="+std::to_string(row.pausedRootDelta)+" resumed="+std::to_string(row.resumePaws)+
        " peakLift="+std::to_string(row.peakLift)+" duplicateAwards="+std::to_string(row.duplicateAwards)+
        " menuHeld="+std::to_string(row.menuHeld)+" dragHeld="+std::to_string(row.dragHeld)+
        " careProtected="+std::to_string(row.careProtected)+" careInterruptedActive="+std::to_string(row.careInterruptedActive)+
        " farCancelled="+std::to_string(row.farCancelled)+
        " sourceImmutable="+std::to_string(row.sourceImmutable)+" profileUnchanged="+std::to_string(host.Saved()==saved);
    context+=" chaseFrames="+std::to_string(row.chaseFrames)+" movingLaunches="+std::to_string(row.movingLaunches)+
        " turnFrames="+std::to_string(row.turnFrames)+" pounceDrawings="+std::to_string(row.pounceDrawings);
    // The longer source recovery/quiet interval deliberately permits fewer
    // low-pointer attempts; the separate host test verifies repeated strong
    // swats. This capture must also show the new over-body source action.
    if(!entries(Action::Paw)||!entries(Action::Tiptoe)||row.chaseFrames<120||!row.movingLaunches||!row.turnFrames||!row.pounceDrawings||
            !row.hops||row.hops!=row.landings||row.peakLift<20||!row.pausedHopFrames||row.pausedRootDelta>.05||
            !row.resumePaws||!row.awards||row.duplicateAwards||!row.menuFrames||!row.dragFrames||
            !row.menuHeld||!row.dragHeld||!row.careFrames||!row.careInterruptedActive||!row.careProtected||!row.farCancelled||
            !row.sourceImmutable||host.Saved()!=saved)return 684;
    return 0;
}
int Run(std::string& context) {
    if(!LoadAtlas())return 680;
    for(const auto path:{L"render-self-test",L"render-self-test/cursor-play",L"render-self-test/cursor-actors"})
        if(!CreateDirectoryW(path,nullptr)&&GetLastError()!=ERROR_ALREADY_EXISTS)return 681;
    std::array<Row,3> rows{};rows[1].character=cat::Character::Tuxedo;rows[2].character=cat::Character::OddEye;
    for(auto& row:rows)if(const int result=Simulate(row,context))return result;
    Surface canvas,actor;if(!canvas.Create(Width,Height)||!actor.Create(ActorWidth,ActorHeight))return 681;
    const std::array<const wchar_t*,8> reviewNames{{L"paw",L"chase",L"turn",L"hop",L"care",L"resume",L"menu",L"drag"}};
    std::array<bool,8> reviewSaved{};
    std::ostringstream trace;trace.imbue(std::locale::classic());trace<<"{\"schema\":1,\"fps\":60,\"frames\":[";
    for(unsigned index=0;index<Count;++index) {
        const auto elapsed=static_cast<ULONGLONG>(index)*1000/Fps;
        context="native cursor GDI+ output frame="+std::to_string(index);
        {
            Graphics graphics(canvas.image.get());graphics.SetCompositingMode(CompositingModeSourceCopy);
            graphics.Clear(Color(255,246,244,239));graphics.SetCompositingMode(CompositingModeSourceOver);
            graphics.SetCompositingQuality(CompositingQualityHighQuality);
            graphics.SetInterpolationMode(InterpolationModeHighQualityBicubic);graphics.SetSmoothingMode(SmoothingModeAntiAlias);
            graphics.SetPixelOffsetMode(PixelOffsetModeHighQuality);graphics.SetTextRenderingHint(TextRenderingHintAntiAliasGridFit);
            Font title(L"Segoe UI",18,FontStyleRegular,UnitPixel),small(L"Segoe UI",12,FontStyleRegular,UnitPixel);
            SolidBrush ink(Color(255,41,53,60)),pointerBrush(Color(255,217,100,102));
            graphics.DrawString(L"Pointer pursuit / actual Windows renderer",-1,&title,PointF(20,10),&ink);
            graphics.DrawString(L"60 FPS GDI+ + Box2D, 110 px cats; memory-only pointer samples. Not a desktop recording.",-1,
                &small,PointF(20,37),&ink);
            if(index)trace<<',';trace<<"{\"frame\":"<<index<<",\"elapsed_ms\":"<<elapsed<<",\"characters\":[";
            for(unsigned r=0;r<rows.size();++r) {
                auto& row=rows[r];const auto& frame=row.frames[index];
                if(!RestoreActor(actor,row.character,index))return 683;
                const auto hash=cat::Checksum(std::string(reinterpret_cast<const char*>(actor.pixels),ActorWidth*ActorHeight*4));
                if(hash!=frame.actorHash)return 685;
                const REAL floor=335.0f+r*340.0f;
                const double worldCenter=80+frame.x;
                const double camera=0; // Entire1280px logical monitor plus80px margins.
                const REAL center=static_cast<REAL>(worldCenter+camera);
                graphics.DrawImage(actor.image.get(),center-ActorWidth*.5f,floor-ActorFloor,
                    static_cast<REAL>(ActorWidth),static_cast<REAL>(ActorHeight));
                Pen ground(Color(255,185,196,197),1);graphics.DrawLine(&ground,20.0f,floor+1,Width-20.0f,floor+1);
                for(int mark=-400;mark<1600;mark+=40) {
                    const REAL x=static_cast<REAL>(80+mark+camera);
                    if(x>=20&&x<=Width-20)graphics.DrawLine(&ground,x,floor+1,x,floor+5);
                }
                const REAL pointerX=static_cast<REAL>(80+frame.pointerX+camera),pointerY=static_cast<REAL>(floor-frame.pointerHeight);
                graphics.FillEllipse(&pointerBrush,pointerX-4,pointerY-4,8.0f,8.0f);
                graphics.DrawString(r==2?L"Snowdrop":r?L"Tuxedo":L"Cream",-1,&small,PointF(22,floor-255),&ink);
                std::string label=StageName(frame.stage);label+=" / ";label+=ActionName(frame.action);
                if(frame.menu)label+=" / menu pause";if(frame.drag)label+=" / drag pause";
                const std::wstring wide(label.begin(),label.end());
                graphics.DrawString(wide.c_str(),-1,&small,PointF(22,floor-237),&ink);
                if(r)trace<<',';
                trace<<"{\"character\":"<<static_cast<unsigned>(row.character)<<",\"stage\":\""<<StageName(frame.stage)
                    <<"\",\"action\":\""<<ActionName(frame.action)<<"\",\"action_id\":"<<static_cast<unsigned>(frame.action)
                    <<",\"motion_serial\":"<<frame.serial<<",\"action_elapsed_ms\":"<<frame.actionElapsed
                    <<",\"requested_motion\":"<<static_cast<unsigned>(frame.motion)
                    <<",\"root_x\":"<<frame.x<<",\"physical_lift\":"<<frame.lift
                    <<",\"velocity_x\":"<<frame.velocityX<<",\"velocity_y\":"<<frame.velocityY
                    <<",\"canvas_root_x\":"<<center<<",\"canvas_root_y\":"<<floor-frame.lift
                    <<",\"camera_offset_x\":"<<camera<<",\"floor_y\":"<<floor
                    <<",\"pointer_world_x\":"<<frame.pointerX<<",\"pointer_world_height\":"<<frame.pointerHeight
                    <<",\"source_reference_height\":"<<frame.referenceHeight<<",\"reference_height_px\":110"
                    <<",\"sprite_clip\":"<<static_cast<unsigned>(frame.pose.spriteClip)<<",\"sprite_resource\":"<<frame.resource
                    <<",\"sprite_frame\":"<<frame.source<<",\"pose_offset_x\":"<<frame.pose.x<<",\"pose_cosmetic_lift\":"<<frame.pose.lift
                    <<",\"actor_hash\":\""<<frame.actorHash<<"\",\"awarded_contacts\":"<<frame.contacts
                    <<",\"grounded\":"<<(frame.grounded?"true":"false")<<",\"contact_allowed\":"<<(frame.contactAllowed?"true":"false")
                    <<",\"menu_paused\":"<<(frame.menu?"true":"false")<<",\"drag_paused\":"<<(frame.drag?"true":"false")
                    <<",\"roaming_paused\":"<<(frame.roamingPaused?"true":"false")<<",\"waiting_for_stop\":"<<(frame.waiting?"true":"false")
                    <<",\"cursor_reach\":["<<frame.pose.cursorReach.rise<<','<<frame.pose.cursorReach.leftLift<<','
                    <<frame.pose.cursorReach.rightLift<<','<<frame.pose.cursorReach.aimX<<"],\"paws_local\":[["
                    <<frame.paws[0].x<<','<<frame.paws[0].y<<"],["<<frame.paws[1].x<<','<<frame.paws[1].y<<"]]}";
            }
            trace<<"]}";graphics.Flush(FlushIntentionSync);if(graphics.GetLastStatus()!=Ok)return 685;
        }
        const auto path=L"render-self-test/cursor-play/frame-"+std::wstring(5-std::to_wstring(index).size(),L'0')+
            std::to_wstring(index)+L".png";
        if(!SaveNativeWalkingReview(*canvas.image,path.c_str()))return 685;
        const auto& frame=rows[0].frames[index];
        const std::array<bool,8> representative{{
            frame.stage==Stage::Low&&frame.action==Action::Paw&&frame.actionElapsed>=240&&frame.actionElapsed<=340,
            frame.action==Action::Chase&&std::abs(frame.velocityX)>100,
            frame.pose.spriteClip==Clip::RunningTurn&&frame.actionElapsed>=550,
            !frame.grounded&&frame.lift>=rows[0].peakLift*.85,
            frame.stage==Stage::Care&&frame.pose.spriteClip==Clip::Eat&&frame.actionElapsed>=1500,
            frame.stage==Stage::Resume&&frame.action==Action::Tiptoe&&frame.actionElapsed>=1200,
            frame.menu,frame.drag}};
        for(unsigned review=0;review<reviewSaved.size();++review)if(!reviewSaved[review]&&representative[review]) {
            if(!SaveNativeWalkingReview(*canvas.image,(std::wstring(L"render-self-test/cursor-play/review-")+reviewNames[review]+L".png").c_str()))return 685;
            reviewSaved[review]=true;
        }
    }
    if(std::any_of(reviewSaved.begin(),reviewSaved.end(),[](bool saved){return !saved;}))return 687;
    trace<<"]}\n";
    std::ostringstream report;report.imbue(std::locale::classic());
    report<<"{\"schema\":1,\"test\":\"cursor-play-native\",\"status\":\"passed\",\"version\":\"" YEOPJARI_VERSION_STRING
        "\",\"render_source\":\"Windows GDI+ / Box2D\",\"desktop_recording\":false,\"os_input_injected\":false"
        ",\"app_active_timer_ms\":16,\"capture_clock\":\"deterministic 16/17 ms production updates at 60 Hz\""
        ",\"fps\":60,\"frame_count\":"<<Count<<",\"canvas\":["<<Width<<','<<Height<<"],\"reference_height_px\":110"
        ",\"duration_seconds\":60,\"profile\":\"no profile loaded or saved\",\"trace\":\"trace.json\""
        ",\"scenario\":\"source-video double swat; full-monitor escaping pointer; moving pounce; planted running reversal; reacquire after landing; roaming-paused physical hop; menu/drag freeze; missing-pointer cancellation; care ownership; resume\""
        ",\"guard_evidence\":\"menu and drag are in-memory Tick flags with FreezePrimaryMotion, not OS interactions; care uses actual SetMotion(Eat), not a feeding command\""
        ",\"far_cancel_policy\":\"cancel contact immediately; an already launched ballistic hop finishes safely\""
        ",\"characters\":[";
    for(unsigned index=0;index<rows.size();++index) {
        const auto& row=rows[index];if(index)report<<',';
        report<<"{\"character\":"<<static_cast<unsigned>(row.character)<<",\"hops\":"<<row.hops<<",\"landings\":"<<row.landings
            <<",\"peak_lift\":"<<row.peakLift<<",\"awarded_contacts\":"<<row.awards<<",\"duplicate_contact_awards\":"<<row.duplicateAwards
            <<",\"roaming_paused_root_delta\":"<<row.pausedRootDelta<<",\"roaming_paused_airborne_frames\":"<<row.pausedHopFrames
            <<",\"max_frame_root_delta\":"<<row.maxFrameRootDelta<<",\"menu_pause_frames\":"<<row.menuFrames
            <<",\"chase_frames\":"<<row.chaseFrames<<",\"moving_launches\":"<<row.movingLaunches
            <<",\"turn_frames\":"<<row.turnFrames<<",\"pounce_drawings\":"<<row.pounceDrawings
            <<",\"drag_pause_frames\":"<<row.dragFrames<<",\"menu_pixels_held\":true,\"drag_pixels_held\":true"
            <<",\"care_protected_frames\":"<<row.careFrames<<",\"care_interrupted_active_cursor\":true,\"care_ownership_preserved\":true,\"far_cancelled\":true"
            <<",\"resume_paw_attempts\":"<<row.resumePaws<<",\"source_base4_hash\":\""<<row.sourceHash
            <<"\",\"source_immutable\":true,\"all_actor_pixels_inside_canvas\":true,\"profile_unchanged\":true,\"action_entries\":{";
        for(unsigned action=0;action<row.entries.size();++action) {
            if(action)report<<',';report<<'\"'<<ActionName(static_cast<Action>(action))<<"\":"<<row.entries[action];
        }
        report<<"}}";
    }
    report<<"],\"review_images\":[\"review-paw.png\",\"review-chase.png\",\"review-turn.png\",\"review-hop.png\",\"review-care.png\",\"review-resume.png\",\"review-menu.png\",\"review-drag.png\"]}\n";
    if(!WriteBytes(L"render-self-test/cursor-play/trace.json",trace.str())||
            !WriteBytes(L"render-self-test/cursor-play/capture.json",report.str()))return 686;
    context="three coats: source swats, full-monitor pursuit, moving pounce, planted turns, paused hop, once-only credit, menu/drag/care guards; 3600 frames at 60 FPS";
    return 0;
}
} // namespace native_cursor_play_capture

int NativeCursorPlayCapture(HINSTANCE instance) {
    g.instance=instance;
    const HRESULT com=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);if(FAILED(com))return 680;
    ULONG_PTR token=0;GdiplusStartupInput startup;
    if(GdiplusStartup(&token,&startup,nullptr)!=Ok){CoUninitialize();return 680;}
    int result=0;std::string context="cursor play capture initialization";
    try{result=native_cursor_play_capture::Run(context);}catch(...){result=689;}
    WriteBytes(L"render-self-test/cursor-play-report.txt","exit="+std::to_string(result)+" "+context+"\n");
    ReleaseAtlases();ReleaseCarePresentation();g.styleBlendFrom.Reset();g.styleBlendTo.Reset();
    g.initialized=false;g.manualMotionActive=false;g.window=nullptr;cursorInteraction={};
    GdiplusShutdown(token);CoUninitialize();return result;
}
