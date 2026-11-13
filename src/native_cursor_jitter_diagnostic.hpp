#pragma once

// Read-only production-path probe. Samples are memory-only: no OS pointer,
// visible window, profile loading/saving or change to the running pet.
int NativeCursorJitterDiagnostic(HINSTANCE instance) {
    g.instance=instance;
    const HRESULT com=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
    if(FAILED(com))return 690;
    ULONG_PTR token=0;GdiplusStartupInput startup;
    if(GdiplusStartup(&token,&startup,nullptr)!=Ok){CoUninitialize();return 690;}
    int result=0;
    try {
        if(!LoadAtlas())result=691;
        else {
            CreateDirectoryW(L"render-self-test",nullptr);
            CreateDirectoryW(L"render-self-test/cursor-jitter",nullptr);
            std::ostringstream report,trace;
            report<<"{\"version\":\"" YEOPJARI_VERSION_STRING "\",\"input\":\"independent stationary-center sinusoidal pointer\",\"profile_unchanged\":true,\"runs\":[";
            trace<<"character,amplitude,frequency,height,ms,action,serial,clip,source,flip,x,lift,vx,turn,waiting,update_us,render_us\n";
            bool comma=false;
            for(const auto character:cat::kAvailableCharacters) {
                native_cursor_tests::Host host;host.Reset(character);
                if(!EnsureCareCoats(character)){result=692;break;}
                for(const double height:{25.,65.,110.})for(const double hz:{1.5,3.}) {
                    if(result)break;
                    host.Reset(character);
                    native_cursor_play_capture::Row row;row.character=character;
                    row.body=cat::appearance::VisualModifiers(g.nutrition,character);
                    Surface actor;if(!actor.Create(360,420)){result=693;break;}
                    std::vector<double> updateTimes,renderTimes;
                    unsigned changes=0,flips=0,cancels=0,turns=0,attempts=0;
                    ULONGLONG frozen=0,longest=0;unsigned priorFrame=~0u;
                    auto priorClip=cat::sprite_sequence::Clip::None;
                    auto priorAction=cat::environment::CursorAction::None;
                    bool priorFlip=false,priorAir=false;double priorX=400;
                    LARGE_INTEGER clockFrequency;QueryPerformanceFrequency(&clockFrequency);
                    for(unsigned frame=0;frame<1250;++frame) {
                        const auto elapsed=frame*16ULL;
                        const double pointerX=400+48*std::sin(elapsed*.001*hz*2*cat::animation::kPi);
                        LARGE_INTEGER a,b,c;QueryPerformanceCounter(&a);
                        host.Frame(pointerX,height);
                        const auto physical=g.physics.RenderPose();
                        const auto pose=PrimaryDisplayedPose(host.now,physical);
                        QueryPerformanceCounter(&b);
                        native_cursor_play_capture::Frame drawing;drawing.pose=pose;drawing.lift=physical.lift;
                        if(!native_cursor_play_capture::DrawActor(actor,row,drawing,elapsed)){result=694;break;}
                        QueryPerformanceCounter(&c);
                        const double update=(b.QuadPart-a.QuadPart)*1e6/clockFrequency.QuadPart;
                        const double render=(c.QuadPart-b.QuadPart)*1e6/clockFrequency.QuadPart;
                        updateTimes.push_back(update);renderTimes.push_back(render);
                        const auto action=cursorInteraction.session.Action();
                        const auto* art=CareSpriteFor(pose,character);
                        const auto source=art?SpriteFrameFor(pose,*art->layout):pose.frame;
                        if(frame) {
                            changes+=pose.spriteClip!=priorClip;
                            flips+=pose.flip!=priorFlip;
                            cancels+=priorAction==cat::environment::CursorAction::Pounce&&action==cat::environment::CursorAction::Chase&&!priorAir;
                        }
                        attempts+=action==cat::environment::CursorAction::Pounce&&action!=priorAction;
                        turns+=g.runningTurn.Active();
                        if(frame&&source==priorFrame&&pose.spriteClip==priorClip&&pose.flip==priorFlip&&
                                std::abs(physical.x-priorX)<.1&&physical.grounded)frozen+=16;
                        else frozen=0;
                        longest=std::max(longest,frozen);
                        trace<<static_cast<unsigned>(character)<<",48,"<<hz<<','<<height<<','<<elapsed<<','
                            <<static_cast<unsigned>(action)<<','<<g.motionSerial<<','<<static_cast<unsigned>(pose.spriteClip)<<','<<source<<','<<pose.flip<<','
                            <<physical.x<<','<<physical.lift<<','<<physical.velocityX<<','<<static_cast<unsigned>(g.runningTurn.CurrentPhase())<<','<<g.waitingForStop<<','<<update<<','<<render<<'\n';
                        priorAction=action;priorClip=pose.spriteClip;priorFrame=source;
                        priorFlip=pose.flip;priorX=physical.x;priorAir=cursorInteraction.session.InFlight();
                    }
                    if(result)break;
                    std::sort(updateTimes.begin(),updateTimes.end());std::sort(renderTimes.begin(),renderTimes.end());
                    if(comma)report<<',';comma=true;
                    report<<"{\"character\":"<<static_cast<unsigned>(character)<<",\"amplitude_px\":48,\"frequency_hz\":"<<hz
                        <<",\"height_px\":"<<height<<",\"frames\":1250,\"clip_changes\":"<<changes<<",\"mirror_flips\":"<<flips
                        <<",\"pounce_attempts\":"<<attempts<<",\"cancelled_before_takeoff\":"<<cancels<<",\"physical_launches\":"<<host.launches
                        <<",\"turn_frames\":"<<turns<<",\"longest_fixed_ground_drawing_ms\":"<<longest
                        <<",\"update_p99_us\":"<<updateTimes[updateTimes.size()*99/100]<<",\"render_p99_us\":"<<renderTimes[renderTimes.size()*99/100]<<'}';
                }
            }
            report<<"]}\n";
            if(!WriteBytes(L"render-self-test/cursor-jitter/report.json",report.str())||
                    !WriteBytes(L"render-self-test/cursor-jitter/trace.csv",trace.str()))result=695;
        }
    }catch(...){result=696;}
    ReleaseAtlases();ReleaseCarePresentation();g.styleBlendFrom.Reset();g.styleBlendTo.Reset();
    g.initialized=false;g.window=nullptr;cursorInteraction={};
    GdiplusShutdown(token);CoUninitialize();return result;
}
