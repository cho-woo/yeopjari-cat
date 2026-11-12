#pragma once

// Included in the production command Host namespace. No OS window, profile,
// cursor movement or waiting for wall-clock minutes is involved.
namespace random_zoomies_native {
struct NarrowResult {
    std::uint64_t chosenRun=0,movingMs=0;
    unsigned edgeTurns=0;
    double maxStep=0;
};
int NarrowArea(cat::Character character,NarrowResult& result) {
    Host host;host.Reset(character,false);g.workInterference=false;
    g.randomZoomiesRandom.seed(841+static_cast<unsigned>(character));
    g.work={0,0,960,1080};g.dpiScale=1.5;g.centerX=480;ResetMovement();
    g.perch={};g.perch.nextTry=host.now+1000000;
    for(unsigned i=0;i<12;++i)g.physics.Update(1./60);
    SetMotion(Motion::Sleep,400000,host.now);host.Rebase();
    do {host.Frame(50);}while(!g.randomZoomies.Due());
    SetMotion(Motion::Idle,0,host.now);g.autonomousResting=true;g.nextDecision=host.now;host.Frame(50);
    if(!g.randomZoomies.Active())return 639;
    result.chosenRun=g.randomZoomies.RemainingRunMs();const auto start=host.now;
    while(g.randomZoomies.Active()&&host.now-start<result.chosenRun+120000) {
        const auto before=g.physics.CurrentPose();const int priorDirection=g.direction;
        const auto bounds=MovementBoundsLogical();
        const bool edge=cat::motion::BrakeBeforeEdge(before.x,WalkingVelocity(before),
            g.direction*g.speed/g.dpiScale,bounds.first,bounds.second);
        const auto budget=g.randomZoomies.RemainingRunMs();host.Frame(50);
        const auto after=g.physics.CurrentPose();
        result.maxStep=std::max(result.maxStep,std::abs(after.x-before.x));
        if(edge&&g.direction!=priorDirection)++result.edgeTurns;
        if(!std::isfinite(after.x)||!after.grounded||after.x<bounds.first-.1||after.x>bounds.second+.1||result.maxStep>15)return 639;
        if(g.randomZoomies.Active())result.movingMs+=budget-g.randomZoomies.RemainingRunMs();
        else if(budget<=50)result.movingMs+=budget;
    }
    return !g.randomZoomies.Active()&&result.movingMs==result.chosenRun&&result.edgeTurns>=1&&
        g.autonomousResting&&g.motion==Motion::Idle?0:639;
}
int Run() {
    for(const auto* path:{L"render-self-test",L"render-self-test/random-zoomies"})
        if(!CreateDirectoryW(path,nullptr)&&GetLastError()!=ERROR_ALREADY_EXISTS)return 630;
    std::ostringstream report;report.imbue(std::locale::classic());
    report<<"{\"schema\":1,\"test\":\"random-zoomies-native\",\"status\":\"passed\",\"version\":\""
        YEOPJARI_VERSION_STRING "\",\"host\":\"production dispatcher / GroundDrive / Box2D\","
        "\"wait_min_ms\":120000,\"wait_max_ms\":420000,\"run_min_ms\":5000,\"run_max_ms\":15000,\"cases\":[";
    bool first=true;
    for(const auto character:{cat::Character::Cream,cat::Character::Tuxedo}) {
        Host host;host.Reset(character,false);g.workInterference=false;
        g.randomZoomiesRandom.seed(5141+static_cast<unsigned>(character));
        g.perch={};g.perch.nextTry=host.now+1000000;
        SetMotion(Motion::Sleep,400000,host.now);host.Rebase();
        host.Frame(50);const auto chosenWait=g.randomZoomies.WaitMs();
        if(chosenWait<120000||chosenWait>420000)return 631;
        const auto waitStart=host.now;
        while(!g.randomZoomies.Due()&&host.now-waitStart<=420100)host.Frame(50);
        if(!g.randomZoomies.Due()||g.randomZoomies.Active()||g.motion!=Motion::Sleep)return 632;
        for(unsigned i=0;i<40;++i)host.Frame(50);
        if(!g.randomZoomies.Due()||g.randomZoomies.Active())return 632;
        SetMotion(Motion::Idle,0,host.now);g.autonomousResting=true;g.nextDecision=host.now;
        host.Frame(50);
        if(!g.randomZoomies.Active()||g.motion!=Motion::Zoomies||g.manualMotionActive)return 633;
        const auto chosenRun=g.randomZoomies.RemainingRunMs(),started=host.now;
        if(chosenRun<5000||chosenRun>15000)return 633;
        double distance=0,maxStep=0;int priorDirection=0;unsigned turns=0;
        std::uint64_t movingMs=0;
        while(g.randomZoomies.Active()&&host.now-started<chosenRun+120000) {
            const auto before=g.physics.CurrentPose();
            const auto budget=g.randomZoomies.RemainingRunMs();host.Frame(50);
            const auto after=g.physics.CurrentPose();const auto step=std::abs(after.x-before.x);
            maxStep=std::max(maxStep,step);distance+=step;
            if(!std::isfinite(after.x)||!after.grounded||step>15||g.pet.roamingPaused)return 634;
            if(g.randomZoomies.Active())movingMs+=budget-g.randomZoomies.RemainingRunMs();
            else if(budget<=50)movingMs+=budget;
            if(std::abs(after.velocityX)>30) {
                const int direction=after.velocityX<0?-1:1;
                if(priorDirection&&direction!=priorDirection)++turns;priorDirection=direction;
            }
        }
        if(g.randomZoomies.Active()||movingMs!=chosenRun||turns<1||distance<300||
                !g.autonomousResting||g.motion!=Motion::Idle||host.now-started<chosenRun||
                g.randomZoomies.WaitMs()<120000||g.randomZoomies.WaitMs()>420000)return 635;
        const auto elapsedWithConnections=host.now-started;
        const auto wait=g.randomZoomies.WaitMs();g.hidden=true;
        for(unsigned i=0;i<80;++i)AdvanceRandomZoomies(host.now+i*50,.05);
        if(g.randomZoomies.WaitMs()!=wait||g.randomZoomies.Active())return 636;
        g.hidden=false;
        for(unsigned i=0;i<300;++i) {
            host.Frame(50);
            if(g.stopClip==cat::sprite_sequence::Clip::None&&g.groundArtBridge.Neutral()&&
                    !g.waitingForStop&&std::abs(WalkingVelocity(g.physics.RenderPose()))<=.5)break;
        } // Finish the complete source recovery and standing outro.
        // A direct command interrupts an owned random run. Its preference and
        // ownership survive the scheduler noticing the new serial next frame.
        while(!g.randomZoomies.Due()) {
            AdvanceRandomZoomies(host.now,.25);
            // Keep this opportunity pending, as an ordinary care action does.
            g.autonomousResting=false;
        }
        g.autonomousResting=true;g.nextDecision=host.now;
        AdvanceRandomZoomies(host.now,.05);
        if(!g.randomZoomies.Active())return 637;
        host.Command(kManualWalkCommand);const auto serial=g.motionSerial;
        host.Frame(50);
        if(g.randomZoomies.Active()||!g.manualMotionActive||g.motion!=Motion::Walk||
                g.motionSerial!=serial||g.pet.roamingPaused)return 637;
        NarrowResult narrow;if(const int error=NarrowArea(character,narrow))return error;
        if(!first)report<<',';first=false;
        report<<"{\"character\":"<<static_cast<unsigned>(character)<<",\"chosen_wait_ms\":"<<chosenWait
            <<",\"chosen_run_ms\":"<<chosenRun<<",\"actual_running_ms\":"<<movingMs
            <<",\"elapsed_with_connections_ms\":"<<elapsedWithConnections<<",\"turns\":"<<turns
            <<",\"distance\":"<<distance<<",\"max_root_step\":"<<maxStep
            <<",\"narrow_area_completed\":true,\"narrow_chosen_run_ms\":"<<narrow.chosenRun
            <<",\"narrow_actual_running_ms\":"<<narrow.movingMs<<",\"narrow_edge_turns\":"<<narrow.edgeTurns
            <<",\"narrow_max_root_step\":"<<narrow.maxStep
            <<",\"busy_deferred\":true,\"suspended_wait_preserved\":true,\"manual_interrupt_preserved\":true,"
            "\"completion_quiet\":true,\"pause_preference_preserved\":true}";
    }
    report<<"]}\n";
    return WriteBytes(L"render-self-test/random-zoomies/report.json",report.str())?0:638;
}
} // namespace random_zoomies_native
