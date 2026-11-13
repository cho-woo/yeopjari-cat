#pragma once
#include <uiautomationclient.h>

// A disposable real layered HWND, actual Win32 popup and separate MTA/UIA
// observer. No global input, user-profile loading, updater or tray is used.
namespace native_menu_visibility_tests {
constexpr UINT InspectMessage=WM_APP+0x371,CancelMessage=WM_APP+0x372;
struct Probe {
    HWND owner=nullptr,popup=nullptr;
    std::atomic<bool> ready{false},sampled{false};
    std::atomic<int> error{0};
    RECT popupRect{};
    bool visible=false,hidden=false,closing=false,menuOpen=false;
    std::size_t alpha=0,uncovered=0;
    std::wstring screenshot;
};
Probe* active=nullptr;

std::size_t Alpha() {
    std::size_t count=0;
    if(g.surface.pixels)for(int i=0;i<g.surface.width*g.surface.height;++i)
        count+=g.surface.pixels[static_cast<std::size_t>(i)*4+3]>=64;
    return count;
}
bool DesktopScreenshot(RECT area,HWND owner,const std::wstring& path) {
    MONITORINFO monitor{};monitor.cbSize=sizeof(monitor);
    GetMonitorInfoW(MonitorFromWindow(owner,MONITOR_DEFAULTTONEAREST),&monitor);
    IntersectRect(&area,&area,&monitor.rcMonitor);
    Surface desktop;
    if(area.right<=area.left||area.bottom<=area.top||!desktop.Create(area.right-area.left,area.bottom-area.top))return false;
    DwmFlush();
    HDC screen=GetDC(nullptr);
    const bool copied=BitBlt(desktop.dc,0,0,desktop.width,desktop.height,screen,area.left,area.top,SRCCOPY|CAPTUREBLT)!=FALSE;
    ReleaseDC(nullptr,screen);
    for(int i=0;i<desktop.width*desktop.height;++i)desktop.pixels[static_cast<std::size_t>(i)*4+3]=255;
    return copied&&SaveNativeWalkingReview(*desktop.image,path.c_str());
}
BOOL CALLBACK FindPopup(HWND window,LPARAM value) {
    DWORD pid=0;GetWindowThreadProcessId(window,&pid);
    wchar_t name[32]{};GetClassNameW(window,name,32);
    if(pid==GetCurrentProcessId()&&IsWindowVisible(window)&&std::wcscmp(name,L"#32768")==0) {
        *reinterpret_cast<HWND*>(value)=window;return FALSE;
    }
    return TRUE;
}
void Observe(Probe& probe) {
    const HRESULT initialized=CoInitializeEx(nullptr,COINIT_MULTITHREADED);
    IUIAutomation* automation=nullptr;
    const GUID clsid{0xff48dba4,0x60ef,0x4201,{0xaa,0x87,0x54,0x10,0x3e,0xef,0x59,0x4e}};
    if(FAILED(initialized)||FAILED(CoCreateInstance(clsid,nullptr,CLSCTX_INPROC_SERVER,
            __uuidof(IUIAutomation),reinterpret_cast<void**>(&automation))))probe.error=621;
    if(automation) {
        IUIAutomation2* bounded=nullptr;
        if(SUCCEEDED(automation->QueryInterface(__uuidof(IUIAutomation2),reinterpret_cast<void**>(&bounded)))) {
            bounded->put_ConnectionTimeout(1000);bounded->put_TransactionTimeout(1000);bounded->Release();
        }
    }
    const auto deadline=GetTickCount64()+4000;
    while(!probe.error&&GetTickCount64()<deadline) {
        HWND popup=nullptr;EnumWindows(FindPopup,reinterpret_cast<LPARAM>(&popup));
        if(popup) {probe.popup=popup;break;}
        Sleep(10);
    }
    if(!probe.error&&!probe.popup)probe.error=622;
    if(!probe.error) {
        IUIAutomationElement* element=nullptr;BOOL offscreen=TRUE;int pid=0;
        if(FAILED(automation->ElementFromHandle(probe.popup,&element))||!element)probe.error=623;
        else {
            if(FAILED(element->get_CurrentIsOffscreen(&offscreen))||offscreen||
                    FAILED(element->get_CurrentProcessId(&pid))||pid!=static_cast<int>(GetCurrentProcessId())||
                    FAILED(element->get_CurrentBoundingRectangle(&probe.popupRect))||
                    probe.popupRect.right<=probe.popupRect.left||probe.popupRect.bottom<=probe.popupRect.top)probe.error=623;
            element->Release();
        }
    }
    if(automation)automation->Release();
    if(SUCCEEDED(initialized))CoUninitialize();
    probe.ready.store(true,std::memory_order_release);
    PostMessageW(probe.owner,InspectMessage,0,0);
    const auto sampleDeadline=GetTickCount64()+2000;
    while(!probe.sampled.load(std::memory_order_acquire)&&GetTickCount64()<sampleDeadline)Sleep(5);
    if(!probe.sampled&&!probe.error)probe.error=624;
    PostMessageW(probe.owner,CancelMessage,0,0);
}
LRESULT CALLBACK Procedure(HWND window,UINT message,WPARAM wParam,LPARAM lParam) {
    if(message==InspectMessage&&active&&active->ready.load(std::memory_order_acquire)) {
        auto& p=*active;p.visible=IsWindowVisible(window)!=FALSE;
        p.hidden=g.hidden;p.closing=g.closing;p.menuOpen=g.menuOpen;p.alpha=Alpha();
        RECT actor{};GetWindowRect(window,&actor);
        for(int y=0;y<g.surface.height;++y)for(int x=0;x<g.surface.width;++x)
            if(g.surface.pixels[(static_cast<std::size_t>(y)*g.surface.width+x)*4+3]>=64) {
                const POINT point{actor.left+x,actor.top+y};
                if(!PtInRect(&p.popupRect,point))++p.uncovered;
            }
        RECT area{};UnionRect(&area,&actor,&p.popupRect);
        if(!DesktopScreenshot(area,window,p.screenshot))p.error=625;
        p.sampled.store(true,std::memory_order_release);return 0;
    }
    if(message==CancelMessage){EndMenu();return 0;}
    // An unexpected menu result cannot persist or destroy an ordinary profile.
    if(message==WM_CLOSE){g.closing=true;ShowWindow(window,SW_HIDE);return 0;}
    if(message==WM_DESTROY)return 0;
    return WindowProc(window,message,wParam,lParam);
}
void Pump(unsigned milliseconds) {
    const auto deadline=GetTickCount64()+milliseconds;
    while(GetTickCount64()<deadline) {
        MSG message{};
        while(PeekMessageW(&message,nullptr,0,0,PM_REMOVE)){TranslateMessage(&message);DispatchMessageW(&message);}
        Sleep(2);
    }
}
int Run(HINSTANCE instance,std::string& context) {
    if(!LoadAtlas())return 620;
    for(const auto* path:{L"render-self-test",L"render-self-test/menu-visibility",L"render-self-test/menu-visibility/fixture"})
        if(!CreateDirectoryW(path,nullptr)&&GetLastError()!=ERROR_ALREADY_EXISTS)return 620;
    g.directory=L"render-self-test/menu-visibility/fixture";
    g.savePath=g.directory+L"/pet.ini";g.backupPath=g.directory+L"/pet.bak";
    g.nutritionReady=false;
    g.taskbarCreated=RegisterWindowMessageW(L"YeopjariCat.MenuVisibilityTest.Taskbar");
    constexpr wchar_t name[]=L"YeopjariCat.Native.MenuVisibilityTest";
    WNDCLASSEXW klass{};klass.cbSize=sizeof(klass);klass.lpfnWndProc=Procedure;
    klass.hInstance=instance;klass.lpszClassName=name;
    if(!RegisterClassExW(&klass))return 620;
    const std::array<RECT,5> works{{{0,0,1920,1080},{-1920,-1080,0,0},{-2560,0,-1280,1440},
        {-1920,-1080,0,0},{1920,0,3200,720}}};
    const std::array<RECT,5> actors{{{1780,940,1900,1070},{-200,-150,-20,-5},{-2550,1300,-2420,1438},
        {-1900,-1200,-1800,-1000},{3000,620,3190,718}}};
    for(unsigned i=0;i<works.size();++i) {
        UINT alignment=0;const auto point=PetMenuAnchor(actors[i],works[i],alignment);
        if(point.x<works[i].left||point.x>works[i].right||point.y<works[i].top||point.y>=works[i].bottom||
                (alignment==TPM_LEFTALIGN?point.x!=actors[i].right:point.x!=actors[i].left))return 630;
    }
    std::ostringstream report;report.imbue(std::locale::classic());
    report<<"{\"schema\":1,\"status\":\"passed\",\"test\":\"native-context-menu-visibility\","
        "\"version\":\"" YEOPJARI_VERSION_STRING "\",\"render_source\":\"Windows layered HWND / GDI+ / UI Automation\","
        "\"desktop_screenshots\":true,\"placement_cases\":5,\"negative_monitor_coordinates_checked\":true,\"cases\":[";
    bool first=true;
    int result=0;
    // The first case is the exact old popup placement, retained as labelled
    // evidence of coverage rather than reported as the corrected product.
    for(unsigned scenario=0;scenario<6&&!result;++scenario) {
        const wchar_t* labels[]{L"legacy-idle",L"idle",L"walk",L"jump",L"eat",L"tray"};
        const char* names[]{"legacy-idle","idle","walk","jump","eat","tray"};
        context="native menu scenario="+std::string(names[scenario]);
        native_manual_motion_tests::Host host;host.Reset(cat::Character::Cream,true);
        g.workInterference=false;g.nutritionReady=false;
        g.monitor=MonitorFromPoint({0,0},MONITOR_DEFAULTTOPRIMARY);
        MONITORINFO monitor{};monitor.cbSize=sizeof(monitor);GetMonitorInfoW(g.monitor,&monitor);
        g.work=monitor.rcWork;g.dpiScale=1;g.centerX=g.work.left+400;ResetMovement();
        for(unsigned i=0;i<12;++i)g.physics.Update(1./60);
        g.window=CreateWindowExW(WS_EX_LAYERED|WS_EX_TOOLWINDOW|WS_EX_TOPMOST|WS_EX_NOACTIVATE,
            name,L"Native menu visibility fixture",WS_POPUP,0,0,320,300,nullptr,nullptr,instance,nullptr);
        if(!g.window||!g.surface.Create(320,300)){result=620;break;}
        const auto ticks=GetTickCount64();g.lastTick=ticks;g.lastPhysicsTime=MotionClock();
        g.nextMaintenance=g.nextSave=g.foodRequestAfter=g.updateAnnounceAfter=ticks+60000;
        if(scenario==2)StartManualMotion(Motion::Walk);
        else if(scenario==3)StartManualMotion(Motion::Jump);
        else SetMotion(scenario==4?Motion::Eat:Motion::Idle,10000);
        g.timerInterval=0;RefreshAnimationTimer();Render();ShowWindow(g.window,SW_SHOWNOACTIVATE);
        if(scenario==3) {
            const auto takeoffDeadline=GetTickCount64()+2000;
            while(g.physics.RenderPose().grounded&&GetTickCount64()<takeoffDeadline)Pump(10);
            Pump(60);
        } else if(scenario==2) {
            // Open the menu during actual walking, after the complete new
            // planted entrance. A fixed 1.5 s wait sampled its last still pose.
            const auto movementDeadline=GetTickCount64()+4000;
            while(std::abs(WalkingVelocity(g.physics.RenderPose()))<=2&&GetTickCount64()<movementDeadline)Pump(10);
        } else Pump(scenario==4?750:120);
        const auto saved=cat::Serialize(g.pet);const auto serial=g.motionSerial;
        const auto before=Alpha();const bool moving=scenario!=2||std::abs(WalkingVelocity(g.physics.RenderPose()))>2;
        const bool airborne=scenario!=3||!g.physics.RenderPose().grounded;
        POINT click{-1,-1};
        for(int x=0;x<g.surface.width&&click.x<0;++x)for(int y=0;y<g.surface.height;++y)
            if(g.surface.pixels[(static_cast<std::size_t>(y)*g.surface.width+x)*4+3]>=64){click={x,y};break;}
        Probe probe;probe.owner=g.window;
        probe.screenshot=L"render-self-test/menu-visibility/"+std::wstring(labels[scenario])+L".png";
        active=&probe;std::thread observer(Observe,std::ref(probe));
        POINT screen=click;ClientToScreen(g.window,&screen);
        if(scenario==0) {
            HMENU menu=BuildPetMenu();g.menuOpen=true;SetForegroundWindow(g.window);
            const auto choice=TrackPopupMenu(menu,TPM_RETURNCMD|TPM_NONOTIFY|TPM_RIGHTBUTTON,
                screen.x,screen.y,0,g.window,nullptr);
            DestroyMenu(menu);g.menuOpen=false;if(choice)result=626;
        } else if(scenario==5) {
            const POINT tray{monitor.rcWork.right-4,std::min(monitor.rcMonitor.bottom-2,monitor.rcWork.bottom+4)};
            SendMessageW(g.window,kTrayMessage,MAKELPARAM(tray.x,tray.y),MAKELPARAM(WM_CONTEXTMENU,1));
        } else {
            SendMessageW(g.window,WM_RBUTTONDOWN,MK_RBUTTON,MAKELPARAM(click.x,click.y));
            if(g.hidden||g.closing||!IsWindowVisible(g.window)||Alpha()!=before)result=626;
            SendMessageW(g.window,WM_RBUTTONUP,0,MAKELPARAM(click.x,click.y));
        }
        observer.join();active=nullptr;Pump(40);
        const bool afterVisible=IsWindowVisible(g.window)!=FALSE;
        const auto after=Alpha();const bool unchanged=cat::Serialize(g.pet)==saved&&g.motionSerial==serial;
        RECT afterRect{};GetWindowRect(g.window,&afterRect);
        const auto afterPath=L"render-self-test/menu-visibility/"+std::wstring(labels[scenario])+L"-after.png";
        if(!DesktopScreenshot(afterRect,g.window,afterPath))result=625;
        if(probe.error)result=probe.error;
        if(!moving||!airborne||before<400||probe.alpha!=before||after<400||!probe.visible||
                probe.hidden||probe.closing||!probe.menuOpen||!afterVisible||g.hidden||g.closing||g.menuOpen||!unchanged)result=626;
        if(scenario&&probe.uncovered*100<probe.alpha*98)result=627;
        if(!first)report<<',';first=false;
        report<<"{\"scenario\":\""<<names[scenario]<<"\",\"legacy_baseline\":"<<(scenario==0?"true":"false")
            <<",\"uia_popup_observed\":"<<(probe.sampled&&!probe.error?"true":"false")
            <<",\"window_visible_during\":"<<(probe.visible?"true":"false")
            <<",\"window_visible_after\":"<<(afterVisible?"true":"false")
            <<",\"hidden\":"<<(g.hidden?"true":"false")<<",\"closing\":"<<(g.closing?"true":"false")
            <<",\"alpha_before\":"<<before<<",\"alpha_during\":"<<probe.alpha<<",\"alpha_after\":"<<after
            <<",\"uncovered_alpha\":"<<probe.uncovered<<",\"state_unchanged\":"<<(unchanged?"true":"false")
            <<",\"screenshot\":\""<<names[scenario]<<".png\",\"after_screenshot\":\""<<names[scenario]<<"-after.png\"}";
        KillTimer(g.window,kTickTimer);DestroyWindow(g.window);g.window=nullptr;g.surface.Reset();
    }
    report<<"],\"exit_code\":"<<result<<"}\n";
    auto bytes=report.str();
    if(result){const auto at=bytes.find("\"status\":\"passed\"");bytes.replace(at,17,"\"status\":\"failed\"");}
    if(!WriteBytes(L"render-self-test/menu-visibility/report.json",bytes)&&!result)result=628;
    g.initialized=false;g.window=nullptr;UnregisterClassW(name,instance);
    return result;
}
} // namespace native_menu_visibility_tests

int NativeMenuVisibilityTest(HINSTANCE instance) {
    g.instance=instance;
    const auto com=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);if(FAILED(com))return 620;
    ULONG_PTR token=0;GdiplusStartupInput startup;
    if(GdiplusStartup(&token,&startup,nullptr)!=Ok){CoUninitialize();return 620;}
    std::string context="menu visibility initialization";int result=0;
    try{result=native_menu_visibility_tests::Run(instance,context);}catch(...){result=629;}
    WriteBytes(L"render-self-test/menu-visibility-report.txt","exit="+std::to_string(result)+" "+context+"\n");
    ReleaseAtlases();ReleaseCarePresentation();g.surface.Reset();g.blendFrom.Reset();g.blendTo.Reset();
    g.styleBlendFrom.Reset();g.styleBlendTo.Reset();g.initialized=false;cursorInteraction={};
    if(g.preciseTimer){timeEndPeriod(1);g.preciseTimer=false;}
    GdiplusShutdown(token);CoUninitialize();return result;
}
