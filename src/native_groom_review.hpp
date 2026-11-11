#pragma once

// Compact actual production review: every60Hz pose is checked/traced, while
// eight PNGs per coat retain the real110px display size on the review board.
namespace native_groom_review {
constexpr unsigned Fps=60;
constexpr int Width=260,Height=240,Floor=220;
using Clip=cat::sprite_sequence::Clip;
int Run(std::string& context) {
    if(!LoadAtlas())return 700;
    for(const auto* path:{L"render-self-test",L"render-self-test/groom-review"})
        if(!CreateDirectoryW(path,nullptr)&&GetLastError()!=ERROR_ALREADY_EXISTS)return 701;
    Surface actor,board;
    if(!actor.Create(Width,Height)||!board.Create(Width*8,Height*3))return 701;
    {Graphics graphics(board.image.get());graphics.Clear(Color(255,244,242,237));}
    std::ostringstream report,trace;
    report.imbue(std::locale::classic());trace.imbue(std::locale::classic());
    report<<"{\"schema\":2,\"test\":\"video-groom-native\",\"status\":\"passed\",\"version\":\""
        YEOPJARI_VERSION_STRING "\",\"render_source\":\"Windows GDI+ / Box2D\",\"desktop_recording\":false,"
        "\"fps\":60,\"reference_height_px\":110,\"board\":\"groom-board.png\",\"characters\":[";
    trace<<"{\"schema\":2,\"fps\":60,\"frames\":[";
    bool firstTrace=true;unsigned row=0;
    for(const auto character:cat::kAvailableCharacters) {
        native_manual_motion_tests::Host host;host.Reset(character,true);
        if(!host.artworkReady)return 702;
        g.cursorPlay=false;g.workInterference=false;g.direction=g.facing=1;
        SetMotion(Motion::Groom,5900,host.now);host.Rebase();
        const auto* sprite=LoadedCareSprite(Clip::Groom,character);
        if(!sprite||std::string_view(sprite->layout->sourcePath).find("assets/video-actions/")!=0)return 702;
        const auto saved=host.Saved();const auto start=host.now;
        const auto duration=g.nextDecision-g.motionStart;
        if(duration<cat::sprite_sequence::ProgramDuration(sprite->layout->program)||duration>8000)return 702;
        const auto body=cat::appearance::VisualModifiers(g.nutrition,character);
        const double origin=g.physics.CurrentPose().x;
        unsigned sample=0,active=0;bool completed=false;
        const std::array<ULONGLONG,8> samples{{0,500,1200,1900,2800,3900,duration-100,duration+250}};
        const unsigned count=static_cast<unsigned>((duration+300)*Fps/1000+1);
        for(unsigned index=0;index<count;++index) {
            const auto elapsed=static_cast<ULONGLONG>(index)*1000/Fps;
            if(index)host.Frame(start+elapsed-host.now);
            const auto physical=g.physics.RenderPose();const auto pose=PrimaryDisplayedPose(host.now,physical);
            context="video Groom coat="+std::to_string(static_cast<unsigned>(character))+" frame="+std::to_string(index);
            if(!physical.grounded||std::abs(physical.x-origin)>.01||std::abs(physical.lift)>.01||host.Saved()!=saved)return 703;
            if(elapsed<duration) {
                if(pose.spriteClip!=Clip::Groom||!pose.spriteTimed||pose.scaleX!=1||pose.scaleY!=1)return 703;
                ++active;
            } else {
                if(g.motion==Motion::Groom)return 705;
                completed=true;
            }
            {
                Graphics graphics(actor.image.get());graphics.SetCompositingMode(CompositingModeSourceCopy);
                graphics.Clear(Color(0,0,0,0));graphics.SetCompositingMode(CompositingModeSourceOver);
                graphics.SetInterpolationMode(InterpolationModeHighQualityBicubic);
                graphics.SetPixelOffsetMode(PixelOffsetModeHighQuality);
                DrawPetStyle(graphics,character,cat::Outfit::None,pose,body,Width*.5f,Floor,110);
                graphics.Flush(FlushIntentionSync);
                if(graphics.GetLastStatus()!=Ok)return 704;
            }
            if(!native_coat_tests::ValidPixels(reinterpret_cast<const std::uint32_t*>(actor.pixels),Width,Height,true))return 704;
            if(!firstTrace)trace<<',';firstTrace=false;
            trace<<"{\"character\":"<<static_cast<unsigned>(character)<<",\"elapsed_ms\":"<<elapsed
                <<",\"source_frame\":"<<(pose.spriteClip==Clip::Groom?SpriteFrameFor(pose,*sprite->layout):0)
                <<",\"grounded\":true,\"root_x\":"<<physical.x<<",\"lift\":"<<physical.lift<<'}';
            if(sample<samples.size()&&elapsed>=samples[sample]) {
                Graphics graphics(board.image.get());graphics.DrawImage(actor.image.get(),static_cast<INT>(sample*Width),static_cast<INT>(row*Height));
                const std::wstring name=L"render-self-test/groom-review/c"+std::to_wstring(static_cast<unsigned>(character))+L"-"+std::to_wstring(sample)+L".png";
                if(!SaveNativeWalkingReview(*actor.image,name.c_str()))return 706;
                ++sample;
            }
        }
        if(!completed||sample!=samples.size()||active==0)return 705;
        if(row)report<<',';
        report<<"{\"character\":"<<static_cast<unsigned>(character)<<",\"duration_ms\":"<<duration
            <<",\"source_frames\":"<<sprite->layout->frameCount<<",\"active_frames\":"<<active
            <<",\"completed\":true,\"save_unchanged\":true}";
        ++row;
    }
    report<<"]}\n";trace<<"]}\n";
    if(!WriteBytes(L"render-self-test/groom-review/report.json",report.str())||
       !WriteBytes(L"render-self-test/groom-review/trace.json",trace.str())||
       !SaveNativeWalkingReview(*board.image,L"render-self-test/groom-review/groom-board.png"))return 706;
    context="three coats: registered Dropshot Groom, actual production action completion, fixed ground root and unchanged saves";
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
