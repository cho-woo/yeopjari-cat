#pragma once

// Deterministic Box2D + actual GDI+ drawings, NOT a desktop screen recording.
// The upper lane reproduces the v2.0.16 diagnostic's omitted stop wait. Both
// lanes use identical art, distance mapping and physical requests; the lower
// lane follows the same request/advance/display ordering as the shipping host.
int NativeGroundDriveReview(std::string& context) {
    using Clip=cat::sprite_sequence::Clip;
    const auto* walk=cat::sprite_assets::Find(Clip::Walk,0,cat::sprite_assets::kCareSprites);
    const auto* run=cat::sprite_assets::Find(Clip::Run,0,cat::sprite_assets::kCareSprites);
    if(!walk||!run)return 480;
    context="Box2D walking start/speed/stop/reversal at 100/125/150 percent (synthetic DPI)";
    CreateDirectoryW(L"render-self-test/ground-drive",nullptr);
    std::string trace="dpi,tick,lane,x,velocity,travel,phase,frame,facing,waiting,action_elapsed,settled\n";
    const auto beforeCache=careSpriteMemory;
    for(double dpi:{1.,1.25,1.5}) {
        Surface canvas,from,to;
        const int width=static_cast<int>(720*dpi),height=static_cast<int>(510*dpi);
        if(!canvas.Create(width,height))return 481;
        cat::physics::Controller prior,after;prior.Reset(90,60,300);after.Reset(90,60,300);
        cat::motion::GroundDrive drive,priorDrive;cat::animation::Locomotion gait,priorGait;
        gait.ConfigureAuthoredStrides(walk->stridePixels,run->stridePixels,walk->standingFrame);
        priorGait.ConfigureAuthoredStrides(walk->stridePixels,run->stridePixels,walk->standingFrame);
        int lastFacing=1,flips=0;bool sawPriorFrontFlash=false;
        Motion requestedMotion=Motion::Idle,previousWalking=Motion::Walk;
        bool waiting=false;std::uint64_t started=0,decision=5000;
        for(unsigned tick=0;tick<630;++tick) {
            const double t=tick/60.,dt=1./60;
            const double desired=t<.5?0:t<3?22:t<4?42:t<5?0:t<7?-28:t<9?28:0;
            const auto a=after.CurrentPose(),oldAfter=after.RenderPose(),oldPrior=prior.RenderPose();
            const auto requested=desired==0?Motion::Idle:Motion::Walk;
            const std::uint64_t ticks=tick*1000/60;
            if(requested!=requestedMotion) {
                if(cat::animation::Walking(requestedMotion))previousWalking=requestedMotion;
                waiting=cat::motion::BeginGroundStop(requested,a.grounded,a.velocityX,oldAfter.velocityX,drive);
                requestedMotion=requested;started=ticks;decision=ticks+5000;
            }
            cat::motion::AdvanceGroundStop(waiting,a.grounded,a.velocityX,drive,ticks,started,decision);
            const double target=drive.Update(waiting?0:desired,a.velocityX,oldAfter.velocityX,a.grounded,dt);
            if(drive.Facing()!=lastFacing) {
                if(std::abs(a.velocityX)>.5||std::abs(oldAfter.velocityX)>.5)return 482;
                ++flips;lastFacing=drive.Facing();
            }
            after.SetTargetVelocity(target);
            prior.SetTargetVelocity(priorDrive.Update(desired,prior.CurrentPose().velocityX,oldPrior.velocityX,
                prior.CurrentPose().grounded,dt));
            after.Update(dt);prior.Update(dt);
            const auto b=after.RenderPose(),p=prior.RenderPose();
            if(cat::animation::Walking(requestedMotion))previousWalking=requestedMotion;
            const auto motion=cat::motion::DisplayedMotion(requestedMotion,previousWalking,b.grounded,waiting,b.velocityX);
            if(!gait.Advance(motion,b.relativeTravelX-oldAfter.relativeTravelX,dt,b.grounded))return 483;
            const auto actionElapsed=ticks>=started?ticks-started:0;
            auto pose=cat::animation::Evaluate(motion,actionElapsed,ticks,drive.Facing(),gait);
            cat::animation::ApplyAuthoredLocomotion(pose,motion,gait,true);
            if(pose.spriteClip==Clip::None){pose.spriteClip=Clip::Sleep;pose.spriteTimed=false;pose.spriteFrame=0;}
            const auto priorMotion=cat::motion::DisplayedMotion(requested,Motion::Walk,p.grounded,false,p.velocityX);
            if(!priorGait.Advance(priorMotion,p.relativeTravelX-oldPrior.relativeTravelX,dt,p.grounded))return 483;
            auto oldPose=cat::animation::Evaluate(priorMotion,0,ticks,priorDrive.Facing(),priorGait);
            // Exact v2.0.16 presentation omission: neutral poses were eligible
            // for side-view retention only after the 100ms standing hold.
            const bool oldNeutral=priorMotion==Motion::Idle||priorMotion==Motion::Watch;
            if(!(oldNeutral && priorGait.RecentAuthoredWalk() && !priorGait.AuthoredStanding()))
                cat::animation::ApplyAuthoredLocomotion(oldPose,priorMotion,priorGait,true);
            if(oldPose.spriteClip==Clip::None){oldPose.spriteClip=Clip::Sleep;oldPose.spriteTimed=false;oldPose.spriteFrame=0;}
            if(t>=4 && t<4.9) {
                if(pose.spriteClip!=Clip::Walk)return 489;
                if(oldPose.spriteClip==Clip::Sleep)sawPriorFrontFlash=true;
            }
            pose.flip=drive.Facing()<0;
            if(pose.blendFrame>=0||pose.x||pose.lift||pose.scaleX!=1||pose.scaleY!=1)return 484;
            if(tick%3!=0)continue; // 20Hz review encoding; physics remains 60Hz.
            {
                Graphics graphics(canvas.image.get());graphics.Clear(Color(255,235,239,243));
                graphics.SetSmoothingMode(SmoothingModeAntiAlias);graphics.SetInterpolationMode(InterpolationModeHighQualityBicubic);
                graphics.SetPixelOffsetMode(PixelOffsetModeHighQuality);
                Font font(L"Segoe UI",static_cast<REAL>(14*dpi),FontStyleRegular,UnitPixel);SolidBrush ink(Color(255,30,43,57));
                const std::wstring title=L"Native GDI+ / Box2D / "+std::to_wstring(static_cast<int>(dpi*100))+L"% / "+std::to_wstring(tick*1000/60)+L"ms";
                graphics.DrawString(title.c_str(),-1,&font,PointF(12*dpi,8*dpi),&ink);
                for(unsigned lane=0;lane<2;++lane) {
                    const auto& drawing=lane?pose:oldPose;const auto& physical=lane?b:p;
                    const REAL floor=static_cast<REAL>((lane?470:235)*dpi);
                    Pen ground(Color(255,160,170,180),1);graphics.DrawLine(&ground,0.f,floor,static_cast<REAL>(width),floor);
                    graphics.DrawString(lane?L"Current: shared request / stop / display":L"Prior diagnostic: no stop wait",-1,&font,PointF(12*dpi,(lane?265:45)*dpi),&ink);
                    for(auto character:cat::kAvailableCharacters) {
                        const REAL center=static_cast<REAL>((physical.x+(character==cat::Character::Tuxedo?360:0))*dpi);
                        const REAL scale=static_cast<REAL>(110*dpi/CurrentArt(character).base.standingHeight);
                        DrawStyledAnimationDirect(graphics,drawing,center,floor,scale,character,cat::Outfit::None,
                            cat::nutrition::VisualModifiers(g.nutrition),from,to,width,height);
                    }
                    const auto* layout=cat::sprite_assets::Find(drawing.spriteClip,0,cat::sprite_assets::kCareSprites);
                    trace+=std::to_string(dpi)+","+std::to_string(tick)+","+std::to_string(lane)+","+
                        std::to_string(physical.x)+","+std::to_string(physical.velocityX)+","+std::to_string(physical.relativeTravelX)+","+
                        std::to_string(lane?gait.AuthoredPhase():priorGait.AuthoredPhase())+","+std::to_string(SpriteFrameFor(drawing,*layout))+","+
                        std::to_string(lane?drive.Facing():priorDrive.Facing())+","+
                        std::to_string(lane&&waiting)+","+std::to_string(lane?actionElapsed:0)+","+
                        std::to_string(lane&&drive.Settled())+"\n";
                }
                graphics.Flush(FlushIntentionSync);
            }
            // One playable 100% sequence, plus exact synthetic-DPI milestones.
            if(dpi==1 || tick==180 || tick==288 || tick==450) {
                const auto path=L"render-self-test/ground-drive/dpi"+std::to_wstring(static_cast<int>(dpi*100))+L"-"+
                    std::to_wstring(10000+tick/3)+L".png";
                if(!SaveNativeWalkingReview(*canvas.image,path.c_str()))return 485;
            }
        }
        if(flips!=2 || !sawPriorFrontFlash)return 486;
    }
    if(!WriteBytes(L"render-self-test/ground-drive/trace.csv",trace))return 487;
    const auto afterCache=careSpriteMemory;
    if(beforeCache.residentPixels!=afterCache.residentPixels || beforeCache.tuxedoPixels!=afterCache.tuxedoPixels || beforeCache.decodedAtlases!=afterCache.decodedAtlases)return 488;
    return 0;
}
