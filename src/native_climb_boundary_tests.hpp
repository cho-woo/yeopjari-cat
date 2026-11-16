#pragma once
// Included inside native_manual_motion_tests. Actual companion controller,
// Box2D and shipping GDI+ path, with the complete authored return to neutral.
namespace climb_boundary {
int Run(std::string& detail) {
    CreateDirectoryW(L"render-self-test",nullptr);CreateDirectoryW(L"render-self-test/climb-boundary",nullptr);
    Surface image;if(!image.Create(320,300))return 560;
    using Clip=cat::sprite_sequence::Clip;
    std::ostringstream report;report.imbue(std::locale::classic());
    report<<"{\"version\":\"" YEOPJARI_VERSION_STRING "\",\"capture\":\"actual companion model, Box2D and GDI+, synthetic clock\",\"cases\":[";
    bool comma=false;
    for(const auto character:cat::kAvailableCharacters)for(const bool paused:{false,true}) {
        Host host;host.Reset(character,true);if(!host.artworkReady)return 561;
        auto state=cat::companion::NewState(g.pet.bornUtc);state.enabled=true;state.preferences.character=character;
        state.pet.roamingPaused=paused;state.pet.position=1;state.nutrition.bodyCondition=50;
        const auto utc=UtcNow();state.pet.updatedUtc=state.nutrition.updatedUtc=state.memory.updatedUtc=state.life.updatedUtc=utc;
        const auto saved=cat::companion::Serialize(state);
        cat::companion::Model model;model.Bind(state);model.Reset(96,904,600);
        if(!model.Perform(Clip::Climb,host.now))return 561;
        const auto* climb=LoadedCareSprite(Clip::Climb,character);const auto* walk=LoadedCareSprite(Clip::Walk,character);
        if(!climb||!walk)return 561;
        const auto duration=cat::sprite_sequence::ProgramDuration(climb->layout->program);
        const auto outro=walk->layout->program.outro;
        std::array<bool,cat::sprite_sequence::kMaxAuthoredFrames> seen{};
        CarePresentationRenderer presentation;std::uint64_t began=0,finished=0;
        bool lastClimb=false,returned=false;unsigned painted=0,outroFrames=0;double prior=904,maxStep=0;
        const auto folder=L"render-self-test/climb-boundary/c"+std::to_wstring(static_cast<unsigned>(character))+(paused?L"-paused":L"-roaming");
        CreateDirectoryW(folder.c_str(),nullptr);
        for(unsigned frame=0;frame<2600;++frame) {
            cat::companion::Context c;c.nowUtc=utc;c.ticks=host.now+(frame+1)*16;c.elapsedSeconds=.016;c.primaryVisible=false;
            model.Tick(c);const auto view=model.View(c.ticks);const auto& pose=view.animation;
            const auto* art=CareSpriteFor(pose,character);const unsigned source=art?SpriteFrameFor(pose,*art->layout):unsigned(pose.frame);
            if(pose.spriteClip==Clip::Climb) {
                if(!began)began=c.ticks;
                lastClimb|=source+1==climb->layout->frameCount;
            }
            if(began&&lastClimb&&pose.spriteClip!=Clip::Climb&&!finished) {
                if(pose.spriteClip!=Clip::Walk)return 563;
                finished=c.ticks;
            }
            if(finished&&pose.spriteClip==Clip::Walk) {
                if(pose.flip)return 565;
                seen[source]=true;
            }
            if(began&&c.ticks>=began+duration-250) {
                maxStep=std::max(maxStep,std::abs(view.physical.x-prior));prior=view.physical.x;
                Graphics graphics(image.image.get());graphics.SetCompositingMode(CompositingModeSourceCopy);graphics.Clear(Color(0,0,0,0));
                graphics.SetCompositingMode(CompositingModeSourceOver);graphics.SetInterpolationMode(InterpolationModeHighQualityBicubic);
                graphics.SetSmoothingMode(SmoothingModeAntiAlias);graphics.SetPixelOffsetMode(PixelOffsetModeHighQuality);
                DrawPetStyle(graphics,character,cat::Outfit::None,pose,view.body,160,260,110,&presentation,c.ticks,view.presentationGeneration,false);
                graphics.Flush(FlushIntentionSync);
                const auto digits=std::to_wstring(painted++);
                if(!SaveNativeWalkingReview(*image.image,(folder+L"/frame-"+std::wstring(5-digits.size(),L'0')+digits+L".png").c_str()))return 562;
            }
            if(finished&&c.ticks>finished+cat::sprite_sequence::Duration(walk->layout->program,outro)+350&&!model.Busy()) {
                returned=view.physical.grounded;break;
            }
        }
        for(unsigned i=0;i<outro.count;++i)outroFrames+=seen[walk->layout->program.steps[outro.first+i].frame];
        detail="climb authored return coat="+std::to_string(static_cast<unsigned>(character))+" outro="+std::to_string(outroFrames);
        if(!lastClimb||!returned||outroFrames!=outro.count||maxStep>.1||cat::companion::Serialize(state)!=saved)return 563;
        if(comma)report<<',';comma=true;
        report<<"{\"coat\":"<<static_cast<unsigned>(character)<<",\"paused\":"<<(paused?"true":"false")
            <<",\"final_climb_frame\":true,\"return_frames\":"<<outroFrames<<",\"required_return_frames\":"<<outro.count
            <<",\"max_root_step\":"<<maxStep<<",\"painted_frames\":"<<painted<<",\"complete\":true}";
    }
    report<<"],\"passed\":true,\"profile_unchanged\":true}";
    return WriteBytes(L"render-self-test/climb-boundary/report.json",report.str())?0:564;
}
}
