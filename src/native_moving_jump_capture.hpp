#pragma once

// Production request, motor, Box2D, displayed pose and GDI+ painter. Samples
// existing artwork; this is neither generated source art nor desktop footage.
namespace native_moving_jump_capture {
constexpr unsigned SimulationFps=60,OutputFps=30,Steps=600;
constexpr int Width=1280,Height=320;
int Run() {
    CreateDirectoryW(L"render-self-test",nullptr);
    CreateDirectoryW(L"render-self-test/moving-jump",nullptr);
    Surface frame;if(!frame.Create(Width,Height))return 810;
    std::ostringstream trace;trace.imbue(std::locale::classic());
    trace<<"{\"schema\":1,\"simulation_fps\":60,\"fps\":30,\"frames\":[";
    unsigned written=0;bool first=true;
    for(unsigned scenario=0;scenario<2;++scenario) {
        native_manual_motion_tests::keyboard_native::Clear();
        native_manual_motion_tests::Host host;host.Reset(cat::Character::Cream,true);
        g.work={0,0,2200,900};g.dpiScale=1;g.centerX=200;g.direction=g.facing=1;
        g.cursorPlay=false;g.workInterference=false;ResetMovement();
        for(unsigned i=0;i<12;++i)g.physics.Update(1./60);
        SetMotion(Motion::Idle,8000,host.now);host.Rebase();
        const auto start=host.now;const double origin=g.physics.CurrentPose().x;
        const auto body=cat::appearance::VisualModifiers(g.nutrition,g.preferences.character);
        bool flight=false,landed=false,resumed=false;double takeoffX=0,airSpeed=0,airDistance=0,maxDrift=0,peak=0;
        for(unsigned i=0;i<Steps;++i) {
            const auto elapsed=static_cast<ULONGLONG>(i)*1000/SimulationFps;
            if(i)host.Frame(start+elapsed-host.now);
            if(i==30)host.Command(scenario?kManualRunCommand:kManualWalkCommand);
            if(i==150)host.Command(kManualJumpCommand);
            const auto physical=g.physics.CurrentPose(),displayed=g.physics.RenderPose();
            const auto pose=PrimaryDisplayedPose(host.now,displayed);
            peak=std::max(peak,physical.lift);
            if(!physical.grounded) {
                if(!flight){flight=true;takeoffX=physical.x;airSpeed=physical.velocityX;}
                maxDrift=std::max(maxDrift,std::abs(physical.velocityX-airSpeed));
            }
            if(flight&&physical.landed) {
                landed=true;airDistance=physical.x-takeoffX;
                resumed=cat::animation::Walking(g.motion)&&!g.waitingForStop&&!g.groundArtBridge.Stopping();
            }
            if(i%2)continue;
            {
                Graphics graphics(frame.image.get());graphics.Clear(Color(255,247,247,244));
                graphics.SetInterpolationMode(InterpolationModeHighQualityBicubic);
                graphics.SetPixelOffsetMode(PixelOffsetModeHighQuality);
                Pen floor(Color(255,175,183,186));graphics.DrawLine(&floor,0,261,Width,261);
                for(int x=0;x<Width;x+=50)graphics.DrawLine(&floor,x,261,x,266);
                Font label(L"Segoe UI",14,FontStyleRegular,UnitPixel);SolidBrush ink(Color(255,45,55,60));
                graphics.DrawString(scenario?L"RUN > FORWARD JUMP > RUN":L"WALK > FORWARD JUMP > WALK",-1,&label,PointF(20,15),&ink);
                graphics.DrawString(L"Current artwork | native GDI+ / Box2D | 60 Hz simulation, 30 FPS capture | not desktop footage",-1,
                    &label,PointF(20,38),&ink);
                DrawPetStyle(graphics,cat::Character::Cream,cat::Outfit::None,pose,body,
                    static_cast<REAL>(200+displayed.x-origin),static_cast<REAL>(260-displayed.lift),110);
                graphics.Flush(FlushIntentionSync);
            }
            wchar_t name[160]{};swprintf_s(name,L"render-self-test/moving-jump/frame-%05u.png",written);
            if(!SaveNativeWalkingReview(*frame.image,name))return 811;
            if(!first)trace<<',';first=false;
            trace<<"{\"frame\":"<<written<<",\"scenario\":"<<scenario<<",\"simulation_frame\":"<<i
                <<",\"elapsed_ms\":"<<elapsed<<",\"physical_x\":"<<physical.x<<",\"rendered_x\":"<<displayed.x
                <<",\"lift\":"<<physical.lift<<",\"velocity_x\":"<<physical.velocityX<<",\"velocity_y\":"<<physical.velocityY
                <<",\"grounded\":"<<(physical.grounded?"true":"false")<<",\"landed\":"<<(physical.landed?"true":"false")
                <<",\"moving_jump\":"<<(g.runningJump.active?"true":"false")<<",\"waiting_for_stop\":"<<(g.waitingForStop?"true":"false")<<'}';
            ++written;
        }
        if(!flight||!landed||!resumed||peak<30||airSpeed<8||airDistance<airSpeed*.55||maxDrift>1)return 812;
    }
    trace<<"]}\n";
    const std::string metadata="{\"schema\":1,\"version\":\"" YEOPJARI_VERSION_STRING
        "\",\"render_source\":\"Windows GDI+ / Box2D\",\"desktop_recording\":false,\"fps\":30,"
        "\"simulation_fps\":60,\"frame_count\":600,\"canvas\":[1280,320],\"display_height_logical_px\":110,"
        "\"scenario\":\"10s walk-forward-jump-walk; 10s run-forward-jump-run\","
        "\"artwork\":\"Dropshot reference-conditioned jump video frames\",\"new_painted_frames\":0,\"new_video_frames\":62}\n";
    return WriteBytes(L"render-self-test/moving-jump/trace.json",trace.str())&&
        WriteBytes(L"render-self-test/moving-jump/capture.json",metadata)?0:813;
}
}
int NativeMovingJumpCapture(HINSTANCE instance) {
    g.instance=instance;ULONG_PTR token=0;GdiplusStartupInput startup;
    if(GdiplusStartup(&token,&startup,nullptr)!=Ok)return 809;
    const int result=LoadAtlas()?native_moving_jump_capture::Run():810;
    WriteBytes(L"render-self-test/moving-jump-report.txt","exit="+std::to_string(result)+" native moving jump capture\n");
    ReleaseAtlases();GdiplusShutdown(token);return result;
}
