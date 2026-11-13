#pragma once

// Shipping input owners, physical motor and GDI+ renderer with memory-only
// samples. These movies never move OS input or load/save a user's pet profile.
namespace native_controls_capture {
using namespace native_cursor_play_capture;
constexpr unsigned ControlsCount=1680;
int SimulateControls(Row& row,std::string& context) {
    native_manual_motion_tests::Host host;host.Reset(row.character,false);
    g.cursorPlay=true;g.direction=g.facing=1;g.nextDecision=host.now+120000;
    auto saved=host.Saved();const double initialX=g.physics.RenderPose().x;
    row.body=cat::appearance::VisualModifiers(g.nutrition,row.character);
    Surface actor;if(!actor.Create(ActorWidth,ActorHeight))return 810;
    unsigned nearTurns=0,nearSwats=0,chases=0,interrupted=0,resets=0;
    bool wasTurning=false;ULONGLONG turnAge=0,serial=g.motionSerial;Motion prior=g.motion;
    for(unsigned index=0;index<ControlsCount;++index) {
        const auto elapsed=index*1000ULL/60;
        if(index==480) {
            if(host.Saved()!=saved)return 813;
            if(!native_manual_motion_tests::keyboard_native::Start(host,row.character))return 811;
            saved=host.Saved();
            serial=g.motionSerial;prior=g.motion;wasTurning=false;
        }
        const auto step=(index+1)*1000ULL/60-elapsed;
        Frame frame;
        if(index<480) {
            frame.pointerX=elapsed<5000?initialX+(elapsed<4000?48:90)*std::sin(elapsed*.001*6*cat::animation::kPi):
                1000+(elapsed-5000)*.08;
            frame.pointerHeight=elapsed<5000?65:120;
            host.now+=step;
            POINT cursor{static_cast<LONG>(g.work.left+frame.pointerX*g.dpiScale),
                static_cast<LONG>(g.work.bottom-frame.pointerHeight*g.dpiScale)};
            UpdateCursorReactionSample(host.now,step,cursor,true);
            AdvancePrimaryMotion(host.now,step/1000.);
            frame.action=cursorInteraction.session.Action();
            if(elapsed<4000) {nearTurns+=g.runningTurn.Active();nearSwats+=frame.action==Action::Paw||frame.action==Action::Punch;}
            else if(elapsed>=5000)chases+=frame.action==Action::Chase;
        } else {
            native_manual_motion_tests::keyboard_native::ContinuityKeys(index-480);
            native_manual_motion_tests::keyboard_native::Step(host);
            if(wasTurning&&!g.runningTurn.Active()) {
                const auto* turn=LoadedCareSprite(Clip::RunningTurn,row.character);
                if(turn&&turnAge+32<cat::sprite_sequence::ProgramDuration(turn->layout->program))++interrupted;
            }
            if(serial!=g.motionSerial&&cat::animation::Walking(prior)&&cat::animation::Walking(g.motion))++resets;
            wasTurning=g.runningTurn.Active();turnAge=g.runningTurn.SpriteMs();serial=g.motionSerial;prior=g.motion;
        }
        const auto physical=g.physics.RenderPose();frame.pose=PrimaryDisplayedPose(host.now,physical);
        frame.x=physical.x;frame.lift=physical.lift;frame.velocityX=physical.velocityX;frame.velocityY=physical.velocityY;
        frame.grounded=physical.grounded;frame.serial=g.motionSerial;frame.motion=g.motion;
        frame.actionElapsed=host.now>=g.motionStart?host.now-g.motionStart:0;
        if(const auto* art=CareSpriteFor(frame.pose,row.character)) {frame.source=SpriteFrameFor(frame.pose,*art->layout);frame.resource=art->layout->resourceId;}
        if(!DrawActor(actor,row,frame,elapsed)||!CacheActor(actor,row.character,index))return 812;
        frame.actorHash=cat::Checksum(std::string(reinterpret_cast<const char*>(actor.pixels),ActorWidth*ActorHeight*4));
        row.frames.push_back(frame);
    }
    native_manual_motion_tests::keyboard_native::Clear();
    context="controls coat="+std::to_string(static_cast<unsigned>(row.character))+" near swats="+std::to_string(nearSwats)+
        " near turns="+std::to_string(nearTurns)+" chase="+std::to_string(chases)+" interrupted="+std::to_string(interrupted)+" resets="+std::to_string(resets);
    if(nearTurns||nearSwats<160||chases<40||interrupted||resets||host.Saved()!=saved)return 813;
    return 0;
}
int Controls(std::string& context) {
    for(const auto path:{L"render-self-test",L"render-self-test/controls-play",L"render-self-test/cursor-actors"})CreateDirectoryW(path,nullptr);
    std::array<Row,3> rows{};rows[1].character=cat::Character::Tuxedo;rows[2].character=cat::Character::OddEye;
    for(auto& row:rows)if(const auto result=SimulateControls(row,context))return result;
    Surface canvas,actor;if(!canvas.Create(Width,Height)||!actor.Create(ActorWidth,ActorHeight))return 810;
    std::ostringstream trace;trace.imbue(std::locale::classic());trace<<"{\"fps\":60,\"frames\":[";
    for(unsigned index=0;index<ControlsCount;++index) {
        if(index)trace<<',';trace<<"{\"frame\":"<<index<<",\"cats\":[";
        {
            Graphics graphics(canvas.image.get());graphics.Clear(Color(255,247,246,241));
            Font title(L"Segoe UI",19),small(L"Segoe UI",13);SolidBrush ink(Color(255,46,53,58));Pen floorPen(Color(255,197,201,196));
            graphics.DrawString(index<480?L"Near pointer: 3 Hz teasing -> escape / actual app renderer":
                L"Keyboard: walk / Shift / reversal / stop / taps / jump / actual app renderer",-1,&title,PointF(22,16),&ink);
            for(unsigned r=0;r<rows.size();++r) {
                const auto& frame=rows[r].frames[index];if(!RestoreActor(actor,rows[r].character,index))return 812;
                const REAL floor=335+r*340,root=static_cast<REAL>(std::clamp(80+frame.x,220.,Width-220.));
                graphics.DrawLine(&floorPen,20.0f,floor,Width-20.0f,floor);
                graphics.DrawImage(actor.image.get(),root-ActorWidth/2,floor-ActorFloor);
                if(index<480) {
                    const REAL px=static_cast<REAL>(root+frame.pointerX-frame.x),py=static_cast<REAL>(floor-frame.pointerHeight);
                    SolidBrush pointer(Color(255,224,100,58));graphics.FillEllipse(&pointer,px-4,py-4,8.f,8.f);
                }
                const std::wstring label=L"Coat "+std::to_wstring(static_cast<unsigned>(rows[r].character))+L" / source "+
                    std::to_wstring(frame.resource)+L":"+std::to_wstring(frame.source);
                graphics.DrawString(label.c_str(),-1,&small,PointF(22,floor-255),&ink);
                if(r)trace<<',';
                trace<<"{\"coat\":"<<static_cast<unsigned>(rows[r].character)<<",\"clip\":"<<static_cast<unsigned>(frame.pose.spriteClip)
                    <<",\"source\":"<<frame.source<<",\"serial\":"<<frame.serial<<",\"x\":"<<frame.x<<",\"lift\":"<<frame.lift
                    <<",\"vx\":"<<frame.velocityX<<",\"hash\":\""<<frame.actorHash<<"\"}";
            }
        }
        trace<<"]}";
        const auto digits=std::to_wstring(index);
        if(!SaveNativeWalkingReview(*canvas.image,(L"render-self-test/controls-play/frame-"+std::wstring(5-digits.size(),L'0')+digits+L".png").c_str()))return 814;
    }
    trace<<"]}";WriteBytes(L"render-self-test/controls-play/trace.json",trace.str());
    WriteBytes(L"render-self-test/controls-play/capture.json","{\"status\":\"passed\",\"fps\":60,\"frame_count\":1680,\"near_turns\":0,\"interrupted_keyboard_turns\":0,\"travel_clock_resets\":0,\"coats\":[0,8,10],\"os_input_injected\":false,\"profile_unchanged\":true}");
    return 0;
}
#include "native_companion_controls_capture.hpp"
} // namespace native_controls_capture

int NativeControlsCapture(HINSTANCE instance,bool companionOnly=false) {
    g.instance=instance;ULONG_PTR token=0;GdiplusStartupInput startup;
    if(GdiplusStartup(&token,&startup,nullptr)!=Ok)return 810;
    const HRESULT com=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
    std::string context="control capture initialization";int result=810;
    try {
        if(LoadAtlas()) {
            result=companionOnly?0:native_controls_capture::Controls(context);
            if(!result)result=native_controls_capture::CompanionReview(context);
        }
    }catch(...){result=819;}
    WriteBytes(L"render-self-test/controls-play-report.txt","exit="+std::to_string(result)+" "+context);
    ReleaseAtlases();ReleaseCarePresentation();g.styleBlendFrom.Reset();g.styleBlendTo.Reset();
    g.initialized=false;g.window=nullptr;cursorInteraction={};GdiplusShutdown(token);if(SUCCEEDED(com))CoUninitialize();return result;
}
