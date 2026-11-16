#pragma once
#include <windows.h>
#include <atomic>
#include <cstdint>
#include <mutex>
#include <thread>

namespace cat::frame {
// Active animation uses a high-resolution waitable timer. Posted ticks are
// coalesced: a busy UI receives one pending frame, never a catch-up burst.
// Hidden/resting actors continue to use the host's inexpensive Win32 timer.
class Timer {
public:
    ~Timer(){Shutdown();}
    bool Enable(HWND window,UINT message,unsigned intervalMs=16) {
        std::lock_guard<std::mutex> lock(mutex_);
        if(failed_||!window)return false;
        if(enabled_&&window_==window&&message_==message&&interval_==intervalMs)return true;
        if(!timer_) {
            // https://learn.microsoft.com/windows/win32/api/synchapi/nf-synchapi-createwaitabletimerexw
            timer_=CreateWaitableTimerExW(nullptr,nullptr,0x00000002,TIMER_ALL_ACCESS);
            control_=CreateEventW(nullptr,FALSE,FALSE,nullptr);
            if(!timer_||!control_){Close();failed_=true;return false;}
            try {worker_=std::thread([this]{Run();});}
            catch(...){Close();failed_=true;return false;}
        }
        window_=window;message_=message;interval_=intervalMs;++generation_;pending_=false;
        LARGE_INTEGER due{};due.QuadPart=-10000LL*intervalMs;
        enabled_=SetWaitableTimer(timer_,&due,static_cast<LONG>(intervalMs),nullptr,nullptr,FALSE)!=FALSE;
        SetEvent(control_);return enabled_;
    }
    void Disable() {
        std::lock_guard<std::mutex> lock(mutex_);
        if(!enabled_)return;
        enabled_=false;pending_=false;++generation_;
        CancelWaitableTimer(timer_);SetEvent(control_);
    }
    bool Consume(WPARAM generation) {
        std::lock_guard<std::mutex> lock(mutex_);
        if(!enabled_||generation!=generation_)return false;
        return true;
    }
    void Complete(WPARAM generation) {
        std::lock_guard<std::mutex> lock(mutex_);
        if(enabled_&&generation==generation_)pending_=false;
    }
    void Shutdown() {
        {std::lock_guard<std::mutex> lock(mutex_);enabled_=false;pending_=false;++generation_;
            stopping_=true;if(control_)SetEvent(control_);}
        if(worker_.joinable())worker_.join();Close();
    }
private:
    std::mutex mutex_;HANDLE timer_=nullptr,control_=nullptr;
    std::thread worker_;std::atomic<bool> enabled_{false},stopping_{false};
    HWND window_=nullptr;UINT message_=0;unsigned interval_=0;
    std::uintptr_t generation_=1;bool pending_=false,failed_=false;
    void Close(){if(timer_)CloseHandle(timer_);if(control_)CloseHandle(control_);timer_=control_=nullptr;}
    void Run(){
        HANDLE events[]{control_,timer_};
        while(!stopping_) {
            const auto result=WaitForMultipleObjects(enabled_?2:1,events,FALSE,INFINITE);
            if(result==WAIT_OBJECT_0+1){
                std::lock_guard<std::mutex> lock(mutex_);
                if(enabled_&&!stopping_&&!pending_){pending_=true;
                    if(!PostMessageW(window_,message_,generation_,0))pending_=false;}
            } else if(result!=WAIT_OBJECT_0)break;
        }
    }
};
}
