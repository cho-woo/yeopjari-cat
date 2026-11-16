#pragma once
namespace native_frame_timer {
constexpr UINT Message=WM_APP+0x39a;
struct State {cat::frame::Timer timer;std::vector<ULONGLONG> frames;};
LRESULT CALLBACK Procedure(HWND window,UINT message,WPARAM wParam,LPARAM lParam){
    auto* state=reinterpret_cast<State*>(GetWindowLongPtrW(window,GWLP_USERDATA));
    if(message==WM_NCCREATE){state=static_cast<State*>(reinterpret_cast<CREATESTRUCTW*>(lParam)->lpCreateParams);
        SetWindowLongPtrW(window,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(state));}
    if(message==Message&&state){if(state->timer.Consume(wParam)){state->frames.push_back(GetTickCount64());state->timer.Complete(wParam);}return 0;}
    return DefWindowProcW(window,message,wParam,lParam);
}
void Pump(unsigned ms){const auto end=GetTickCount64()+ms;while(GetTickCount64()<end){
    MSG message{};while(PeekMessageW(&message,nullptr,0,0,PM_REMOVE)){TranslateMessage(&message);DispatchMessageW(&message);}
    MsgWaitForMultipleObjects(0,nullptr,FALSE,2,QS_ALLINPUT);
}}
int Run(HINSTANCE instance){
    constexpr wchar_t name[]=L"YeopjariCat.FrameTimerTest";WNDCLASSW klass{};
    klass.lpfnWndProc=Procedure;klass.hInstance=instance;klass.lpszClassName=name;
    if(!RegisterClassW(&klass)&&GetLastError()!=ERROR_CLASS_ALREADY_EXISTS)return 980;
    State state;HWND window=CreateWindowExW(0,name,L"",WS_POPUP,0,0,1,1,nullptr,nullptr,instance,&state);
    if(!window)return 980;
    if(!state.timer.Enable(window,Message)){DestroyWindow(window);return 981;}
    Pump(800);const auto paced=state.frames.size();state.timer.Disable();Pump(60);
    bool passed=paced>=20&&state.frames.size()==paced;
    state.timer.Enable(window,Message);Sleep(180);
    MSG message{};std::vector<MSG> pending;
    while(PeekMessageW(&message,window,Message,Message,PM_REMOVE))pending.push_back(message);
    const unsigned queued=static_cast<unsigned>(pending.size());
    for(auto& frame:pending)DispatchMessageW(&frame);
    passed=passed&&queued==1;
    state.timer.Disable();state.timer.Enable(window,Message);const auto before=state.frames.size();Pump(80);
    passed=passed&&state.frames.size()>before;
    state.timer.Shutdown();const auto stopped=state.frames.size();Pump(40);passed=passed&&state.frames.size()==stopped;
    DestroyWindow(window);CreateDirectoryW(L"render-self-test",nullptr);
    WriteBytes(L"render-self-test/frame-timer.json","{\"passed\":"+std::string(passed?"true":"false")+
        ",\"paced_frames_800ms\":"+std::to_string(paced)+",\"queued_after_180ms_ui_stall\":"+std::to_string(queued)+"}");
    return passed?0:982;
}
}
