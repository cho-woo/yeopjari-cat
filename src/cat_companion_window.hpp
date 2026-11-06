#pragma once

// Included by cat_companion.hpp only on Windows. All scheduling remains in the
// application's existing timer; this window owns no second loop or timer.
#include <windows.h>
#include <windowsx.h>
#include <cstring>

namespace cat::companion {

enum class InputKind { Pet, Menu, Drag, DragEnd };
struct Input { InputKind kind = InputKind::Pet; int screenX = 0, screenY = 0; };

class Window {
public:
    // Paint into the supplied premultiplied BGRA DIB. Its transparent margins
    // remain click-through. Render callbacks may share immutable sprite atlases,
    // but receive every cat-specific visual value explicitly in the snapshot.
    using Painter = std::function<void(HDC,unsigned char*,int,int,const Snapshot&,double)>;
    using Handler = std::function<void(const Input&)>;
    Window() = default;
    Window(const Window&) = delete;
    Window& operator=(const Window&) = delete;
    ~Window() { Close(); }

    bool Create(HINSTANCE instance,HWND owner,Painter painter,Handler handler) {
        if(hwnd_) return true;
        WNDCLASSEXW wc{}; wc.cbSize=sizeof(wc); wc.hInstance=instance;
        wc.lpfnWndProc=Procedure; wc.hCursor=LoadCursorW(nullptr,IDC_ARROW);
        wc.lpszClassName=L"YeopjariCatIndependentCompanion";
        if(!RegisterClassExW(&wc) && GetLastError()!=ERROR_CLASS_ALREADY_EXISTS) return false;
        painter_=std::move(painter); handler_=std::move(handler);
        hwnd_=CreateWindowExW(WS_EX_LAYERED|WS_EX_TOOLWINDOW|WS_EX_TOPMOST|WS_EX_NOACTIVATE,
            wc.lpszClassName,L"옆자리 고양이 · 두 번째 고양이",WS_POPUP,0,0,1,1,owner,nullptr,instance,this);
        return hwnd_!=nullptr;
    }
    HWND Handle() const { return hwnd_; }
    // The supporting application's own rectangle always receives its clicks;
    // the cat body above that top edge remains independently interactive.
    void SetSupportRect(const RECT* rect) { supportRect_=rect?*rect:RECT{}; hasSupportRect_=rect!=nullptr; }
    bool Dragging() const { return pressed_ && moved_; }
    bool Interacting() const { return pressed_ || dispatching_; }
    bool Present(const Snapshot& s,int workLeft,int workBottom,double dpi,bool hidden=false,
                 int bottomSafetyInset=0) {
        if(!hwnd_) return false;
        if(hidden || !s.enabled) { ShowWindow(hwnd_,SW_HIDE); return true; }
        dpi_=std::isfinite(dpi)?std::clamp(dpi,.5,4.0):1;
        const int width=static_cast<int>(std::ceil(280*dpi_)),height=static_cast<int>(std::ceil(235*dpi_));
        if((width!=width_ || height!=height_) && !Resize(width,height)) return false;
        bottomSafetyInset=std::clamp(bottomSafetyInset,0,height_-1);
        std::memset(pixels_,0,static_cast<std::size_t>(width_)*height_*4);
        if(painter_) painter_(dc_,pixels_,width_,height_,s,dpi_);
        POINT destination{workLeft+static_cast<LONG>(std::lround(s.physical.x*dpi_))-width_/2,
            workBottom-static_cast<LONG>(std::lround(s.physical.lift*dpi_))-height_+bottomSafetyInset};
        POINT origin{}; SIZE size{width_,height_}; BLENDFUNCTION blend{AC_SRC_OVER,0,255,AC_SRC_ALPHA};
        HDC screen=GetDC(nullptr);
        const bool rendered=UpdateLayeredWindow(hwnd_,screen,&destination,&size,dc_,&origin,0,&blend,ULW_ALPHA)!=FALSE;
        ReleaseDC(nullptr,screen);
        if(rendered) ShowWindow(hwnd_,SW_SHOWNOACTIVATE);
        return rendered;
    }
    void Close() {
        if(hwnd_) { HWND old=hwnd_; hwnd_=nullptr; DestroyWindow(old); }
        ReleaseSurface(); painter_={}; handler_={}; pressed_=moved_=false;
    }
private:
    friend bool NativeWindowSmokeTest(HINSTANCE,const Painter&,std::string&);
    HWND hwnd_=nullptr;
    HDC dc_=nullptr;
    HBITMAP bitmap_=nullptr,oldBitmap_=nullptr;
    unsigned char* pixels_=nullptr;
    int width_=0,height_=0;
    double dpi_=1;
    POINT press_{};
    RECT supportRect_{};
    bool hasSupportRect_=false;
    bool pressed_=false,moved_=false,dispatching_=false;
    Painter painter_; Handler handler_;
    void ReleaseSurface() {
        if(dc_ && oldBitmap_) SelectObject(dc_,oldBitmap_);
        if(bitmap_) DeleteObject(bitmap_);
        if(dc_) DeleteDC(dc_);
        dc_=nullptr; bitmap_=oldBitmap_=nullptr; pixels_=nullptr; width_=height_=0;
    }
    bool Resize(int width,int height) {
        ReleaseSurface();
        BITMAPINFO info{}; info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);
        info.bmiHeader.biWidth=width; info.bmiHeader.biHeight=-height;
        info.bmiHeader.biPlanes=1; info.bmiHeader.biBitCount=32; info.bmiHeader.biCompression=BI_RGB;
        HDC screen=GetDC(nullptr); dc_=CreateCompatibleDC(screen);
        bitmap_=CreateDIBSection(screen,&info,DIB_RGB_COLORS,reinterpret_cast<void**>(&pixels_),nullptr,0);
        ReleaseDC(nullptr,screen);
        if(!dc_ || !bitmap_ || !pixels_) { ReleaseSurface(); return false; }
        oldBitmap_=static_cast<HBITMAP>(SelectObject(dc_,bitmap_)); width_=width; height_=height; return true;
    }
    void Dispatch(InputKind kind,POINT point) {
        if(!handler_) return;
        dispatching_=true; handler_({kind,point.x,point.y}); dispatching_=false;
    }
    static LRESULT CALLBACK Procedure(HWND hwnd,UINT message,WPARAM wParam,LPARAM lParam) {
        auto* self=reinterpret_cast<Window*>(GetWindowLongPtrW(hwnd,GWLP_USERDATA));
        if(message==WM_NCCREATE) {
            self=static_cast<Window*>(reinterpret_cast<CREATESTRUCTW*>(lParam)->lpCreateParams);
            SetWindowLongPtrW(hwnd,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(self));
        }
        if(!self) return DefWindowProcW(hwnd,message,wParam,lParam);
        switch(message) {
        case WM_MOUSEACTIVATE:return MA_NOACTIVATE;
        case WM_ERASEBKGND:return 1;
        case WM_NCHITTEST: {
            POINT point{GET_X_LPARAM(lParam),GET_Y_LPARAM(lParam)};
            if(self->hasSupportRect_ && PtInRect(&self->supportRect_,point)) return HTTRANSPARENT;
            ScreenToClient(hwnd,&point);
            if(!self->pixels_ || point.x<0 || point.y<0 || point.x>=self->width_ || point.y>=self->height_) return HTTRANSPARENT;
            const auto alpha=self->pixels_[(static_cast<std::size_t>(point.y)*self->width_+point.x)*4+3];
            return alpha>=12?HTCLIENT:HTTRANSPARENT;
        }
        case WM_LBUTTONDOWN:
            GetCursorPos(&self->press_); self->pressed_=true; self->moved_=false; SetCapture(hwnd); return 0;
        case WM_MOUSEMOVE:
            if(self->pressed_) {
                POINT point{}; GetCursorPos(&point);
                self->moved_=self->moved_ || std::abs(point.x-self->press_.x)+std::abs(point.y-self->press_.y)>4*self->dpi_;
                if(self->moved_) self->Dispatch(InputKind::Drag,point);
            }
            return 0;
        case WM_LBUTTONUP:
            if(self->pressed_) {
                POINT point{}; GetCursorPos(&point);
                const auto kind=self->moved_?InputKind::DragEnd:InputKind::Pet;
                self->pressed_=self->moved_=false; ReleaseCapture(); self->Dispatch(kind,point);
            }
            return 0;
        case WM_CAPTURECHANGED:self->pressed_=self->moved_=false; return 0;
        case WM_RBUTTONUP: { POINT point{}; GetCursorPos(&point); self->Dispatch(InputKind::Menu,point); return 0; }
        case WM_CLOSE:ShowWindow(hwnd,SW_HIDE); return 0; // Main owns lifetime and the persisted enable setting.
        case WM_NCDESTROY:
            SetWindowLongPtrW(hwnd,GWLP_USERDATA,0); self->hwnd_=nullptr; break;
        }
        return DefWindowProcW(hwnd,message,wParam,lParam);
    }
};

