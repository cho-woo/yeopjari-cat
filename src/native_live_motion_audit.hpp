#pragma once
// Opt-in developer fixture. Real WM_TIMER / Tick / layered HWNDs and desktop
// compositor capture; never loads a profile, registers hotkeys or starts updates.
#include <condition_variable>
#include <deque>
#include <fstream>

namespace cat::companion {
struct LiveMotionAuditAccess {
    static void Start(Model& m,cat::animation::Motion motion,std::uint64_t ticks) {
        m.brain_->Interrupt(ticks);m.storyActive_=true;m.quietActive_=false;
        const auto* art=m.Registered(motion==cat::animation::Motion::Zoomies?cat::sprite_sequence::Clip::Run:cat::sprite_sequence::Clip::Walk);
        const double speed=art?cat::sprite_assets::NominalLocomotionSpeed(*art):40;
        m.SetMotion(motion,ticks,5000,cat::animation::Walking(motion)?speed:0);
        m.explicitTravel_=cat::animation::Walking(motion);
    }
    static std::uint64_t Until(const Model& m){return m.nextDecision_;}
    static std::uint64_t Serial(const Model& m){return m.motionSerial_;}
    static void Fish(Model& m,std::uint64_t ticks){m.SetMotion(cat::animation::Motion::Eat,ticks,
        m.RegisteredDuration(cat::sprite_sequence::Clip::Fish,8000),0);m.feedingKind_=cat::feeding::FoodKind::Fish;}
    static void FinishBall(Model& m,std::uint64_t ticks){if(m.ballPlaying_&&
        (m.ballSession_.phase==cat::toy::Phase::Rest||m.ballSession_.phase==cat::toy::Phase::Wait)){m.ResetControls();m.BeginQuiet(ticks);}}
    static void RepeatToy(Model& m,std::uint64_t ticks){if(Ready(m,ticks)&&ticks>m.motionStart_+1500)m.Perform(cat::sprite_sequence::Clip::ToyPlay,ticks);}
    static bool Ready(const Model& m,std::uint64_t){return !m.Busy()&&
        (m.motion_==cat::animation::Motion::Idle||m.motion_==cat::animation::Motion::Watch)&&
        !m.groundArtBridge_.Starting()&&!m.groundArtBridge_.Stopping();}
};
}
namespace native_live_motion_audit {
constexpr wchar_t ClassName[]=L"YeopjariCat.LiveMotionAudit";
constexpr int Width=1000,Height=620;
HWND backdrop=nullptr;
bool closing=false;
std::function<void()> observeFrame;
ULONGLONG pointerStarted=0;
std::wstring label=L"Starting real-time audit";

LRESULT CALLBACK BackdropProc(HWND window,UINT message,WPARAM wParam,LPARAM lParam) {
    if(message==WM_CLOSE){closing=true;return 0;}
    if(message==WM_PAINT) {
        PAINTSTRUCT ps{};HDC dc=BeginPaint(window,&ps);RECT rect{};GetClientRect(window,&rect);
        HBRUSH background=CreateSolidBrush(RGB(246,242,235));FillRect(dc,&rect,background);DeleteObject(background);
        SetBkMode(dc,TRANSPARENT);SetTextColor(dc,RGB(50,48,57));
        TextOutW(dc,24,22,label.c_str(),static_cast<int>(label.size()));
        constexpr wchar_t note[]=L"Live Windows timer + actual app renderer | first cat + companion | isolated fixture";
        TextOutW(dc,24,50,note,static_cast<int>(std::size(note)-1));EndPaint(window,&ps);return 0;
    }
    return DefWindowProcW(window,message,wParam,lParam);
}
LRESULT CALLBACK ActorProc(HWND window,UINT message,WPARAM wParam,LPARAM lParam) {
    // Audit playback is observational. Accidental input cannot open a menu,
    // move a root or offer food while the timed test script owns both cats.
    if(message==WM_TIMER||message==kFrameTick||message==WM_PAINT||message==WM_ERASEBKGND){
        const auto result=WindowProc(window,message,wParam,lParam);
        if((message==WM_TIMER||message==kFrameTick)&&observeFrame)observeFrame();return result;}
    if(message==WM_NCHITTEST)return HTTRANSPARENT;
    return DefWindowProcW(window,message,wParam,lParam);
}
bool Pointer(POINT& point,bool companion){
    const double age=(GetTickCount64()-pointerStarted)/1000.;
    const auto physical=companion?g.companionModel.Physical():g.physics.CurrentPose();
    const auto section=unsigned(age/10)%6;
    const double x=section==0?0:section==1?42*std::sin(age*18):section==2?280*std::sin(age*.8):
        section==3?32*std::sin(age*22):section==4?180*std::sin(age*2):0;
    const double height=section==3?180:section==4?120:80;
    point={LONG(g.work.left+std::clamp(physical.x+x,100.,900.)),LONG(g.work.bottom-height)};return true;
}
void StartAction(unsigned selected,ULONGLONG ticks) {
    using C=cat::sprite_sequence::Clip;using A=cat::companion::LiveMotionAuditAccess;
    if(selected<32){
        if(selected==1||selected==9||selected==12)RunCommand(selected==1?kManualWalkCommand:selected==9?kManualRunCommand:kManualJumpCommand,ticks);
        else SetMotion(static_cast<Motion>(selected),5000,ticks);
        A::Start(g.companionModel,static_cast<Motion>(selected),ticks);return;
    }
    if(selected<=34){RunCommand(kManualRunCommand,ticks);g.nextDecision=ticks+15000;
        g.companionModel.Perform(C::Run,ticks);return;}
    if(selected==35){StartFlyHunt(ticks);g.companionModel.Perform(C::FlyHunt,ticks);return;}
    if(selected==36){const auto* fish=LoadedCareSprite(C::Fish,g.preferences.character);
        SetMotion(Motion::Eat,fish?cat::sprite_sequence::ProgramDuration(fish->layout->program):8000,ticks);
        g.fishMeal=true;A::Fish(g.companionModel,ticks);return;}
    if(selected<=39){BeginAutonomousRest(ticks);RequestPrimaryFurniture(static_cast<cat::furniture::Kind>(selected-36));
        g.companionModel.RequestFurniture(static_cast<cat::furniture::Kind>(selected-36));return;}
    if(selected==40||selected==41){g.pet.roamingPaused=false;g.companionStore.state.pet.roamingPaused=false;
        PlaceToy(selected==40?ToyKind::Ball:ToyKind::FishingRod);
        g.companionModel.Perform(selected==40?C::BallPlay:C::ToyPlay,ticks);return;}
    if(selected==42){g.cursorPlay=true;g.pet.roamingPaused=false;g.companionStore.state.pet.roamingPaused=false;
        pointerStarted=ticks;liveAuditPointer=Pointer;return;}
    if(selected==43){keyboardPlay.fixture=true;keyboardPlay.fixtureForeground=backdrop;
        BeginKeyboardCapture(ticks);g.companionModel.BeginKeyboard(ticks);return;}
    if(selected==44){StartWallClimb(ticks);g.companionModel.Perform(C::Climb,ticks);return;}
    if(selected==45){g.pet.roamingPaused=false;g.companionStore.state.pet.roamingPaused=false;
        RequestCompanionPlay(ticks);return;}
}
void DriveAction(unsigned selected,ULONGLONG ticks,ULONGLONG begin,bool& stopping) {
    using C=cat::sprite_sequence::Clip;
    const auto age=ticks-begin;
    if(selected>=32&&selected<=34&&age>=2500&&!stopping&&std::abs(g.physics.CurrentPose().velocityX)>40) {
        const auto wanted=selected==32?C::Brake:selected==33?C::Slide:C::Stumble;
        if(g.runningTurn.Active()||!g.companionModel.Perform(wanted,ticks))return;
        for(unsigned seed=0;seed<1000;++seed){auto random=g.random;random.seed(seed);(void)random();
            if(cat::lively::StopClip(random())==wanted){g.random.seed(seed);break;}}
        g.manualMotionActive=false;BeginAutonomousRest(ticks);stopping=true;
    }
    if(selected==43&&!stopping){const unsigned section=unsigned(age/2500)%8;
        const int direction=section==7?0:section%3==0?-1:1;const bool run=section%3!=1;
        ApplyKeyboardPlayInput(VK_LEFT,direction<0);ApplyKeyboardPlayInput(VK_RIGHT,direction>0);ApplyKeyboardPlayInput(VK_SHIFT,run);
        const bool jump=age%10000<35&&age>500;ApplyKeyboardPlayInput(VK_SPACE,jump);
        g.companionModel.KeyboardDrive(direction,run,jump);
    }
    if(selected==41&&age<60000)cat::companion::LiveMotionAuditAccess::RepeatToy(g.companionModel,ticks);
    if((selected==40||selected==41||selected==42||selected==43)&&age>=60000) {
        if(selected==40||selected==41){
            if(g.toy!=ToyKind::None&&g.toySession.phase!=cat::toy::Phase::Interact&&g.toySession.phase!=cat::toy::Phase::Inspect)PlaceToy(ToyKind::None);
            cat::companion::LiveMotionAuditAccess::FinishBall(g.companionModel,ticks);
        }
        if(selected==42){g.cursorPlay=false;liveAuditPointer=nullptr;}
        if(selected==43&&!stopping){StopKeyboardPlay(false,true);keyboardPlay.fixture=false;g.companionModel.EndKeyboard(ticks);}
        stopping=true;
    }
}

// Encoding runs off the UI thread. A bounded queue records drops explicitly,
// rather than allowing a slow encoder to stall animation and fake a defect.
struct Recorder {
    HANDLE input=nullptr,process=nullptr;
    std::mutex mutex;std::condition_variable signal;
    std::mutex regionMutex;POINT captureOrigin{};
    std::atomic<bool> captureStop{false};std::thread captureWorker;
    struct CaptureFrame {unsigned index=0;std::vector<unsigned char> pixels;};
    std::deque<CaptureFrame> queue;
    std::thread worker;bool stop=false,failed=false;
    std::atomic<unsigned> written{0};unsigned dropped=0,occluded=0;
    bool Open(const std::wstring& ffmpeg) {
        if(ffmpeg.empty())return true;
        SECURITY_ATTRIBUTES sa{sizeof(sa),nullptr,TRUE};HANDLE read=nullptr;
        if(!CreatePipe(&read,&input,&sa,0))return false;
        SetHandleInformation(input,HANDLE_FLAG_INHERIT,0);
        HANDLE log=CreateFileW(L"encoder.log",GENERIC_WRITE,FILE_SHARE_READ,&sa,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
        STARTUPINFOW startup{};startup.cb=sizeof(startup);startup.dwFlags=STARTF_USESTDHANDLES;
        startup.hStdInput=read;startup.hStdOutput=startup.hStdError=log;
        auto command=L"\""+ffmpeg+L"\" -hide_banner -loglevel error -y -f rawvideo -pixel_format bgra -video_size "+
            std::to_wstring(Width)+L"x"+std::to_wstring(Height)+L" -framerate 30 -i pipe:0 -an -c:v libx264 -preset veryfast -crf 20 -pix_fmt yuv420p desktop.mp4";
        PROCESS_INFORMATION child{};
        const bool launched=CreateProcessW(ffmpeg.c_str(),command.data(),nullptr,nullptr,TRUE,CREATE_NO_WINDOW,nullptr,nullptr,&startup,&child)!=FALSE;
        CloseHandle(read);if(log!=INVALID_HANDLE_VALUE)CloseHandle(log);
        if(!launched){CloseHandle(input);input=nullptr;return false;}
        process=child.hProcess;CloseHandle(child.hThread);
        worker=std::thread([this]{std::vector<unsigned char> previous;for(;;){CaptureFrame frame;
            {std::unique_lock<std::mutex> lock(mutex);signal.wait(lock,[this]{return stop||!queue.empty();});
                if(queue.empty()&&stop)break;frame=std::move(queue.front());queue.pop_front();}
            while(written<=frame.index){const auto& pixels=written<frame.index&&!previous.empty()?previous:frame.pixels;
                std::size_t offset=0;while(offset<pixels.size()){DWORD bytes=0;
                    if(!WriteFile(input,pixels.data()+offset,static_cast<DWORD>(pixels.size()-offset),&bytes,nullptr)||!bytes){failed=true;break;}
                    offset+=bytes;}if(failed)break;++written;}
            previous=std::move(frame.pixels);if(failed)break;}});
        return true;
    }
    void Capture(Surface& screen,POINT origin,unsigned index) {
        if(!input)return;
        // A moved or covered test window must never record another app's
        // content. Leave a timestamped hold and report the lost observation.
        struct Visibility {RECT area;bool blocked=false;} visibility{{origin.x,origin.y,origin.x+Width,origin.y+Height}};
        if(IsIconic(backdrop)){++occluded;return;}
        EnumWindows([](HWND window,LPARAM data)->BOOL {
            if(window==backdrop)return FALSE;
            if(!IsWindowVisible(window)||IsIconic(window))return TRUE;
            DWORD pid=0;GetWindowThreadProcessId(window,&pid);if(pid==GetCurrentProcessId())return TRUE;
            DWORD cloaked=0;if(SUCCEEDED(DwmGetWindowAttribute(window,DWMWA_CLOAKED,&cloaked,sizeof(cloaked)))&&cloaked)return TRUE;
            auto& v=*reinterpret_cast<Visibility*>(data);RECT bounds{},intersection{};
            if(GetWindowRect(window,&bounds)&&IntersectRect(&intersection,&bounds,&v.area)){v.blocked=true;return FALSE;}
            return TRUE;
        },reinterpret_cast<LPARAM>(&visibility));
        if(visibility.blocked){++occluded;return;}
        DwmFlush();HDC desktop=GetDC(nullptr);BitBlt(screen.dc,0,0,Width,Height,desktop,origin.x,origin.y,SRCCOPY|CAPTUREBLT);ReleaseDC(nullptr,desktop);
        std::lock_guard<std::mutex> lock(mutex);
        if(queue.size()>=3){++dropped;return;}
        CaptureFrame frame;frame.index=index;frame.pixels.assign(screen.pixels,screen.pixels+std::size_t(Width)*Height*4);
        queue.push_back(std::move(frame));signal.notify_one();
    }
    void UpdateOrigin(POINT point){std::lock_guard<std::mutex> lock(regionMutex);captureOrigin=point;}
    void StartCapture(POINT point,ULONGLONG started){if(!input)return;UpdateOrigin(point);
        captureWorker=std::thread([this,started]{Surface screen;if(!screen.Create(Width,Height))return;
            ULONGLONG next=started;
            while(!captureStop.load()){
                const auto ticks=GetTickCount64();
                if(ticks>=next){POINT point{};{std::lock_guard<std::mutex> lock(regionMutex);point=captureOrigin;}
                    const auto index=unsigned((ticks-started)*30/1000);Capture(screen,point,index);
                    next=started+(std::uint64_t(index)+1)*1000/30;
                }else Sleep(1);
            }});
    }
    void Finish(){if(!input)return;captureStop=true;if(captureWorker.joinable())captureWorker.join();
        {std::lock_guard<std::mutex> lock(mutex);stop=true;}signal.notify_all();
        worker.join();CloseHandle(input);input=nullptr;WaitForSingleObject(process,30000);
        DWORD code=1;GetExitCodeProcess(process,&code);failed=failed||code!=0;CloseHandle(process);process=nullptr;}
    ~Recorder(){Finish();}
};

int Run(HINSTANCE instance,unsigned coat,unsigned first,unsigned last,unsigned repeats,const std::wstring& ffmpeg) {
    g.instance=instance;ULONG_PTR token=0;GdiplusStartupInput startup;
    if(GdiplusStartup(&token,&startup,nullptr)!=Ok)return 970;
    const HRESULT com=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
    if(!LoadAtlas())return 971;
    native_manual_motion_tests::Host host;host.Reset(static_cast<cat::Character>(coat),true);
    if(!host.artworkReady)return 971;
    // Every file belongs to this working directory; ordinary records are never read.
    g.directory=L"fixture";CreateDirectoryW(g.directory.c_str(),nullptr);
    g.savePath=g.directory+L"/pet.ini";g.backupPath=g.directory+L"/pet.bak";
    g.nutritionReady=false;g.workInterference=g.cursorPlay=false;
    g.monitor=MonitorFromPoint({0,0},MONITOR_DEFAULTTOPRIMARY);
    WNDCLASSW klass{};klass.lpfnWndProc=BackdropProc;klass.hInstance=instance;klass.lpszClassName=ClassName;
    klass.hCursor=LoadCursorW(nullptr,IDC_ARROW);RegisterClassW(&klass);
    RECT outer{0,0,Width,Height};AdjustWindowRect(&outer,WS_OVERLAPPEDWINDOW,FALSE);
    backdrop=CreateWindowExW(WS_EX_TOPMOST,ClassName,L"옆자리 고양이 · 실시간 모션 검증",WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU|WS_MINIMIZEBOX,
        50,80,outer.right-outer.left,outer.bottom-outer.top,nullptr,nullptr,instance,nullptr);
    ShowWindow(backdrop,SW_SHOWNORMAL);UpdateWindow(backdrop);
    POINT origin{};ClientToScreen(backdrop,&origin);
    g.work={origin.x,origin.y+100,origin.x+Width,origin.y+Height-20};g.dpiScale=1;g.centerX=g.work.left+260;
    ResetMovement();for(unsigned i=0;i<12;++i)g.physics.Update(1./60);
    WNDCLASSW actor{};actor.lpfnWndProc=ActorProc;actor.hInstance=instance;actor.lpszClassName=L"YeopjariCat.LiveAuditActor";RegisterClassW(&actor);
    g.window=CreateWindowExW(WS_EX_LAYERED|WS_EX_TRANSPARENT|WS_EX_TOOLWINDOW|WS_EX_TOPMOST|WS_EX_NOACTIVATE,actor.lpszClassName,
        L"Live audit primary",WS_POPUP,0,0,320,300,backdrop,nullptr,instance,nullptr);
    g.surface.Create(320,300);g.blendFrom.Create(320,300);g.blendTo.Create(320,300);
    auto& state=g.companionStore.state;state=cat::companion::NewState(UtcNow()-366*86400);state.enabled=true;
    state.preferences.character=static_cast<cat::Character>(coat);state.pet.position=.76;state.pet.roamingPaused=true;
    state.nutrition.bodyCondition=50;g.companionStore.outcome=cat::companion::Outcome::Adopted;
    g.companionModel.Bind(state);g.companionDpi=0;g.companionWindow.Create(instance,g.window,PaintCompanion,{});EnsureCompanionWindow();
    std::ostringstream region;region<<"{\"x\":"<<origin.x<<",\"y\":"<<origin.y<<",\"width\":"<<Width<<",\"height\":"<<Height<<"}";
    WriteBytes(L"capture-region.json",region.str());
    const auto started=GetTickCount64();g.lastTick=started;g.lastPhysicsTime=MotionClock();
    g.nextMaintenance=g.nextSave=g.foodRequestAfter=g.updateAnnounceAfter=started+86400000;
    g.nextCuriosity=g.nextFlyHunt=started+86400000;g.perch.nextTry=started+86400000;
    SetMotion(Motion::Idle,86400000,started);g.autonomousResting=true;
    Render();ShowWindow(g.window,SW_SHOWNOACTIVATE);RefreshAnimationTimer();
    Recorder recorder;if(!recorder.Open(ffmpeg))return 972;
    recorder.StartCapture(origin,started);
    std::array<double,6> timings{};liveAuditTimings=&timings;
    std::ofstream trace("trace.csv");trace<<"wall_ms,action,repeat,role,motion,clip,source,x,lift,vx,vy,serial,timer_gap_ms,setup_ms,furniture_ms,companion_ms,dispatch_ms,physics_ms,render_ms,scene,pounce,source_ms,contact_serial,grounded,origin_x,origin_y\n";
    unsigned selected=first,repeat=0,completed=0;bool active=false,stopping=false;ULONGLONG next=started+2000,begin=0,end=0,lastSample=started;
    std::uint64_t maxGap=0,lateFrames=0,frames=0;
    observeFrame=[&]{
        const auto ticks=g.lastTick;
        POINT moved{};ClientToScreen(backdrop,&moved);
        if(moved.x!=origin.x||moved.y!=origin.y){
            const LONG dx=moved.x-origin.x,dy=moved.y-origin.y;
            g.work.left+=dx;g.work.right+=dx;g.work.top+=dy;g.work.bottom+=dy;g.centerX+=dx;
            g.companionWork=g.work;origin=moved;recorder.UpdateOrigin(origin);Render();TickCompanion(ticks,0);
        }
        if(g.lastTick!=lastSample) {
            const auto gap=g.lastTick-lastSample;maxGap=std::max(maxGap,gap);lateFrames+=gap>50;++frames;lastSample=g.lastTick;
            const auto physical=g.physics.RenderPose();const auto pose=PrimaryDisplayedPose(ticks,physical);
            const auto* art=CareSpriteFor(pose,g.preferences.character);
            const auto companion=g.companionModel.View(ticks);const auto* second=CareSpriteFor(companion.animation,state.preferences.character);
            const auto sourceFrame=[&](const cat::animation::Pose& drawing,const CareSpriteAtlas* sprite) {
                if(drawing.ballPounce)return cat::ball_movie::Frame(drawing.spriteElapsedMs);
                if(drawing.furnitureScene!=cat::furniture::Kind::None){
                    const auto* asset=cat::furniture::Find(drawing.furnitureScene,coat);
                    return asset?unsigned(std::min<std::uint64_t>(asset->frames-1,drawing.furnitureElapsedMs*24/1000)):0;}
                return sprite?SpriteFrameFor(drawing,*sprite->layout):unsigned(drawing.frame);
            };
            const auto extra=[&](const cat::animation::Pose& drawing,unsigned contact,bool grounded) {
                trace<<','<<unsigned(drawing.furnitureScene)<<','<<drawing.ballPounce<<','<<
                    (drawing.furnitureScene!=cat::furniture::Kind::None?drawing.furnitureElapsedMs:drawing.spriteElapsedMs)<<','<<contact<<','<<grounded<<','<<origin.x<<','<<origin.y<<'\n';
            };
            trace<<ticks-started<<','<<selected<<','<<repeat<<",0,"<<static_cast<unsigned>(g.motion)<<','<<static_cast<unsigned>(pose.spriteClip)<<','<<
                sourceFrame(pose,art)<<','<<physical.x<<','<<physical.lift<<','<<physical.velocityX<<','<<physical.velocityY<<','<<g.motionSerial<<','<<gap;
            for(double time:timings)trace<<','<<time;extra(pose,g.ballKickSerial,physical.grounded);
            trace<<ticks-started<<','<<selected<<','<<repeat<<",1,"<<static_cast<unsigned>(companion.motion)<<','<<static_cast<unsigned>(companion.animation.spriteClip)<<','<<
                sourceFrame(companion.animation,second)<<','<<companion.physical.x<<','<<companion.physical.lift<<','<<companion.physical.velocityX<<','<<companion.physical.velocityY<<','<<cat::companion::LiveMotionAuditAccess::Serial(g.companionModel)<<','<<gap;
            for(double time:timings)trace<<','<<time;extra(companion.animation,unsigned(cat::companion::BallReviewAccess::Contact(g.companionModel)),companion.physical.grounded);
        }
    };
    while(!closing&&selected<=last) {
        MSG message{};for(unsigned pumped=0;pumped<4&&PeekMessageW(&message,nullptr,0,0,PM_REMOVE);++pumped){
            if(message.message==WM_QUIT){closing=true;break;}TranslateMessage(&message);DispatchMessageW(&message);}
        const auto ticks=GetTickCount64();
        if(!active&&ticks>=next) {
            label=L"Coat "+std::to_wstring(coat)+L" | motion "+std::to_wstring(selected)+L" | repetition "+std::to_wstring(repeat+1);
            InvalidateRect(backdrop,nullptr,FALSE);UpdateWindow(backdrop);
            stopping=false;StartAction(selected,ticks);
            begin=ticks;end=selected<37?std::max(g.nextDecision,cat::companion::LiveMotionAuditAccess::Until(g.companionModel)):
                selected>=40&&selected<=44?ticks+60000:ticks+1000;active=true;
        }
        if(active)DriveAction(selected,ticks,begin,stopping);
        if(active&&ticks>end+2000&&!g.waitingForStop&&!g.manualMotionActive&&!PrimaryAuthoredCareHolding(ticks)&&
                g.physics.RenderPose().grounded&&!g.groundArtBridge.Starting()&&!g.groundArtBridge.Stopping()&&!g.runningTurn.Active()&&
                !g.furniture.Requested()&&!g.companionModel.furniture.Requested()&&g.toy==ToyKind::None&&
                cat::companion::LiveMotionAuditAccess::Ready(g.companionModel,ticks)) {
            g.pet.roamingPaused=state.pet.roamingPaused=true;
            ++completed;if(++repeat>=repeats){repeat=0;++selected;}active=false;next=ticks+1200;
        }
        if(active&&ticks>begin+90000){closing=true;break;}
        Sleep(1);
    }
    KillTimer(g.window,kTickTimer);g.frameTimer.Disable();observeFrame={};liveAuditTimings=nullptr;liveAuditPointer=nullptr;recorder.Finish();
    std::ostringstream report;report<<"{\"version\":\"" YEOPJARI_VERSION_STRING "\",\"real_wall_clock\":true,\"desktop_compositor_capture\":true,\"coat\":"<<coat<<
        ",\"completed\":"<<completed<<",\"requested\":"<<(last-first+1)*repeats<<",\"timer_frames\":"<<frames<<",\"max_timer_gap_ms\":"<<maxGap<<
        ",\"timer_gaps_over_50ms\":"<<lateFrames<<",\"encoded_frames\":"<<recorder.written<<",\"encoder_drops\":"<<recorder.dropped<<",\"occluded_captures\":"<<recorder.occluded<<",\"encoder_failed\":"<<(recorder.failed?"true":"false")<<"}";
    WriteBytes(L"report.json",report.str());
    g.companionWindow.Close();DestroyWindow(g.window);g.window=nullptr;DestroyWindow(backdrop);backdrop=nullptr;
    ReleaseAtlases();ReleaseCarePresentation();g.surface.Reset();g.blendFrom.Reset();g.blendTo.Reset();
    if(g.preciseTimer){timeEndPeriod(1);g.preciseTimer=false;}g.initialized=false;
    GdiplusShutdown(token);if(SUCCEEDED(com))CoUninitialize();
    return completed==(last-first+1)*repeats&&!recorder.failed?0:973;
}

int QueueProbe(HINSTANCE instance) {
    const HRESULT com=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
    std::ostringstream report;report<<"{\"version\":\"" YEOPJARI_VERSION_STRING "\",\"cases\":[";
    bool comma=false;
    for(unsigned coat:{0u,8u,10u})for(auto kind:{cat::furniture::Kind::Tower,cat::furniture::Kind::Box}) {
        cat::furniture::Player player;player.Prepare(instance,kind,coat);
        const auto start=MotionClock();player.Reset();const auto ms=(MotionClock()-start)*1000;
        if(comma)report<<',';comma=true;report<<"{\"coat\":"<<coat<<",\"kind\":"<<unsigned(kind)<<",\"cancel_ms\":"<<ms<<'}';
    }
    report<<"]}";WriteBytes(L"movie-cancel.json",report.str());
    if(SUCCEEDED(com))CoUninitialize();return 0;
}
}
