#pragma once
#include <tuple>

// Only --self-test enters this fixture, before any user files, application
// windows or update worker are opened. These are real HWND geometry/lifetime
// queries and the production transition host around a real Box2D controller.
namespace native_perch_tests {
template<class... Values> class RestoreValues {
public:
    explicit RestoreValues(Values&... values):references_(values...),saved_(values...){}
    ~RestoreValues(){Restore(std::index_sequence_for<Values...>{});}
private:
    std::tuple<Values&...> references_;
    std::tuple<Values...> saved_;
    template<std::size_t... Indices> void Restore(std::index_sequence<Indices...>) {
        ((std::get<Indices>(references_)=std::move(std::get<Indices>(saved_))),...);
    }
};
struct Window {
    HWND value=nullptr;
    ~Window(){if(value&&IsWindow(value))DestroyWindow(value);}
};
struct PhysicsReset {
    ~PhysicsReset(){g.physics.Reset(0,0,100);}
};
LRESULT CALLBACK WindowProcedure(HWND window,UINT message,WPARAM wParam,LPARAM lParam) {
    if(message==WM_MOUSEACTIVATE)return MA_NOACTIVATE;
    return DefWindowProcW(window,message,wParam,lParam);
}

int Run(HINSTANCE instance) {
    // A live user/controller state is never reconstructed by a test fixture.
    // SelfTest invokes this first, and restores every host field it touches.
    if(g.physicsReady||g.window||g.initialized||g.brain||g.toyEngaged)return 270;
    RestoreValues saved(g.instance,g.monitor,g.work,g.dpiScale,g.centerX,g.physicsReady,
        g.pet,g.preferences,g.nutrition,g.perch,g.direction,g.facing,g.airborneMotion,
        g.motion,g.motionSerial,g.motionStart,g.nextDecision,g.walkingMotion,g.waitingForStop,
        g.jumpPending,g.jumpLaunched,g.punchHits,g.lifeSequenceCount,g.smallBoxEvent,
        g.lifeCrossing,g.lifeHangUntil,g.approachingToy,g.toyContact,g.approachingUser,
        g.memorySequenceCount,g.landedAt,g.landingStrength,g.gait,g.random);
    PhysicsReset resetPhysics;
    g.instance=instance;
    g.monitor=MonitorFromPoint({0,0},MONITOR_DEFAULTTOPRIMARY);
    MONITORINFO monitor{};monitor.cbSize=sizeof(monitor);
    if(!g.monitor||!GetMonitorInfoW(g.monitor,&monitor))return 271;
    g.work=monitor.rcWork;g.dpiScale=1;
    if(g.work.right-g.work.left<640||g.work.bottom-g.work.top<480)return 272;
    const auto utc=UtcNow();
    g.pet=cat::NewPet(utc-366*cat::life::Day);
    g.preferences={cat::Character::Cream,cat::Outfit::None};
    g.nutrition=cat::nutrition::NewState(g.pet.bornUtc,utc,80);
    constexpr wchar_t name[]=L"YeopjariCat.Native.PerchTransitionSelfTest";
    WNDCLASSEXW klass{};klass.cbSize=sizeof(klass);klass.lpfnWndProc=WindowProcedure;
    klass.hInstance=instance;klass.lpszClassName=name;
    if(!RegisterClassExW(&klass))return 273;
    const int result=[&]() -> int {
        // Control landing, moved target, resized/raised target, maximized
        // style/geometry, and actual destruction during the descending arc.
        for(int scenario=0;scenario<5;++scenario) {
            Window window;
            const int left=g.work.left+20,top=std::max(g.work.top+300,g.work.bottom-400);
            window.value=CreateWindowExW(WS_EX_LAYERED|WS_EX_NOACTIVATE,name,L"Perch transition fixture",
                WS_POPUP|WS_MAXIMIZEBOX,left,top,360,180,
                nullptr,nullptr,instance,nullptr);
            if(!window.value)return 274;
            if(!SetLayeredWindowAttributes(window.value,0,0,LWA_ALPHA)||
                !SetWindowPos(window.value,HWND_BOTTOM,left,top,360,180,SWP_SHOWWINDOW|SWP_NOACTIVATE))return 275;
            DwmFlush();
            if(GetForegroundWindow()==window.value)return 276;
            RECT initial{};
            if(!QueryPerchRect(window.value,initial))return 277;
            const double height=g.work.bottom-initial.top;
            g.perch={};g.motion=Motion::Idle;g.gait.Reset();
            g.centerX=initial.left+90;
            g.physics.Reset(g.centerX-g.work.left,96,g.work.right-g.work.left-96);
            g.physics.SetCeiling(g.work.bottom-g.work.top-160);
            g.physicsReady=true;
            BeginPerchTransition(window.value,initial,true);
            if(!g.perch.transitioning||!g.perch.preparing||!g.perch.landingOnWindow)return 278;
            ULONGLONG now=g.motionStart;
            bool airborne=false,changed=false,falling=false;
            for(int frame=0;frame<600&&g.perch.transitioning;++frame) {
                const auto before=g.physics.CurrentPose();
                if(scenario&&airborne&&!changed&&before.velocityY<0&&before.lift>height+5) {
                    RECT bounds{};
                    if(!GetWindowRect(window.value,&bounds))return 279;
                    if(scenario==1) {
                        if(!SetWindowPos(window.value,nullptr,bounds.left+240,bounds.top,0,0,
                            SWP_NOSIZE|SWP_NOZORDER|SWP_NOACTIVATE))return 280;
                    } else if(scenario==2) {
                        // New ledge is above the fixed launch apex. Resizing
                        // must not retain/catch the body on its old elevation.
                        if(!SetWindowPos(window.value,nullptr,bounds.left,bounds.top-80,330,260,
                            SWP_NOZORDER|SWP_NOACTIVATE))return 281;
                    } else if(scenario==3) {
                        // Exercise the actual IsZoomed path without activating
                        // a window via ShowWindow(SW_SHOWMAXIMIZED). This is
                        // simulated maximize style/geometry, not synthetic HWND data.
                        const auto style=GetWindowLongPtrW(window.value,GWL_STYLE);
                        SetWindowLongPtrW(window.value,GWL_STYLE,style|WS_MAXIMIZE);
                        if(!SetWindowPos(window.value,nullptr,g.work.left,g.work.top,
                            g.work.right-g.work.left,g.work.bottom-g.work.top,
                            SWP_FRAMECHANGED|SWP_NOZORDER|SWP_NOACTIVATE)||!IsZoomed(window.value))return 282;
                    } else {
                        if(!DestroyWindow(window.value))return 283;
                        window.value=nullptr;
                    }
                    DwmFlush();
                    RECT updated{};
                    if(scenario<=2) {
                        if(!QueryPerchRect(window.value,updated))return 284;
                        if((scenario==1&&updated.left<=initial.left+150)||
                           (scenario==2&&updated.top>=initial.top-65))return 285;
                    } else if(QueryPerchRect(window.value,updated))return 286;
                    if(window.value&&GetForegroundWindow()==window.value)return 276;
                    changed=true;
                }
                now+=17;
                UpdatePerchTransition(now,cat::physics::kFixedStepSeconds);
                const auto after=g.physics.CurrentPose();
                airborne=airborne||(!after.grounded&&after.lift>5);
                if(!std::isfinite(after.x)||!std::isfinite(after.lift)||after.hanging)return 287;
                if(changed) {
                    if(after.onPlatform||(after.grounded&&after.lift>1))return 288;
                    if(after.lift>before.lift+.1)return 289;
                    falling=falling||after.lift<before.lift-.1;
                    if(scenario>=3&&(g.perch.hwnd||g.perch.landingOnWindow))return 290;
                }
            }
            const auto landed=g.physics.CurrentPose();
            if(!airborne||g.perch.transitioning||g.perch.preparing||!landed.grounded)return 291;
            if(!scenario) {
                if(!landed.onPlatform||!g.perch.active||!g.perch.landingOnWindow||
                   g.perch.hwnd!=window.value||std::abs(landed.lift-height)>.7)return 292;
            } else if(!changed||!falling||landed.onPlatform||std::abs(landed.lift)>.7||
                g.perch.active||g.perch.landingOnWindow||g.perch.hwnd)return 293;
            g.physics.RemovePlatform();
        }
        return 0;
    }();
    UnregisterClassW(name,instance);
    return result;
}
} // namespace native_perch_tests

int NativePerchTransitionTest(HINSTANCE instance) {
    return native_perch_tests::Run(instance);
}