// Exercise the real layered HWND and production WndProc using the caller's
// real atlas painter. No physical mouse event, capture or AppData write occurs.
// The caller initializes GDI+ and atlases just as it does for the main cat.
inline bool NativeWindowSmokeTest(HINSTANCE instance,const Window::Painter& painter,std::string& error) {
    Window window;
    unsigned menus=0;
    const auto fail=[&](const char* text) { error=text; return false; };
    if(!window.Create(instance,nullptr,painter,[&](const Input& input) { if(input.kind==InputKind::Menu) ++menus; }))
        return fail("Cannot create independent companion HWND");
    Snapshot snapshot; snapshot.enabled=true; snapshot.physical.x=220;
    snapshot.preferences={cat::Character::SilverTabby,cat::Outfit::BowTie};
    snapshot.animation=cat::animation::Evaluate(cat::animation::Motion::Idle,0,1000,1);
    if(!window.Present(snapshot,0,500,1)) return fail("Cannot present independent layered companion surface");
    POINT head{-1,-1},feet{-1,-1},clear{-1,-1};
    for(int y=0;y<window.height_;++y) for(int x=0;x<window.width_;++x) {
        const auto alpha=window.pixels_[(static_cast<std::size_t>(y)*window.width_+x)*4+3];
        if(alpha>=24) { if(head.x<0) head={x,y}; feet={x,y}; }
        else if(alpha==0 && clear.x<0) clear={x,y};
    }
    if(head.x<0 || feet.y<=head.y || clear.x<0) return fail("Companion painter did not produce a body with transparent margins");
    ClientToScreen(window.Handle(),&head); ClientToScreen(window.Handle(),&feet); ClientToScreen(window.Handle(),&clear);
    const auto hit=[&](POINT p) { return SendMessageW(window.Handle(),WM_NCHITTEST,0,MAKELPARAM(p.x,p.y)); };
    if(hit(head)!=HTCLIENT || hit(feet)!=HTCLIENT || hit(clear)!=HTTRANSPARENT)
        return fail("Companion alpha hit testing rejected the visible body or captured a transparent margin");
    RECT rect{};GetWindowRect(window.Handle(),&rect);rect.top=head.y+(feet.y-head.y)/2;
    window.SetSupportRect(&rect);
    if(hit(head)!=HTCLIENT || hit(feet)!=HTTRANSPARENT) return fail("Companion support rectangle obscures body input or captures app input");
    window.SetSupportRect(nullptr);
    if(hit(feet)!=HTCLIENT) return fail("Clearing companion support rectangle did not restore body input");
    if(SendMessageW(window.Handle(),WM_MOUSEACTIVATE,0,0)!=MA_NOACTIVATE) return fail("Companion unexpectedly activates the user's work window");
    SendMessageW(window.Handle(),WM_RBUTTONUP,0,0);
    if(menus!=1) return fail("Companion context-menu callback was not dispatched");
    window.Close(); if(window.Handle()) return fail("Companion HWND did not close cleanly");
    error.clear();return true;
}

} // namespace cat::companion
