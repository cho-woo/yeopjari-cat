#pragma once

// Compact actual production review: every60Hz pose is checked/traced, while
// eight PNGs per coat retain the real110px display size on the review board.
namespace native_groom_review {
constexpr unsigned Fps=60;
constexpr int Width=260,Height=240,Floor=220;
using Clip=cat::sprite_sequence::Clip;
std::uint64_t SourceHash(cat::Character character,unsigned frame) {
    const auto& pixels=CurrentArt(character).base.skins[frame].source;
    return cat::Checksum(std::string(reinterpret_cast<const char*>(pixels.data()),pixels.size()*sizeof(pixels[0])));
}
std::array<double,4> Controls(const cat::animation::Pose& pose) {
    return {{pose.cursorReach.rightLift,pose.cursorReach.aimX*pose.cursorReach.rightLift,
        pose.face.blink,pose.joints.tailRaise}};
}
bool Neutral(const cat::animation::Pose& pose) {
    return !cat::cursor_rig::Active(pose.cursorReach)&&cat::deform::Zero(pose.joints)&&cat::expression::Zero(pose.face);
}
int Run(std::string& context) {
    if(!LoadAtlas())return 700;
    for(const auto* path:{L"render-self-test",L"render-self-test/groom-review"})
        if(!CreateDirectoryW(path,nullptr)&&GetLastError()!=ERROR_ALREADY_EXISTS)return 701;
    Surface actor,board;if(!actor.Create(Width,Height)||!board.Create(Width*8,Height*3))return 701;
    {Graphics graphics(board.image.get());graphics.Clear(Color(255,244,242,237));}
    std::ostringstream report,trace;report.imbue(std::locale::classic());trace.imbue(std::locale::classic());
    report<<"{\"schema\":1,\"test\":\"connected-groom-native\",\"status\":\"passed\",\"version\":\"" YEOPJARI_VERSION_STRING
        "\",\"render_source\":\"Windows GDI+ / Box2D\",\"desktop_recording\":false,\"fps\":60,\"app_active_timer_ms\":16,"
        "\"reference_height_px\":110,\"base_resource_id\":101,\"source_frame\":4,\"closed_eye_source_frame\":5,"
        "\"unique_new_full_cat_paintings\":0,\"board\":\"groom-board.png\",\"trace\":\"trace.json\",\"characters\":[";
    trace<<"{\"schema\":1,\"fps\":60,\"frames\":[";
    bool firstTrace=true;unsigned row=0;
    for(const auto character:cat::kAvailableCharacters) {
        native_manual_motion_tests::Host host;host.Reset(character,true);
        g.cursorPlay=false;g.workInterference=false;g.direction=g.facing=1;
        SetMotion(Motion::Groom,5900,host.now);host.Rebase();
        const auto start=host.now,duration=g.nextDecision-g.motionStart;
        context="actual connected Groom character="+std::to_string(static_cast<unsigned>(character))+" duration="+std::to_string(duration);
        if(duration!=7500)return 702;
        const auto saved=host.Saved();const auto sourceHash=SourceHash(character,4),closedHash=SourceHash(character,5);
        const auto& base=CurrentArt(character).base;const auto sourceRect=base.frames[4];
        const double referenceHeight=base.standingHeight,origin=g.physics.RenderPose().x;
        const auto body=cat::appearance::VisualModifiers(g.nutrition,character);
        const std::array<ULONGLONG,8> samples{{0,400,1100,1700,2700,duration-300,duration,duration+250}};
        std::array<std::string,8> images{};
        std::array<double,4> previousControls{};
        double maximumControlDelta=0,maximumLift=0,maximumRootDelta=0,maximumBlink=0,maximumReach=0;
        double maximumPixelDifference=0,maximumCentroidDelta=0,previousCx=0,previousCy=0;
        unsigned activeFrames=0,closedFrames=0,sample=0;
        std::uint64_t startPixels=0,endPixels=0;
        std::vector<BYTE> previousPixels;
        const unsigned count=static_cast<unsigned>((duration+300)*Fps/1000+1);
        bool completed=false;
        for(unsigned index=0;index<count;++index) {
            const auto elapsed=static_cast<ULONGLONG>(index)*1000/Fps;
            if(index)host.Frame(start+elapsed-host.now);
            context="actual connected Groom character="+std::to_string(static_cast<unsigned>(character))+" frame="+std::to_string(index);
            const auto physical=g.physics.RenderPose();const auto pose=PrimaryDisplayedPose(host.now,physical);
            if(pose.spriteClip!=Clip::None||pose.frame!=4||pose.spriteTimed||pose.blendFrame>=0||pose.bellyRig||pose.centerPivot||
                    pose.jumpRig||pose.x!=0||pose.lift!=0||pose.angle!=0||pose.scaleX!=1||pose.scaleY!=1||
                    !cat::deform::Finite(pose.joints)||!physical.grounded||CareSpriteFor(pose,character)||
                    body.bodyWidth!=1||body.bodyHeight!=1)return 703;
            if(elapsed<duration&&g.motion!=Motion::Groom)return 703;
            if(elapsed>=duration)completed=completed||(g.motion==Motion::Idle&&g.autonomousResting);
            maximumRootDelta=std::max(maximumRootDelta,std::abs(physical.x-origin));
            maximumLift=std::max(maximumLift,std::abs(physical.lift));
            maximumBlink=std::max(maximumBlink,pose.face.blink);maximumReach=std::max(maximumReach,pose.cursorReach.rightLift);
            if(cat::cursor_rig::Active(pose.cursorReach))++activeFrames;
            if(pose.face.blink>=.7)++closedFrames;
            const auto controls=Controls(pose);
            if(index)for(unsigned field=0;field<controls.size();++field)
                maximumControlDelta=std::max(maximumControlDelta,std::abs(controls[field]-previousControls[field]));
            previousControls=controls;
            {
                Graphics graphics(actor.image.get());graphics.SetCompositingMode(CompositingModeSourceCopy);
                graphics.Clear(Color(0,0,0,0));graphics.SetCompositingMode(CompositingModeSourceOver);
                graphics.SetInterpolationMode(InterpolationModeHighQualityBicubic);graphics.SetSmoothingMode(SmoothingModeAntiAlias);
                graphics.SetPixelOffsetMode(PixelOffsetModeHighQuality);
                DrawPetStyle(graphics,character,cat::Outfit::None,pose,body,Width*.5f,Floor,110);
                graphics.Flush(FlushIntentionSync);if(graphics.GetLastStatus()!=Ok)return 704;
            }
            if(!native_coat_tests::ValidPixels(reinterpret_cast<const std::uint32_t*>(actor.pixels),Width,Height,true))return 704;
            const auto pixels=cat::Checksum(std::string(reinterpret_cast<const char*>(actor.pixels),Width*Height*4));
            if(!index)startPixels=pixels;
            if(elapsed>=duration) {
                if(!Neutral(pose)||pixels!=startPixels)return 705;
                endPixels=pixels;
            }
            double weight=0,cx=0,cy=0;
            for(unsigned pixel=0;pixel<Width*Height;++pixel) {
                const auto alpha=actor.pixels[pixel*4+3];weight+=alpha;cx+=(pixel%Width)*alpha;cy+=(pixel/Width)*alpha;
            }
            if(weight<=0)return 704;cx/=weight;cy/=weight;
            double pixelDifference=0;
            if(index) {
                double difference=0;
                for(unsigned byte=0;byte<Width*Height*4;++byte)difference+=std::abs(static_cast<int>(actor.pixels[byte])-previousPixels[byte]);
                pixelDifference=difference/(255.*Width*Height*4);
                maximumPixelDifference=std::max(maximumPixelDifference,pixelDifference);
                maximumCentroidDelta=std::max(maximumCentroidDelta,std::hypot(cx-previousCx,cy-previousCy));
            }
            previousPixels.assign(actor.pixels,actor.pixels+Width*Height*4);previousCx=cx;previousCy=cy;
            if(!firstTrace)trace<<',';firstTrace=false;
            trace<<"{\"character\":"<<static_cast<unsigned>(character)<<",\"frame\":"<<index<<",\"elapsed_ms\":"<<elapsed
                <<",\"requested_motion\":"<<static_cast<unsigned>(g.motion)<<",\"root_x\":"<<physical.x<<",\"physical_lift\":"<<physical.lift
                <<",\"source_resource\":101,\"actual_texture_resource\":"<<base.resourceId<<",\"source_frame\":4,\"source_reference_height\":"<<referenceHeight
                <<",\"frame_geometry\":["<<sourceRect.X<<','<<sourceRect.Y<<','<<sourceRect.Width<<','<<sourceRect.Height<<']'
                <<",\"cursor_reach\":["<<pose.cursorReach.rise<<','<<pose.cursorReach.leftLift<<','<<pose.cursorReach.rightLift<<','
                <<pose.cursorReach.aimX<<','<<pose.cursorReach.groom<<','<<pose.cursorReach.lowReach<<']'
                <<",\"blink\":"<<pose.face.blink<<",\"tail_raise\":"<<pose.joints.tailRaise
                <<",\"adjacent_pixel_mae\":"<<pixelDifference<<",\"alpha_centroid\":["<<cx<<','<<cy<<"],\"actor_hash\":\""<<pixels<<"\"}";
            if(sample<samples.size()&&elapsed>=samples[sample]) {
                const auto name="character-"+std::to_string(static_cast<unsigned>(character))+"-sample-"+std::to_string(sample)+".png";
                const auto wide=std::wstring(name.begin(),name.end());
                if(!SaveNativeWalkingReview(*actor.image,(L"render-self-test/groom-review/"+wide).c_str()))return 704;
                Graphics graphics(board.image.get());graphics.DrawImage(actor.image.get(),static_cast<INT>(sample*Width),static_cast<INT>(row*Height));
                Font font(L"Segoe UI",12,FontStyleRegular,UnitPixel);SolidBrush ink(Color(255,40,50,55));
                const auto label=std::wstring(cat::CharacterName(character))+L" / "+std::to_wstring(elapsed)+L" ms";
                graphics.DrawString(label.c_str(),-1,&font,PointF(static_cast<REAL>(sample*Width+8),static_cast<REAL>(row*Height+8)),&ink);
                images[sample]=name;++sample;
            }
        }
        const auto endpoint=cat::cursor::GroomPose(5900,1,5900);
        const auto& currentRect=base.frames[4];
        const bool sourceUnchanged=SourceHash(character,4)==sourceHash&&SourceHash(character,5)==closedHash&&
            base.standingHeight==referenceHeight&&currentRect.X==sourceRect.X&&currentRect.Y==sourceRect.Y&&
            currentRect.Width==sourceRect.Width&&currentRect.Height==sourceRect.Height;
        context+=" rootDelta="+std::to_string(maximumRootDelta)+" controlsDelta="+std::to_string(maximumControlDelta)+
            " blink="+std::to_string(maximumBlink)+" active="+std::to_string(activeFrames)+" completed="+std::to_string(completed);
        if(!completed||sample!=8||!activeFrames||!closedFrames||maximumBlink<.7||maximumReach<.9||maximumControlDelta>.15||
                maximumRootDelta>.01||maximumLift>.01||!sourceUnchanged||!Neutral(endpoint)||host.Saved()!=saved||!endPixels)return 706;
        if(row)report<<',';
        report<<"{\"character\":"<<static_cast<unsigned>(character)<<",\"product_action_duration_ms\":"<<duration
            <<",\"trace_frames\":"<<count<<",\"actual_texture_resource\":"<<base.resourceId<<",\"source_reference_height\":"<<referenceHeight
            <<",\"source_base4_hash\":\""<<sourceHash<<"\",\"closed_eye_base5_hash\":\""<<closedHash<<"\",\"source_immutable\":true"
            <<",\"source_geometry_unchanged\":true,\"profile_unchanged\":true,\"completed_to_idle\":true,\"api_endpoint5900_neutral\":true"
            <<",\"end_pixels_match_start\":true,\"all_actor_pixels_inside_canvas\":true,\"max_root_dx\":"<<maximumRootDelta
            <<",\"max_physical_lift\":"<<maximumLift<<",\"max_blink\":"<<maximumBlink<<",\"active_groom_frames\":"<<activeFrames
            <<",\"closed_eye_frames\":"<<closedFrames<<",\"max_right_paw_lift\":"<<maximumReach<<",\"max_control_delta\":"<<maximumControlDelta
            <<",\"max_adjacent_pixel_mae\":"<<maximumPixelDifference<<",\"max_adjacent_alpha_centroid_delta\":"<<maximumCentroidDelta
            <<",\"samples\":[";
        for(unsigned i=0;i<images.size();++i){if(i)report<<',';report<<'\"'<<images[i]<<'\"';}
        report<<"]}";++row;
    }
    report<<"],\"visual_quality\":\"Source, geometry and continuity checks are separate from visual review of the actual snapshots\"}\n";
    trace<<"]}\n";
    if(!WriteBytes(L"render-self-test/groom-review/report.json",report.str())||!WriteBytes(L"render-self-test/groom-review/trace.json",trace.str())||
            !SaveNativeWalkingReview(*board.image,L"render-self-test/groom-review/groom-board.png"))return 707;
    context="three coats: actual7500ms connected Groom and300ms Idle, 60Hz trace,24 real110px snapshots, same-source head/eye textures and neutral endpoints";
    return 0;
}
} // namespace native_groom_review

int NativeGroomReview(HINSTANCE instance) {
    g.instance=instance;
    const HRESULT com=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);if(FAILED(com))return 700;
    ULONG_PTR token=0;GdiplusStartupInput startup;
    if(GdiplusStartup(&token,&startup,nullptr)!=Ok){CoUninitialize();return 700;}
    int result=0;std::string context="connected Groom review initialization";
    try{result=native_groom_review::Run(context);}catch(...){result=709;}
    WriteBytes(L"render-self-test/groom-review-report.txt","exit="+std::to_string(result)+" "+context+"\n");
    ReleaseAtlases();ReleaseCarePresentation();g.styleBlendFrom.Reset();g.styleBlendTo.Reset();
    g.initialized=false;g.manualMotionActive=false;g.window=nullptr;cursorInteraction={};
    GdiplusShutdown(token);CoUninitialize();return result;
}
