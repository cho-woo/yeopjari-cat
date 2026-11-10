#pragma once

// Included inside native_manual_motion_tests, after its production Host.
namespace jump_continuity {
using Clip=cat::sprite_sequence::Clip;
constexpr unsigned Fps=60,Frames=420;
struct Alpha {
    int left=420,top=320,right=-1,bottom=-1;
    double x=0,y=0;
};
bool BoundedIdleGestures(const cat::animation::Pose& pose) {
    const auto& j=pose.joints;const auto& face=pose.face;
    const auto within=[](double value,double low,double high) {
        return std::isfinite(value)&&value>=low-1e-12&&value<=high+1e-12;
    };
    return cat::deform::Finite(j)&&within(j.leftEar,0,.48)&&within(j.rightEar,0,.43)&&
        within(j.frontLeft,0,.85)&&within(j.frontRight,0,.80)&&within(j.tailRaise,0,.44)&&
        within(j.headNod,-.10,.10)&&within(j.breath,-.20,.20)&&
        j.crouch==0&&j.pawReachX==0&&j.pawReachY==0&&j.pawAnchorX==0&&
        within(face.blink,0,1)&&face.wide==0&&face.mouth==0&&face.smile==0&&face.brow==0;
}
Alpha Bounds(const Surface& surface) {
    Alpha result;double weight=0;
    for(int y=0;y<surface.height;++y)for(int x=0;x<surface.width;++x) {
        const unsigned a=surface.pixels[(y*surface.width+x)*4+3];
        if(a<=32)continue;
        result.left=std::min(result.left,x);result.top=std::min(result.top,y);
        result.right=std::max(result.right,x);result.bottom=std::max(result.bottom,y);
        result.x+=x*a;result.y+=y*a;weight+=a;
    }
    if(weight){result.x/=weight;result.y/=weight;}
    return result;
}
int Run() {
    for(const auto* path:{L"render-self-test",L"render-self-test/jump-continuity"})
        if(!CreateDirectoryW(path,nullptr)&&GetLastError()!=ERROR_ALREADY_EXISTS)return 580;
    const auto* walk=LoadedCareSprite(Clip::Walk,cat::Character::Cream);
    const auto* jump=LoadedCareSprite(Clip::Jump,cat::Character::Cream);
    if(!walk||!jump||!cat::video_ground_bridge::JumpTurns(*walk->layout).valid)return 581;
    Surface frame,board;if(!frame.Create(420,320)||!board.Create(2420,760))return 580;
    {
        Graphics graphics(board.image.get());graphics.Clear(Color(255,247,245,240));
    }
    std::ostringstream trace,report;trace.imbue(std::locale::classic());report.imbue(std::locale::classic());
    trace<<"{\"schema\":1,\"fps\":60,\"frames\":[";
    report<<"{\"schema\":1,\"test\":\"jump-continuity-native\",\"status\":\"passed\",\"version\":\""
        YEOPJARI_VERSION_STRING "\",\"render_source\":\"Windows GDI+ / Box2D\",\"desktop_recording\":false,"
        "\"fps\":60,\"frames_per_case\":420,\"trace\":\"trace.json\",\"board\":\"entry-exit-board.png\",\"cases\":[";
    bool firstTrace=true;unsigned caseIndex=0;
    for(const auto character:{cat::Character::Cream,cat::Character::Tuxedo})for(const int facing:{1,-1})for(const bool moving:{false,true}) {
        Host host;host.Reset(character,true);g.workInterference=false;
        g.work={0,0,1280,900};g.dpiScale=1;g.direction=g.facing=facing;
        g.centerX=facing>0?400:900;ResetMovement();
        for(unsigned settle=0;settle<12;++settle)g.physics.Update(1./60);
        SetMotion(Motion::Idle,8000,host.now);host.Rebase();
        const auto start=host.now;const double origin=g.physics.CurrentPose().x;
        const auto body=cat::appearance::VisualModifiers(g.nutrition,g.preferences.character);
        bool entry=false,sideEntry=false,flight=false,landing=false,returned=false,neutral=false,locked=false;
        double maxLift=0,maxRootDx=0,jumpOrigin=0,previousX=origin,previousVelocity=0,maxDelta=0,maxIntegrationError=0;
        ULONGLONG previousElapsed=0;unsigned captured=0;
        for(unsigned i=0;i<Frames;++i) {
            const auto elapsed=static_cast<ULONGLONG>(i)*1000/Fps;
            if(i)host.Frame(start+elapsed-host.now);
            if(i==30)host.Command(moving?kManualWalkCommand:kManualJumpCommand);
            if(moving&&i==120)host.Command(kManualJumpCommand);
            const auto physical=g.physics.CurrentPose(),rendered=g.physics.RenderPose();
            const auto pose=PrimaryDisplayedPose(host.now,rendered);
            const auto* asset=CareSpriteFor(pose,character);
            const bool referenceNeutral=!asset&&pose.spriteClip==Clip::None&&pose.frame==4&&
                rendered.grounded&&g.groundArtBridge.Neutral()&&!g.waitingForStop&&!g.jumpPending&&
                (g.motion==Motion::Idle||g.motion==Motion::Watch)&&std::abs(WalkingVelocity(rendered))<=.5&&
                pose.x==0&&pose.lift==0&&pose.angle==0&&pose.scaleX==1&&pose.scaleY==1&&
                !pose.spriteTimed&&!pose.bellyRig&&!pose.centerPivot&&pose.blendFrame<0&&BoundedIdleGestures(pose);
            if(!asset&&!referenceNeutral)return 582;
            if(referenceNeutral&&host.now>=g.motionStart&&host.now-g.motionStart<=350&&
                    (!cat::deform::Zero(pose.joints)||!cat::expression::Zero(pose.face)))return 582;
            const unsigned source=asset?SpriteFrameFor(pose,*asset->layout):4;
            if(asset&&source>=asset->layout->frameCount)return 582;
            const auto& base=CurrentArt(character).base;const auto& baseRect=base.frames[4];
            const cat::sprite_assets::Frame registration=asset?asset->layout->frames[source]:
                cat::sprite_assets::Frame{{baseRect.X,baseRect.Y,baseRect.Width,baseRect.Height},
                    {baseRect.Width/2.,static_cast<double>(baseRect.Height)}};
            const auto resource=asset?asset->layout->resourceId:101;
            const double referenceHeight=asset?asset->layout->referenceHeight:base.standingHeight;
            if(!locked && g.jumpPending && pose.spriteClip==Clip::Jump &&
                    std::abs(WalkingVelocity(physical))<=.5&&std::abs(WalkingVelocity(rendered))<=.5) {
                locked=true;jumpOrigin=rendered.x;
            }
            if(locked)maxRootDx=std::max(maxRootDx,std::abs(rendered.x-jumpOrigin));
            const double delta=rendered.x-previousX,dt=(elapsed-previousElapsed)/1000.;
            const double expected=(previousVelocity+rendered.velocityX)*.5*dt;
            maxDelta=std::max(maxDelta,std::abs(delta));maxIntegrationError=std::max(maxIntegrationError,std::abs(delta-expected));
            if(!std::isfinite(rendered.x)||!std::isfinite(rendered.lift)||maxRootDx>.01||std::abs(delta-expected)>.35)return 583;
            previousX=rendered.x;previousVelocity=rendered.velocityX;previousElapsed=elapsed;
            maxLift=std::max(maxLift,physical.lift);
            if(rendered.grounded&&pose.spriteClip==Clip::Jump&&
                    pose.spriteOptions.jump==cat::sprite_sequence::JumpPhase::Airborne)return 584;
            const bool entering=g.jumpPending&&pose.spriteClip==Clip::Walk&&g.groundArtBridge.StartingForSideAction();
            const bool returning=!g.jumpPending&&g.landedAt&&pose.spriteClip==Clip::Walk&&g.groundArtBridge.Stopping();
            entry=entry||entering;flight=flight||!rendered.grounded;landing=landing||physical.landed;
            sideEntry=sideEntry||(entering&&source>=48&&source<=55);
            if(returning) {
                if(host.now<g.landedAt || host.now-g.landedAt<
                        cat::sprite_sequence::Duration(jump->layout->program,jump->layout->program.outro))return 585;
                returned=true;
            }
            neutral=neutral||(returned&&referenceNeutral&&!g.manualMotionActive);
            const auto placement=cat::render::BuildPlacement(900,300,1,rendered.lift,pose.lift,1);
            const int inset=asset?cat::render::SpriteBottomSafetyInset(*asset->layout,110,body.bodyHeight):0;
            const auto reserved=cat::render::WithBottomSafetyInset(placement,inset);
            if(std::abs(reserved.windowTop+reserved.localBottom-(899-rendered.lift))>1e-8)return 586;
            {
                Graphics graphics(frame.image.get());graphics.SetCompositingMode(CompositingModeSourceCopy);
                graphics.Clear(Color(0,0,0,0));graphics.SetCompositingMode(CompositingModeSourceOver);
                graphics.SetInterpolationMode(InterpolationModeHighQualityBicubic);graphics.SetPixelOffsetMode(PixelOffsetModeHighQuality);
                DrawPetStyle(graphics,character,cat::Outfit::None,pose,body,
                    static_cast<REAL>(210+rendered.x-origin),static_cast<REAL>(280-rendered.lift),110);
                graphics.Flush(FlushIntentionSync);
            }
            const auto alpha=Bounds(frame);if(alpha.right<alpha.left||alpha.bottom<alpha.top)return 587;
            if(!firstTrace)trace<<',';firstTrace=false;
            trace<<"{\"case\":"<<caseIndex<<",\"frame\":"<<i<<",\"elapsed_ms\":"<<elapsed
                <<",\"character\":"<<static_cast<unsigned>(character)<<",\"facing\":"<<facing
                <<",\"scenario\":\""<<(moving?"walk-jump":"idle-jump")<<"\",\"jump_root_locked\":"<<(locked?"true":"false")
                <<",\"root_delta_x\":"<<delta<<",\"predicted_delta_x\":"<<expected
                <<",\"physical_x\":"<<physical.x<<",\"physical_lift\":"<<physical.lift
                <<",\"rendered_x\":"<<rendered.x<<",\"rendered_lift\":"<<rendered.lift
                <<",\"grounded\":"<<(rendered.grounded?"true":"false")<<",\"jump_pending\":"<<(g.jumpPending?"true":"false")
                <<",\"sprite_clip\":"<<static_cast<unsigned>(pose.spriteClip)<<",\"sprite_resource\":"<<resource
                <<",\"sprite_frame\":"<<source<<",\"anchor\":["<<registration.anchor.x<<','<<registration.anchor.y
                <<"],\"reference_height\":"<<referenceHeight<<",\"reference_neutral\":"<<(referenceNeutral?"true":"false")
                <<",\"idle_gestures\":"<<(referenceNeutral&&(!cat::deform::Zero(pose.joints)||!cat::expression::Zero(pose.face))?"true":"false")
                <<",\"alpha_bbox\":["<<alpha.left<<','<<alpha.top<<','<<alpha.right<<','<<alpha.bottom
                <<"],\"alpha_centroid\":["<<alpha.x<<','<<alpha.y<<"],\"entry_turn\":"<<(entering?"true":"false")
                <<",\"return_turn\":"<<(returning?"true":"false")<<'}';
            if(i%30==0 || (entering&&(source==29||source==55)) || returning || physical.landed) {
                const auto name=L"render-self-test/jump-continuity/case-"+std::to_wstring(caseIndex)+L"-"+std::to_wstring(i)+L".png";
                if(!SaveNativeWalkingReview(*frame.image,name.c_str()))return 587;++captured;
            }
        }
        if(!entry||!flight||!landing||!returned||!neutral||!locked||maxLift<30||(moving&&!sideEntry))return 588;
        if(caseIndex)report<<',';
        report<<"{\"character\":"<<static_cast<unsigned>(character)<<",\"facing\":"<<facing
            <<",\"scenario\":\""<<(moving?"walk-jump":"idle-jump")<<"\",\"entry_from_side_turn_seen\":"<<(sideEntry?"true":"false")
            <<",\"root_x_unchanged\":true,\"physical_height_only_once\":true,\"no_prelaunch_air_pose\":true,"
            "\"complete_landing_before_return\":true,\"entry_turn_seen\":true,\"return_turn_seen\":true,\"grounded_return\":true,\"reference_neutral_seen\":true,"
            "\"max_root_dx\":"<<maxRootDx<<",\"max_frame_root_delta\":"<<maxDelta<<",\"max_integration_error\":"<<maxIntegrationError
            <<",\"max_lift\":"<<maxLift<<",\"png_count\":"<<captured<<'}';
        if(!moving) {
            Graphics graphics(board.image.get());graphics.SetInterpolationMode(InterpolationModeHighQualityBicubic);
            graphics.SetPixelOffsetMode(PixelOffsetModeHighQuality);Font label(L"Segoe UI",11,FontStyleRegular,UnitPixel);
            SolidBrush ink(Color(255,40,50,55));Pen floor(Color(255,170,180,185));
            const Clip clips[]{Clip::None,Clip::Walk,Clip::Walk,Clip::Walk,Clip::Walk,Clip::Jump,Clip::Jump,Clip::Jump,Clip::Walk,Clip::Walk,Clip::None};
            const unsigned sources[]{4,20,29,48,55,0,2,15,57,60,4};
            for(unsigned column=0;column<11;++column) {
                auto pose=cat::animation::Pose{};pose.spriteClip=clips[column];pose.spriteFrame=sources[column];pose.flip=facing<0;
                if(clips[column]==Clip::None)pose.frame=4;
                cat::animation::UseAuthoredSprite(pose);const REAL center=110+220*column,bottom=165+190*(caseIndex/2);
                graphics.DrawLine(&floor,center-100,bottom+1,center+100,bottom+1);
                DrawPetStyle(graphics,character,cat::Outfit::None,pose,body,center,bottom,110);
                const auto text=(character==cat::Character::Cream?L"Cream ":L"Tuxedo ")+std::wstring(facing>0?L"right ":L"left ")+
                    (clips[column]==Clip::Walk?L"Walk ":clips[column]==Clip::Jump?L"Jump ":L"Base ")+std::to_wstring(sources[column]);
                graphics.DrawString(text.c_str(),-1,&label,PointF(center-95,bottom+5),&ink);
            }
        }
        ++caseIndex;
    }
    trace<<"]}\n";report<<"]}\n";
    return WriteBytes(L"render-self-test/jump-continuity/trace.json",trace.str())&&
        WriteBytes(L"render-self-test/jump-continuity/report.json",report.str())&&
        SaveNativeWalkingReview(*board.image,L"render-self-test/jump-continuity/entry-exit-board.png")?0:589;
}
} // namespace jump_continuity
