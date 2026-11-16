// Included in Model's private section. Uses the same Session as the primary.
std::uint64_t furnitureRng_=0xe2bf15c89037a641ULL;
unsigned FurnitureRoll() {
    furnitureRng_^=furnitureRng_>>12;furnitureRng_^=furnitureRng_<<25;furnitureRng_^=furnitureRng_>>27;
    return static_cast<unsigned>((furnitureRng_*2685821657736338717ULL)>>32);
}
void UpdateFurniture(const Context& c) {
    using P=cat::furniture::Phase;using M=cat::animation::Motion;
    const auto physical=physics_.CurrentPose(),visible=physics_.RenderPose();
    // An accepted furniture command survives sleep. Finish falling asleep,
    // then use the registered wake outro instead of snapping to a walk.
    if(furniture.State()==P::Queued&&motion_==M::Sleep&&physical.grounded&&
            !physical.onPlatform&&!waitingForStop_&&!jumpPending_&&!keyboardActive_&&
            !ballPlaying_&&!cursor_.Active()&&!wallApproaching_&&!physics_.Hanging()) {
        const auto* sleep=Registered(cat::sprite_sequence::Clip::Sleep);
        const auto intro=sleep?cat::animation::ReferenceSleepIntroMs(&sleep->program,sleep->frameCount):2000;
        if(c.ticks>=motionStart_&&c.ticks-motionStart_>=intro) {
            ResetControls();brain_->Interrupt(c.ticks);interaction_=Interaction::None;
            storyActive_=true;quietActive_=false;
            SetMotion(M::Stretch,c.ticks,3500,0);
        }
    }
    const bool quiet=(motion_==M::Idle||motion_==M::Watch)&&quietActive_&&
        c.ticks>=motionStart_+1000;
    const bool available=!Busy()&&quiet&&!keyboardActive_&&!physics_.Hanging()&&
        !physical.onPlatform&&c.ticks>=careUntil_&&std::abs(physical.velocityX)<=.5;
    const auto roll=FurnitureRoll();
    if(!furniture.Requested()&&furnitureRandom.Advance(c.elapsedSeconds,
            c.supportsFurniture&&available&&!c.primaryBusy&&!state_->pet.roamingPaused,roll,c.ticks>=nextDecision_))
        furniture.Request(static_cast<cat::furniture::Kind>(1+roll%3));
    if(c.furnitureFailed) {
        const bool owned=furniture.Owned();furniture.Cancel();explicitTravel_=false;
        furnitureRandom.Completed(roll);if(owned)BeginQuiet(c.ticks);return;
    }
    if(furniture.State()==P::Queued&&available) {
        if(furniture.Begin(physical.x,minX_,maxX_,cat::GrowthScale(state_->pet.bornUtc,state_->pet.updatedUtc)*std::max(1.,cat::appearance::VisualModifiers(state_->nutrition,state_->preferences.character).bodyWidth),
                static_cast<unsigned>(state_->preferences.character),c.primaryFurnitureSpace,c.primaryFurnitureFallback)) {
            brain_->Interrupt(c.ticks);storyActive_=true;quietActive_=false;explicitTravel_=true;
            const auto* walk=Registered(cat::sprite_sequence::Clip::Walk);
            const double speed=walk?cat::sprite_assets::NominalLocomotionSpeed(*walk):40;
            direction_=furniture.Velocity(physical.x,speed)<0?-1:1;
            SetMotion(M::Walk,c.ticks,120000,speed);
        }
    }
    if(!furniture.Owned())return;
    const auto previous=furniture.State();
    direction_=previous==P::Approach&&furniture.Velocity(physical.x,speed_)<0?-1:1;
    const bool side=facing_==1&&groundArtBridge_.Side()&&!runningTurn_.Active()&&!waitingForStop_&&
        std::abs(visible.velocityX-visible.supportVelocityX)<=.5;
    const bool done=furniture.Advance(c.elapsedSeconds,physical.x,physical.velocityX-physical.supportVelocityX,
        side,c.furnitureReady);
    if(furniture.Playing()&&previous!=P::Play)SetMotion(M::Watch,c.ticks,120000,0);
    if(done){explicitTravel_=false;furnitureRandom.Completed(roll);BeginQuiet(c.ticks);}
}
