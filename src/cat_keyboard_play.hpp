#pragma once
#include <array>

namespace cat::keyboard_play {
// Virtual-key values are shared by Win32 and the fake-input host tests. This
// model owns no OS hook, application focus or saved roaming preference.
constexpr unsigned Left=0x25,Right=0x27,Shift=0x10,Space=0x20,Escape=0x1b;
inline bool Mapped(unsigned key) {
    return key==Left || key==Right || key=='A' || key=='D' || key==Shift ||
        key==0xa0 || key==0xa1 || key==Space || key==Escape;
}
struct Edges { bool consume=false,exit=false,jump=false; };
class Input {
    bool active_=false;
    std::array<bool,256> held_{};
    bool jump_=false;
public:
    void Start() { Stop();active_=true; }
    void Stop() {active_=false;held_.fill(false);jump_=false;}
    bool Active()const{return active_;}
    bool Held(unsigned key)const{return key<held_.size()&&held_[key];}
    Edges Event(unsigned key,bool down,bool systemChord=false) {
        Edges out;
        if(!active_)return out;
        if(systemChord) {Stop();out.exit=true;return out;}
        if(!Mapped(key)||key>=held_.size())return out;
        // Key-up is consumed only for an edge this session actually captured.
        // Keys already held before opt-in are never silently claimed.
        out.consume=down||held_[key];
        const bool edge=down&&!held_[key];held_[key]=down;
        if(edge&&key==Escape){Stop();out.exit=true;return out;}
        if(edge&&key==Space){out.jump=true;jump_=true;}
        return out;
    }
    int Direction()const{
        if(!active_)return 0;
        return static_cast<int>(Held(Right)||Held('D'))-static_cast<int>(Held(Left)||Held('A'));
    }
    bool Running()const{return active_&&(Held(Shift)||Held(0xa0)||Held(0xa1));}
    bool TakeJump(){const bool value=jump_;jump_=false;return value;}
};
struct Safety {
    bool initialized=false,groundReady=false,hidden=false,menu=false,drag=false,closing=false,updating=false;
    bool foregroundChanged=false,owned=true;
    bool Allowed()const{return initialized&&groundReady&&!hidden&&!menu&&!drag&&!closing&&!updating&&!foregroundChanged&&owned;}
};
} // namespace cat::keyboard_play
