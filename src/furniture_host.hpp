#pragma once
// Included after the regular primary motor; no synthetic position resets.
bool RequestPrimaryFurniture(cat::furniture::Kind kind) {
    if(!g.initialized||!g.physicsReady||g.closing||updates.applying||g.hidden)return false;
    return g.furniture.Request(kind);
}
void UpdatePrimaryFurniture(ULONGLONG ticks,double seconds) {
    using Phase=cat::furniture::Phase;
    const auto body=g.physics.CurrentPose(),visible=g.physics.RenderPose();
    if(seconds>0&&g.furniture.State()==Phase::Queued&&g.motion==Motion::Sleep&&
            ManualMotionAvailable()&&!g.waitingForStop&&!g.jumpPending&&
            !KeyboardPlayActive()&&!EnvironmentBusy()&&!g.toyEngaged) {
        const auto* sleep=LoadedCareSprite(cat::sprite_sequence::Clip::Sleep,g.preferences.character);
        const auto intro=sleep?cat::animation::ReferenceSleepIntroMs(&sleep->layout->program,sleep->layout->frameCount):2000;
        if(ticks>=g.motionStart&&ticks-g.motionStart>=intro) {
            if(g.brain)g.brain->Interrupt(ticks);
            SetMotion(Motion::Stretch,3500,ticks);g.manualMotionActive=true;
        }
    }
    const bool calm=g.motion==Motion::Idle||g.motion==Motion::Watch;
    const bool available=!g.furniture.Owned()&&ManualMotionAvailable()&&calm&&g.autonomousResting&&
        !g.toyEngaged&&!g.approachingToy&&!g.approachingUser&&!g.memorySequenceCount&&!g.lifeSequenceCount&&
        !g.companionReaction&&!EnvironmentBusy()&&!KeyboardPlayActive()&&!g.manualMotionActive&&
        !g.waitingForStop&&!g.jumpPending&&!g.runningTurn.Active()&&
        !g.randomZoomies.Active()&&std::abs(WalkingVelocity(body))<=.5&&
        ticks>=g.motionStart+1000;
    if(!g.furniture.Requested()&&g.furnitureRandom.Advance(seconds,
            available&&!g.pet.roamingPaused&&!g.companionModel.Busy(),g.furnitureRandomSource(),CanStartAutonomousStory(ticks)))
        g.furniture.Request(static_cast<cat::furniture::Kind>(1+g.furnitureRandomSource()%3));
    if(!g.furniture.Requested()){ReleaseFurnitureMovie(false);return;}
    const bool ready=PrepareFurnitureMovie(false,g.furniture.Action(),static_cast<unsigned>(g.preferences.character));
    if(FurnitureMovieFailed(false)) {
        const bool owned=g.furniture.Owned();g.furniture.Cancel();ReleaseFurnitureMovie(false);
        g.furnitureRandom.Completed(g.furnitureRandomSource());if(owned)BeginAutonomousRest(ticks);
        return;
    }
    if(seconds<=0)return;
    if(g.furniture.State()==Phase::Queued&&available) {
        const auto bounds=MovementBoundsLogical();
        if(g.furniture.Begin(body.x,bounds.first,bounds.second,cat::GrowthScale(g.pet.bornUtc,UtcNow())*std::max(1.,cat::appearance::VisualModifiers(g.nutrition,g.preferences.character).bodyWidth),
                static_cast<unsigned>(g.preferences.character))) {
            if(g.brain)g.brain->Interrupt(ticks);
            const auto velocity=g.furniture.Velocity(body.x,RegisteredWalkSpeed());
            g.direction=velocity<0?-1:1;
            SetMotion(Motion::Walk,120000,ticks);g.speed=RegisteredWalkSpeed()*g.dpiScale;
        }
    }
    if(!g.furniture.Owned())return;
    const auto previous=g.furniture.State();
    const auto velocity=g.furniture.Velocity(body.x,RegisteredWalkSpeed());
    g.direction=previous==Phase::Approach?(velocity<0?-1:1):1;
    const bool side=g.facing==1&&g.groundArtBridge.Side()&&!g.runningTurn.Active()&&
        !g.waitingForStop&&std::abs(WalkingVelocity(visible))<=.5;
    const bool completed=g.furniture.Advance(seconds,body.x,WalkingVelocity(body),side,ready);
    if(g.furniture.State()==Phase::Play&&previous!=Phase::Play) {
        SetMotion(Motion::Watch,120000,ticks);g.speed=0;
    }
    if(completed) {
        g.furnitureRandom.Completed(g.furnitureRandomSource());ReleaseFurnitureMovie(false);
        BeginAutonomousRest(ticks);
    }
}
