#pragma once

// Samples the shipping idle pose selector, cached character textures and GDI+
// renderer. The menu interval retains the last actor bitmap, just as Tick does;
// this is a native diagnostic, not a desktop recording or generated animation.
namespace native_cute_idle_capture {
constexpr unsigned Fps=60,Count=1200;
constexpr int Width=960,Height=500;
constexpr unsigned PauseFirst=480,PauseLast=510;
using Clip=cat::sprite_sequence::Clip;
struct Frame {
    cat::animation::Pose pose;
    double x=0,lift=0;
    std::uint64_t actorHash=0;
    bool paused=false;
};
struct ProxyBuffers {
    const std::uint32_t *source=nullptr,*deformed=nullptr,*expression=nullptr;
    const Bitmap *sourceImage=nullptr,*image=nullptr,*expressionImage=nullptr;
    std::array<std::size_t,3> sizes{},capacities{};
};
ProxyBuffers Buffers(const SpriteAtlas::Skin& skin) {
    return {skin.source.data(),skin.deformed.data(),skin.expression.data(),
        skin.sourceImage.get(),skin.image.get(),skin.expressionImage.get(),
        {skin.source.size(),skin.deformed.size(),skin.expression.size()},
        {skin.source.capacity(),skin.deformed.capacity(),skin.expression.capacity()}};
}
bool Stable(const SpriteAtlas::Skin& skin,const ProxyBuffers& before) {
    const auto after=Buffers(skin);
    return after.source==before.source&&after.deformed==before.deformed&&after.expression==before.expression&&
        after.sourceImage==before.sourceImage&&after.image==before.image&&after.expressionImage==before.expressionImage&&
        after.sizes==before.sizes&&after.capacities==before.capacities;
}
std::uint64_t SourceHash(const SpriteAtlas::Skin& skin) {
    return cat::Checksum(std::string(reinterpret_cast<const char*>(skin.source.data()),skin.source.size()*4));
}
std::size_t ProxyResidentPixels(const SpriteAtlas& atlas) {
    std::size_t count=0;
    for(const auto& skin:atlas.idleProxies)
        count+=skin.source.capacity()+skin.deformed.capacity()+skin.expression.capacity()+skin.body.capacity()+skin.head.capacity();
    return count;
}
unsigned ProxyEntries(const SpriteAtlas& atlas) {
    unsigned count=0;
    for(const auto& skin:atlas.idleProxies)if(skin.sourceImage&&skin.image&&skin.expressionImage)++count;
    return count;
}
struct Row {
    cat::Character character=cat::Character::Cream;
    cat::nutrition::Visual body;
    std::vector<Frame> frames;
    double startX=0,maxRootDx=0,maxLift=0,maxBlink=0,maxEar=0,maxTail=0,maxPaw=0;
    unsigned changedFrames=0,blinkFrames=0,earFrames=0,tailFrames=0,pawFrames=0;
    unsigned longestStillFrames=0;
    bool pauseHeld=true,resumed=false;
    SpriteAtlas::Skin* proxy=nullptr;
    ProxyBuffers proxyBuffers{};
    std::uint64_t proxyBuildsBefore=0,proxyBuildsAfter=0,proxyLruBuilds=0,originalSourceHash=0;
    int originalWidth=0,originalHeight=0,originalStandingHeight=0,originalResource=0;
    Rect originalFrame{};
    std::size_t originalPixels=0,proxyResidentPixels=0,proxyLimitPixels=0;
    unsigned proxyEntries=0;
};
int PrewarmProxy(Row& row) {
    auto& atlas=CurrentArt(row.character).base;
    const auto& original=atlas.skins[4];
    row.originalWidth=original.width;row.originalHeight=original.height;
    row.originalFrame=atlas.frames[4];row.originalStandingHeight=atlas.standingHeight;row.originalResource=atlas.baseResourceId;
    row.originalPixels=original.source.size();row.originalSourceHash=SourceHash(original);
    row.proxyLimitPixels=2*row.originalPixels*3;
    const auto builds=atlas.idleProxyBuilds;
    // Exercise actual cache eviction at three display buckets, then return to
    // the real110px actor. The cache must retain only two reusable entries.
    for(const double height:{110.,138.,165.,110.}) {
        if(!IdleProxySkin(atlas,height)||ProxyEntries(atlas)>2||ProxyResidentPixels(atlas)>row.proxyLimitPixels)return 670;
    }
    row.proxyLruBuilds=atlas.idleProxyBuilds-builds;
    row.proxy=IdleProxySkin(atlas,110);
    if(!row.proxy||row.proxy==&original||row.proxyLruBuilds<3||ProxyEntries(atlas)!=2||
            row.proxy->width>original.width||row.proxy->height>original.height)return 670;
    row.proxyBuffers=Buffers(*row.proxy);row.proxyBuildsBefore=atlas.idleProxyBuilds;
    return 0;
}
int VerifyProxy(Row& row,bool checkOriginal=false) {
    const auto& atlas=CurrentArt(row.character).base;
    row.proxyBuildsAfter=atlas.idleProxyBuilds;row.proxyEntries=ProxyEntries(atlas);
    row.proxyResidentPixels=ProxyResidentPixels(atlas);
    if(!row.proxy||!Stable(*row.proxy,row.proxyBuffers)||row.proxyBuildsAfter!=row.proxyBuildsBefore||
            row.proxyEntries>2||row.proxyResidentPixels>row.proxyLimitPixels)return 671;
    const auto& frame=atlas.frames[4];
    if(checkOriginal&&(SourceHash(atlas.skins[4])!=row.originalSourceHash||
            atlas.skins[4].width!=row.originalWidth||atlas.skins[4].height!=row.originalHeight||
            atlas.standingHeight!=row.originalStandingHeight||atlas.baseResourceId!=row.originalResource||
            frame.X!=row.originalFrame.X||frame.Y!=row.originalFrame.Y||
            frame.Width!=row.originalFrame.Width||frame.Height!=row.originalFrame.Height))return 672;
    return 0;
}
std::array<double,15> Controls(const cat::animation::Pose& pose) {
    const auto& j=pose.joints;const auto& f=pose.face;
    return {{j.leftEar,j.rightEar,j.headNod,j.frontLeft,j.frontRight,j.tailRaise,j.breath,
        j.crouch,j.pawReachX,j.pawReachY,f.blink,f.wide,f.mouth,f.smile,f.brow}};
}
bool Supported(const cat::animation::Pose& pose) {
    return pose.spriteClip==Clip::None && !pose.spriteTimed && (pose.frame==4||pose.frame==5) &&
        pose.groundContact && !pose.centerPivot && !pose.bellyRig && pose.blendFrame<0 &&
        pose.x==0 && pose.lift==0 && pose.angle==0 && pose.scaleX==1 && pose.scaleY==1 &&
        cat::deform::Finite(pose.joints);
}
int Simulate(Row& row,std::string& context) {
    native_manual_motion_tests::Host host;host.Reset(row.character,true);
    g.workInterference=false;g.facing=g.direction=1;
    SetMotion(Motion::Idle,60000,host.now);host.Rebase();g.groundArtBridge.Reset(host.now);
    const auto saved=host.Saved();const auto start=host.now;
    row.startX=g.physics.RenderPose().x;
    row.body=cat::appearance::VisualModifiers(g.nutrition,row.character);
    row.frames.reserve(Count);
    if(const int proxy=PrewarmProxy(row))return proxy;
    Surface actor;if(!actor.Create(300,220))return 661;
    cat::animation::Pose held;
    std::uint64_t previousHash=0,pauseHash=0;unsigned still=0;
    for(unsigned index=0;index<Count;++index) {
        context="native cute idle character="+std::to_string(static_cast<unsigned>(row.character))+" frame="+std::to_string(index);
        const auto now=start+static_cast<ULONGLONG>(index)*1000/Fps;
        const bool paused=index>=PauseFirst&&index<PauseLast;
        if(index) {
            const auto step=now-host.now;
            if(paused) {host.now=now;g.menuOpen=true;FreezePrimaryMotion(now,step);}
            else {g.menuOpen=false;host.Frame(step);}
        }
        const auto physical=g.physics.RenderPose();
        auto pose=paused?held:PrimaryDisplayedPose(host.now,physical);
        if(!paused)held=pose;
        if(!physical.grounded||!std::isfinite(physical.x)||!std::isfinite(physical.lift)||!Supported(pose))return 662;
        row.maxRootDx=std::max(row.maxRootDx,std::abs(physical.x-row.startX));
        row.maxLift=std::max(row.maxLift,std::abs(physical.lift));
        if(row.maxRootDx>.01||row.maxLift>.01)return 663;
        {
            Graphics graphics(actor.image.get());graphics.SetCompositingMode(CompositingModeSourceCopy);
            graphics.Clear(Color(0,0,0,0));graphics.SetCompositingMode(CompositingModeSourceOver);
            graphics.SetInterpolationMode(InterpolationModeHighQualityBicubic);graphics.SetSmoothingMode(SmoothingModeAntiAlias);
            graphics.SetPixelOffsetMode(PixelOffsetModeHighQuality);
            DrawPetStyle(graphics,row.character,cat::Outfit::None,pose,row.body,150,200,110);
            graphics.Flush(FlushIntentionSync);
            if(graphics.GetLastStatus()!=Ok)return 664;
        }
        if(!native_coat_tests::ValidPixels(reinterpret_cast<const std::uint32_t*>(actor.pixels),300,220,true))return 664;
        if(const int proxy=VerifyProxy(row))return proxy;
        const auto hash=cat::Checksum(std::string(reinterpret_cast<const char*>(actor.pixels),300*220*4));
        if(index&&!paused) {
            if(hash!=previousHash){++row.changedFrames;still=0;}else ++still;
            row.longestStillFrames=std::max(row.longestStillFrames,still);
        }
        if(index==PauseFirst)pauseHash=previousHash;
        if(paused)row.pauseHeld=row.pauseHeld&&hash==pauseHash;
        if(index==PauseLast-1) {
            // Resume the real pose selector at the frozen clock. It must
            // reproduce the retained gesture rather than jump 500 ms ahead.
            g.menuOpen=false;const auto probe=PrimaryDisplayedPose(host.now,physical);g.menuOpen=true;
            const auto expected=Controls(held),actual=Controls(probe);
            if(probe.frame!=held.frame||probe.spriteClip!=held.spriteClip)return 666;
            for(unsigned field=0;field<expected.size();++field)
                if(std::abs(expected[field]-actual[field])>1e-10)return 666;
        }
        if(index>=PauseLast&&hash!=pauseHash)row.resumed=true;
        previousHash=hash;
        const double blink=pose.frame==5?1:pose.face.blink;
        const double ear=std::max(std::abs(pose.joints.leftEar),std::abs(pose.joints.rightEar));
        const double tail=std::abs(pose.joints.tailRaise);
        const double paw=std::max(std::abs(pose.joints.frontLeft),std::abs(pose.joints.frontRight));
        row.maxBlink=std::max(row.maxBlink,blink);row.maxEar=std::max(row.maxEar,ear);
        row.maxTail=std::max(row.maxTail,tail);row.maxPaw=std::max(row.maxPaw,paw);
        if(blink>.05)++row.blinkFrames;if(ear>.05)++row.earFrames;
        if(tail>.05)++row.tailFrames;if(paw>.05)++row.pawFrames;
        row.frames.push_back({pose,physical.x,physical.lift,hash,paused});
    }
    g.menuOpen=false;
    if(host.Saved()!=saved)return 665;
    if(const int proxy=VerifyProxy(row,true))return proxy;
    if(row.maxBlink<.9||row.maxEar<.05||row.maxTail<.05||row.maxPaw<.05||row.changedFrames<30||
            row.longestStillFrames>Fps*6||!row.pauseHeld||!row.resumed)return 666;
    return 0;
}
int Run(std::string& context) {
    if(!LoadAtlas()){context="idle capture atlas load";return 660;}
    for(const auto* path:{L"render-self-test",L"render-self-test/idle-video"})
        if(!CreateDirectoryW(path,nullptr)&&GetLastError()!=ERROR_ALREADY_EXISTS)return 661;
    std::array<Row,2> rows{};rows[0].character=cat::Character::Cream;rows[1].character=cat::Character::Tuxedo;
    for(auto& row:rows)if(const int result=Simulate(row,context))return result;
    Surface canvas;if(!canvas.Create(Width,Height))return 661;
    std::ostringstream trace;trace.imbue(std::locale::classic());
    trace<<"{\"schema\":1,\"fps\":60,\"frames\":[";
    for(unsigned index=0;index<Count;++index) {
        context="native GDI+ cute idle frame="+std::to_string(index);
        {
            Graphics graphics(canvas.image.get());graphics.SetCompositingMode(CompositingModeSourceCopy);
            graphics.Clear(Color(255,246,244,239));graphics.SetCompositingMode(CompositingModeSourceOver);
            graphics.SetCompositingQuality(CompositingQualityHighQuality);
            graphics.SetInterpolationMode(InterpolationModeHighQualityBicubic);
            graphics.SetSmoothingMode(SmoothingModeAntiAlias);graphics.SetPixelOffsetMode(PixelOffsetModeHighQuality);
            graphics.SetTextRenderingHint(TextRenderingHintAntiAliasGridFit);
            Font title(L"Segoe UI",20,FontStyleRegular,UnitPixel),small(L"Segoe UI",12,FontStyleRegular,UnitPixel);
            SolidBrush ink(Color(255,41,53,60));
            graphics.DrawString(L"Cute idle / original design, local gestures",-1,&title,PointF(22,12),&ink);
            graphics.DrawString(L"60 FPS Windows GDI+ + Box2D; 110 px actors. Native diagnostic, not a desktop recording.",-1,
                &small,PointF(23,42),&ink);
            if(index>=PauseFirst&&index<PauseLast)graphics.DrawString(L"Menu pause: actor retained",-1,&small,PointF(700,20),&ink);
            if(index)trace<<',';
            trace<<"{\"frame\":"<<index<<",\"elapsed_ms\":"<<static_cast<unsigned long long>(index)*1000/Fps<<",\"characters\":[";
            for(unsigned rowIndex=0;rowIndex<rows.size();++rowIndex) {
                const auto& row=rows[rowIndex];const auto& frame=row.frames[index];const auto& pose=frame.pose;
                const REAL floor=rowIndex?460.0f:245.0f;
                Pen ground(Color(255,185,196,197),1);graphics.DrawLine(&ground,25.0f,floor+1.0f,935.0f,floor+1.0f);
                graphics.DrawString(rowIndex?L"Tuxedo":L"Cream",-1,&small,PointF(25,floor-152),&ink);
                DrawPetStyle(graphics,row.character,cat::Outfit::None,pose,row.body,480,floor,110);
                if(const int proxy=VerifyProxy(rows[rowIndex]))return proxy;
                if(rowIndex)trace<<',';
                trace<<"{\"character\":"<<static_cast<unsigned>(row.character)<<",\"root_x\":"<<frame.x
                    <<",\"lift\":"<<frame.lift<<",\"sprite_clip\":"<<static_cast<unsigned>(pose.spriteClip)
                    <<",\"sprite_resource\":101,\"sprite_frame\":"<<pose.frame<<",\"reference_height_px\":110"
                    <<",\"blink\":"<<(pose.frame==5?1:pose.face.blink)<<",\"left_ear\":"<<pose.joints.leftEar<<",\"right_ear\":"<<pose.joints.rightEar
                    <<",\"tail\":"<<pose.joints.tailRaise<<",\"left_paw\":"<<pose.joints.frontLeft<<",\"right_paw\":"<<pose.joints.frontRight
                    <<",\"head_nod\":"<<pose.joints.headNod<<",\"breath\":"<<pose.joints.breath
                    <<",\"paused\":"<<(frame.paused?"true":"false")<<",\"actor_hash\":\""<<frame.actorHash<<"\"}";
            }
            trace<<"]}";graphics.Flush(FlushIntentionSync);if(graphics.GetLastStatus()!=Ok)return 667;
        }
        const auto path=L"render-self-test/idle-video/frame-"+std::wstring(5-std::to_wstring(index).size(),L'0')+std::to_wstring(index)+L".png";
        if(!SaveNativeWalkingReview(*canvas.image,path.c_str()))return 667;
    }
    trace<<"]}\n";
    for(auto& row:rows)if(const int proxy=VerifyProxy(row,true))return proxy;
    std::ostringstream metadata;metadata.imbue(std::locale::classic());
    metadata<<"{\"schema\":1,\"version\":\"" YEOPJARI_VERSION_STRING "\",\"render_source\":\"Windows GDI+ / Box2D\","
        "\"desktop_recording\":false,\"fps\":60,\"frame_count\":1200,\"canvas\":[960,500],\"reference_height_px\":110,"
        "\"source_asset\":\"assets/cat.png\",\"source_base_resource_id\":101,\"new_motion_drawings\":0,"
        "\"application_timer_ms\":{\"active_gesture\":16,\"quiet\":40,\"hidden\":250},"
        "\"capture_cadence_note\":\"60 FPS diagnostic sampling; desktop uses adaptive16/40ms and hidden250ms timers\","
        "\"pause_first_frame\":480,\"pause_last_frame_exclusive\":510,\"trace\":\"trace.json\",\"characters\":[";
    for(unsigned index=0;index<rows.size();++index) {
        const auto& row=rows[index];if(index)metadata<<',';
        metadata<<"{\"character\":"<<static_cast<unsigned>(row.character)<<",\"max_root_dx\":"<<row.maxRootDx
            <<",\"max_lift\":"<<row.maxLift<<",\"max_blink\":"<<row.maxBlink<<",\"max_ear\":"<<row.maxEar
            <<",\"max_tail\":"<<row.maxTail<<",\"max_paw\":"<<row.maxPaw<<",\"changed_actor_frames\":"<<row.changedFrames
            <<",\"longest_still_frames\":"<<row.longestStillFrames<<",\"blink_frames\":"<<row.blinkFrames
            <<",\"ear_frames\":"<<row.earFrames<<",\"tail_frames\":"<<row.tailFrames<<",\"paw_frames\":"<<row.pawFrames
            <<",\"idle_proxy_width\":"<<row.proxy->width<<",\"idle_proxy_height\":"<<row.proxy->height
            <<",\"idle_proxy_entries\":"<<row.proxyEntries<<",\"idle_proxy_resident_pixels\":"<<row.proxyResidentPixels
            <<",\"idle_proxy_capacity_limit_pixels\":"<<row.proxyLimitPixels<<",\"original_source_pixels\":"<<row.originalPixels
            <<",\"original_source_width\":"<<row.originalWidth<<",\"original_source_height\":"<<row.originalHeight
            <<",\"original_reference_height\":"<<row.originalStandingHeight
            <<",\"idle_proxy_builds_before\":"<<row.proxyBuildsBefore<<",\"idle_proxy_builds_after\":"<<row.proxyBuildsAfter
            <<",\"idle_proxy_lru_builds\":"<<row.proxyLruBuilds
            <<",\"idle_proxy_buffers_stable\":true,\"idle_proxy_lru_bounded\":true,\"original_source_unchanged\":true,\"original_registration_unchanged\":true"
            <<",\"pause_held\":true,\"pause_clock_preserved\":true,\"resumed\":true,\"source_profile_preserved\":true,\"actor_unclipped\":true}";
    }
    metadata<<"],\"scenario\":\"stable original-design idle; blink, ears, tail and paws; 500ms menu pause; resume\"}\n";
    if(!WriteBytes(L"render-self-test/idle-video/trace.json",trace.str())||
            !WriteBytes(L"render-self-test/idle-video/capture.json",metadata.str()))return 668;
    context="both original coats retain physical support, blink/ears/tail/paws, menu pause and active resume at 60 FPS";return 0;
}
} // namespace native_cute_idle_capture

int NativeCuteIdleCapture(HINSTANCE instance) {
    g.instance=instance;
    const auto com=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);if(FAILED(com))return 660;
    ULONG_PTR token=0;GdiplusStartupInput startup;
    if(GdiplusStartup(&token,&startup,nullptr)!=Ok){CoUninitialize();return 660;}
    int result=0;std::string context="idle capture initialization";
    try{result=native_cute_idle_capture::Run(context);}catch(...){result=669;}
    WriteBytes(L"render-self-test/idle-video-report.txt","exit="+std::to_string(result)+" "+context+"\n");
    ReleaseAtlases();g.styleBlendFrom.Reset();g.styleBlendTo.Reset();
    g.initialized=false;g.manualMotionActive=false;g.menuOpen=false;g.window=nullptr;cursorInteraction={};
    GdiplusShutdown(token);CoUninitialize();return result;
}
