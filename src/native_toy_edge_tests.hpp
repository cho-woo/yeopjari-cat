#pragma once
// Included inside native_manual_motion_tests. The shipping toy dispatcher,
// motor and source pose must reach a clipped edge target at every tested size.
namespace toy_edge {
int Run(std::string& detail) {
    CreateDirectoryW(L"render-self-test",nullptr);
    using Clip=cat::sprite_sequence::Clip;unsigned checked=0;
    for(const auto character:cat::kAvailableCharacters)for(const unsigned age:{7u,366u})
      for(const double dpi:{1.,1.5})for(const int side:{-1,1}) {
        Host host;host.Reset(character,false);
        const auto utc=UtcNow();g.pet.bornUtc=utc-static_cast<std::int64_t>(age)*86400;
        g.pet.updatedUtc=utc;g.nutrition=cat::nutrition::NewState(g.pet.bornUtc,utc,80);g.nutrition.bodyCondition=50;
        g.work={-1230,-500,-230,120};g.dpiScale=dpi;
        const auto bounds=MovementBoundsLogical();const double start=side<0?bounds.first+25:bounds.second-25;
        g.centerX=g.work.left+start*dpi;g.direction=g.facing=side;ResetMovement();
        for(unsigned i=0;i<12;++i)g.physics.Update(1./60);
        host.now=GetTickCount64();PlaceToy(ToyKind::FishingRod);host.Rebase();
        const auto saved=host.Saved();double prior=g.physics.RenderPose().x;bool played=false,recovered=false;
        unsigned lastSource=0;
        for(unsigned i=0;i<1800;++i) {
            host.Frame(16);const auto physical=g.physics.RenderPose();const auto pose=PrimaryDisplayedPose(host.now,physical);
            detail="rod edge coat="+std::to_string(static_cast<unsigned>(character))+" age="+std::to_string(age)+
                " dpi="+std::to_string(dpi)+" side="+std::to_string(side)+" x="+std::to_string(physical.x);
            if(!std::isfinite(physical.x)||std::abs(physical.x-prior)>3)return 536;
            prior=physical.x;
            if(pose.spriteClip==Clip::ToyPlay) {
                played=true;const auto* art=LoadedCareSprite(Clip::ToyPlay,character);
                if(!art)return 537;
                lastSource=std::max(lastSource,SpriteFrameFor(pose,*art->layout));
                recovered|=lastSource+1==art->layout->frameCount;
            }
            if(played&&recovered&&g.toySession.rounds>0)break;
        }
        if(!played||!recovered||host.Saved()!=saved)return 538;
        g.toy=ToyKind::None;g.toySession.Cancel();g.toyEngaged=g.approachingToy=false;++checked;
      }
    const bool saved=WriteBytes(L"render-self-test/toy-edge.json","{\"passed\":true,\"cases\":"+std::to_string(checked)+
        ",\"coats\":3,\"ages_days\":[7,366],\"dpi\":[1,1.5],\"sides\":[-1,1],\"complete_source_recovery\":true,\"profile_unchanged\":true}");
    return saved?0:539;
}
}
