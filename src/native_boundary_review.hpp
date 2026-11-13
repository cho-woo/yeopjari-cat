#pragma once
#include <chrono>
#include "native_update_notice_tests.hpp"

// Full production action/physics/pose clocks; only the OS window and user's
// files are absent. Direct and shipping presentation see identical poses.
namespace native_boundary_review {
int NoticeRender() {
    const auto notice=updates.announcement;const auto operation=updates.operation;
    const HWND hidden=CreateWindowExW(WS_EX_LAYERED|WS_EX_TOOLWINDOW|WS_EX_NOACTIVATE,L"STATIC",L"",WS_POPUP,
        -2000,-2000,480,450,nullptr,nullptr,g.instance,nullptr);
    if(!hidden)return 915;
    updates.operation=UpdateState::Operation::None;
    updates.announcement.SetAvailable(kVersion,cat::update::ToString({kVersion.major,kVersion.minor,kVersion.patch+1}));
    int result=0;
    for(auto coat:cat::kAvailableCharacters) {
        native_manual_motion_tests::Host host;host.Reset(coat,true);g.window=hidden;
        if(!g.surface.Create(480,450)){result=915;break;}
        Render();
        const auto path=L"render-self-test/update-notice/c"+std::to_wstring(static_cast<unsigned>(coat))+L".png";
        if(!SaveNativeWalkingReview(*g.surface.image,path.c_str())){result=916;break;}
        SetMotion(Motion::Sleep,60000);RefreshAnimationTimer();
        if(g.timerInterval!=16){result=917;break;}
        const auto* sleep=LoadedCareSprite(cat::sprite_sequence::Clip::Sleep,coat);
        const auto intro=cat::animation::ReferenceSleepIntroMs(&sleep->layout->program,sleep->layout->frameCount);
        g.motionStart=GetTickCount64()-intro-cat::care_presentation::Transition::DurationMs-500;
        RefreshAnimationTimer();if(g.timerInterval!=40){result=918;break;}
        SetMotion(Motion::Watch,20000);g.motionStart=GetTickCount64()-40000;
        g.flyActive=true;g.flySerial=g.motionSerial;RefreshAnimationTimer();
        if(g.timerInterval!=16){result=919;break;}
    }
    g.window=nullptr;DestroyWindow(hidden);updates.announcement=notice;updates.operation=operation;
    return result;
}
int Run() {
    CreateDirectoryW(L"render-self-test",nullptr);
    CreateDirectoryW(L"render-self-test/boundaries",nullptr);
    Surface before,after;if(!before.Create(240,210)||!after.Create(240,210))return 910;
    struct HiddenWindow {
        HWND window=CreateWindowExW(WS_EX_LAYERED|WS_EX_TOOLWINDOW|WS_EX_NOACTIVATE,L"STATIC",L"",WS_POPUP,
            -2000,-2000,480,450,nullptr,nullptr,g.instance,nullptr);
        ~HiddenWindow(){g.window=nullptr;if(window)DestroyWindow(window);}
    } hidden;
    if(!hidden.window)return 915;
    std::ostringstream report;report<<"{\"schema\":1,\"version\":\"" YEOPJARI_VERSION_STRING "\",\"cases\":[";
    bool comma=false;
    for(auto character:cat::kAvailableCharacters)for(unsigned event=0;event<7;++event) {
        native_manual_motion_tests::Host host;host.Reset(character,true);
        if(!host.artworkReady)return 911;
        g.window=hidden.window;if(!g.surface.Create(480,450))return 910;
        g.cursorPlay=false;ResetPrimaryCarePresentation();
        const auto saved=host.Saved();const auto start=host.now;
        std::vector<BYTE> oldPrevious,newPrevious;
        double oldPeak=0,newPeak=0,prepareMax=0;unsigned captures=0,changes=0;
        std::uint64_t priorTag=0;
        const auto delta=[](const std::vector<BYTE>& prior,const BYTE* p) {
            if(prior.empty())return 0.;double total=0;std::size_t active=0;
            for(std::size_t i=0;i<prior.size();i+=4)if(std::max(prior[i+3],p[i+3])>4) {
                for(unsigned c=0;c<4;++c)total+=std::abs(int(prior[i+c])-p[i+c]);++active;
            }
            return active?total/(active*4*255):0.;
        };
        std::uint64_t actionEnd=0;
        for(unsigned frame=0;frame<800;++frame) {
            if(frame==30) {
                const Motion motion=event==0?Motion::Groom:event==1||event==2?Motion::Eat:
                    event==3?Motion::Treat:event==4?Motion::Knead:event==5?Motion::Sleep:Motion::Watch;
                if(event==6)StartFlyHunt(host.now);
                else {SetMotion(motion,event==5?4000:1000,host.now);host.Rebase();}
                if(event==2)g.fishMeal=true;
                actionEnd=g.nextDecision;
            }
            if(event==5&&frame==360) {SetMotion(Motion::Stretch,3200,host.now);host.Rebase();actionEnd=g.nextDecision;}
            const auto physical=g.physics.RenderPose();
            const auto pose=PrimaryDisplayedPose(host.now,physical);
            const auto render=[&](Surface& surface,bool presentation) {
                RenderFrame(host.now,presentation,false);
                Graphics graphics(surface.image.get());graphics.Clear(Color(0,0,0,0));
                graphics.SetInterpolationMode(InterpolationModeHighQualityBicubic);graphics.SetSmoothingMode(SmoothingModeAntiAlias);
                int inset=0;const auto* asset=CareSpriteFor(pose,character);
                if(asset&&(asset->layout->clip==cat::sprite_sequence::Clip::Fish||asset->layout->clip==cat::sprite_sequence::Clip::Jump||
                    asset->layout->clip==cat::sprite_sequence::Clip::CursorPounce||asset->layout->clip==cat::sprite_sequence::Clip::Eat))
                    inset=cat::render::SpriteBottomSafetyInset(*asset->layout,110*g.dpiScale*
                        cat::GrowthScale(g.pet.bornUtc,UtcNow()),cat::appearance::VisualModifiers(g.nutrition,character).bodyHeight);
                const double localCenter=g.centerX-std::round(g.centerX-g.surface.width/2.);
                graphics.DrawImage(g.surface.image.get(),float(120-localCenter),float(190-(g.surface.height-1-inset)));
                graphics.Flush(FlushIntentionSync);
            };
            render(before,false);
            const auto begin=std::chrono::steady_clock::now();render(after,true);
            prepareMax=std::max(prepareMax,std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-begin).count());
            const auto tag=pose.spriteClip==cat::sprite_sequence::Clip::None?0:1+static_cast<unsigned>(pose.spriteClip);
            if(frame&&tag!=priorTag) {oldPeak=std::max(oldPeak,delta(oldPrevious,before.pixels));newPeak=std::max(newPeak,delta(newPrevious,after.pixels));++changes;}
            priorTag=tag;
            oldPrevious.assign(before.pixels,before.pixels+240*210*4);newPrevious.assign(after.pixels,after.pixels+240*210*4);
            const bool entering=frame>=24&&frame<80;
            const bool exiting=actionEnd&&host.now+100>=actionEnd&&host.now<actionEnd+800;
            if(entering||exiting||event==5) {
                const auto stem=L"render-self-test/boundaries/c"+std::to_wstring(static_cast<unsigned>(character))+
                    L"-e"+std::to_wstring(event)+L"-f"+std::to_wstring(frame);
                if(!SaveNativeWalkingReview(*before.image,(stem+L"-before.png").c_str())||
                   !SaveNativeWalkingReview(*after.image,(stem+L"-after.png").c_str()))return 912;
                ++captures;
            }
            if(frame>80&&actionEnd&&host.now>actionEnd+900&&g.motion==Motion::Idle)break;
            const auto elapsed=frame*1000ULL/60;host.Frame((frame+1)*1000ULL/60-elapsed);
        }
        if(host.Saved()!=saved)return 913;
        if(comma)report<<',';comma=true;
        report<<"{\"coat\":"<<static_cast<unsigned>(character)<<",\"event\":"<<event
            <<",\"changes\":"<<changes<<",\"direct_boundary_delta\":"<<oldPeak<<",\"presented_boundary_delta\":"<<newPeak
            <<",\"max_render_ms\":"<<prepareMax<<",\"captured_pairs\":"<<captures<<",\"elapsed_ms\":"<<host.now-start<<'}';
    }
    report<<"],\"coats\":[0,8,10],\"profile_unchanged\":true,\"os_input_injected\":false}";
    return WriteBytes(L"render-self-test/boundaries/report.json",report.str())?0:914;
}
}
int NativeBoundaryReview(HINSTANCE instance,bool noticeOnly=false) {
    g.instance=instance;ULONG_PTR token=0;GdiplusStartupInput startup;
    if(GdiplusStartup(&token,&startup,nullptr)!=Ok)return 910;
    const HRESULT com=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
    std::string context;int result=LoadAtlas()?NativeCarePresentationRenderTest(context):910;
    if(!result)result=NativeAutomaticUpdateNoticeTest();
    if(!result)result=native_boundary_review::NoticeRender();
    if(!result&&!noticeOnly)result=native_boundary_review::Run();
    WriteBytes(L"render-self-test/boundary-review.txt","exit="+std::to_string(result)+" "+context);
    ReleaseAtlases();ReleaseCarePresentation();g.styleBlendFrom.Reset();g.styleBlendTo.Reset();
    g.surface.Reset();g.blendFrom.Reset();g.blendTo.Reset();g.toySurface.Reset();
    if(g.preciseTimer){timeEndPeriod(1);g.preciseTimer=false;}
    g.initialized=false;g.window=nullptr;cursorInteraction={};GdiplusShutdown(token);if(SUCCEEDED(com))CoUninitialize();return result;
}
