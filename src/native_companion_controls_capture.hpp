// Included inside native_controls_capture. Reuses the real companion model,
// primary social reaction and each animal's normal renderer at the same scale.
int CompanionReview(std::string& context) {
    constexpr unsigned Count=360;
    struct Pair {std::array<Row,2> actors;std::array<std::vector<Frame>,2> frames;};
    std::array<Pair,3> pairs;
    CreateDirectoryW(L"render-self-test/companion-play",nullptr);
    Surface actor;if(!actor.Create(ActorWidth,ActorHeight))return 820;
    for(unsigned r=0;r<3;++r) {
        auto& pair=pairs[r];const auto first=cat::kAvailableCharacters[r],second=cat::kAvailableCharacters[(r+1)%3];
        native_manual_motion_tests::Host host;host.Reset(first,false);
        if(!EnsureCareCoats(first,second))return 820;
        g.companionStore.state=cat::companion::NewState(g.pet.bornUtc);
        auto& state=g.companionStore.state;state.enabled=true;state.preferences.character=second;
        state.pet.updatedUtc=state.nutrition.updatedUtc=state.memory.updatedUtc=state.life.updatedUtc=UtcNow();
        state.pet.position=(g.physics.CurrentPose().x-82)/1200.;state.nutrition.bodyCondition=50;
        g.companionModel.Bind(state);g.companionModel.Reset(0,1200,700);
        cat::companion::Context c;c.ticks=host.now;c.nowUtc=UtcNow();c.primary=g.physics.CurrentPose();c.elapsedSeconds=1./60;
        if(!RequestCompanionPlay(host.now))return 821;
        pair.actors[0].character=first;pair.actors[1].character=second;
        pair.actors[0].body=cat::appearance::VisualModifiers(g.nutrition,first);
        pair.actors[1].body=cat::appearance::VisualModifiers(state.nutrition,second);
        unsigned events=0,reactions=0,firstFinal=0,secondFinal=0;
        for(unsigned index=0;index<Count;++index) {
            for(unsigned sub=0;sub<2;++sub) {
                const unsigned sample=index*2+sub,step=(sample+1)*1000/60-sample*1000/60;
                host.now+=step;c.ticks=host.now;c.elapsedSeconds=step/1000.;c.primary=g.physics.CurrentPose();
                c.primaryMotion=g.motion;c.primaryBusy=!g.companionReaction&&g.motion==Motion::Punch;
                g.companionModel.Tick(c);const auto event=g.companionModel.TakeEvent();
                events+=event==cat::companion::Interaction::Paw;
                const auto serial=g.motionSerial;ReactToCompanion(event,c,host.now);
                reactions+=event==cat::companion::Interaction::Paw&&serial!=g.motionSerial&&g.motion==Motion::Punch;
                DispatchAutonomousMotion(host.now);AdvancePrimaryMotion(host.now,c.elapsedSeconds);
            }
            const auto companion=g.companionModel.View(host.now);
            for(unsigned a=0;a<2;++a) {
                Frame f;const auto physical=a?companion.physical:g.physics.RenderPose();
                f.pose=a?companion.animation:PrimaryDisplayedPose(host.now,physical);
                f.x=physical.x;f.lift=physical.lift;f.velocityX=physical.velocityX;
                if(const auto* art=CareSpriteFor(f.pose,pair.actors[a].character)) {
                    f.source=SpriteFrameFor(f.pose,*art->layout);f.resource=art->layout->resourceId;
                    if(f.pose.spriteClip==Clip::Punch&&f.source+1==art->layout->frameCount) {if(a)++secondFinal;else ++firstFinal;}
                }
                if(!DrawActor(actor,pair.actors[a],f,index*1000/30)||
                        !CacheActor(actor,static_cast<cat::Character>(100+r*2+a),index))return 822;
                f.actorHash=cat::Checksum(std::string(reinterpret_cast<const char*>(actor.pixels),ActorWidth*ActorHeight*4));
                pair.frames[a].push_back(f);
            }
        }
        context="companion pair="+std::to_string(r)+" events="+std::to_string(events)+" final drawings="+
            std::to_string(firstFinal)+","+std::to_string(secondFinal);
        std::ostringstream debug;debug<<"frame,actor,clip,source,x\n";
        for(unsigned f=0;f<Count;++f)for(unsigned a=0;a<2;++a) {
            const auto& frame=pair.frames[a][f];debug<<f<<','<<a<<','<<static_cast<unsigned>(frame.pose.spriteClip)<<','<<frame.source<<','<<frame.x<<'\n';
        }
        WriteBytes((L"render-self-test/companion-play/pair-"+std::to_wstring(r)+L".csv").c_str(),debug.str());
        if(events!=2||reactions!=2||!firstFinal||!secondFinal||state.mutualPlay!=1||state.pet.feedCount||state.memory.meals)return 823;
        // Inspect the complete existing hunting film through this animal's
        // actual pose adapter, including its last planted drawing.
        if(!g.companionModel.Perform(Clip::FlyHunt,host.now))return 824;
        for(unsigned index=0;index<500;++index) {
            host.now+=16;c.ticks=host.now;c.elapsedSeconds=.016;c.primaryVisible=false;
            g.companionModel.Tick(c);
            if(index==80||index==180||index==300||index==430) {
                const auto view=g.companionModel.View(host.now);Frame frame;frame.pose=view.animation;
                frame.x=view.physical.x;frame.lift=view.physical.lift;
                if(!DrawActor(actor,pair.actors[1],frame,index*16)||!SaveNativeWalkingReview(*actor.image,
                    (L"render-self-test/companion-play/fly-"+std::to_wstring(static_cast<unsigned>(second))+L"-"+std::to_wstring(index)+L".png").c_str()))return 824;
            }
        }
        state.enabled=false;
    }
    Surface canvas;if(!canvas.Create(Width,Height))return 820;
    std::ostringstream trace;trace.imbue(std::locale::classic());trace<<"{\"fps\":30,\"frames\":[";
    for(unsigned index=0;index<Count;++index) {
        if(index)trace<<',';trace<<"{\"frame\":"<<index<<",\"pairs\":[";
        {
            Graphics graphics(canvas.image.get());graphics.Clear(Color(255,247,246,241));
            Font title(L"Segoe UI",19),small(L"Segoe UI",13);SolidBrush ink(Color(255,46,53,58));Pen ground(Color(255,197,201,196));
            graphics.DrawString(L"Two independent cats / complete playful swats and rest / actual app renderer",-1,&title,PointF(22,16),&ink);
            for(unsigned r=0;r<3;++r) {
                if(r)trace<<',';trace<<'[';
                const REAL floor=335+r*340;graphics.DrawLine(&ground,20.f,floor,Width-20.f,floor);
                for(unsigned a=0;a<2;++a) {
                    const auto& frame=pairs[r].frames[a][index];
                    if(!RestoreActor(actor,static_cast<cat::Character>(100+r*2+a),index))return 822;
                    graphics.DrawImage(actor.image.get(),static_cast<REAL>(600+frame.x-400-ActorWidth/2),floor-ActorFloor);
                    if(a)trace<<',';
                    trace<<"{\"coat\":"<<static_cast<unsigned>(pairs[r].actors[a].character)<<",\"clip\":"<<static_cast<unsigned>(frame.pose.spriteClip)
                        <<",\"source\":"<<frame.source<<",\"hash\":\""<<frame.actorHash<<"\",\"x\":"<<frame.x<<'}';
                }
                trace<<']';
                const auto label=L"Pair "+std::to_wstring(r+1)+L" / first + companion";
                graphics.DrawString(label.c_str(),-1,&small,PointF(22,floor-255),&ink);
            }
        }
        trace<<"]}";const auto digits=std::to_wstring(index);
        if(!SaveNativeWalkingReview(*canvas.image,(L"render-self-test/companion-play/frame-"+std::wstring(5-digits.size(),L'0')+digits+L".png").c_str()))return 825;
    }
    trace<<"]}";WriteBytes(L"render-self-test/companion-play/trace.json",trace.str());
    WriteBytes(L"render-self-test/companion-play/capture.json","{\"status\":\"passed\",\"fps\":30,\"frame_count\":360,\"pairs\":[[0,8],[8,10],[10,0]],\"complete_swat_bouts_per_cat\":2,\"mutual_play_record_per_event\":1,\"food_credits\":0,\"os_input_injected\":false,\"profile_loaded_or_saved\":false}");
    return 0;
}
