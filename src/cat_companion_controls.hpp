// Included inside Model. Both animals use CursorSession and the same immutable
// Dropshot programs; only body, clocks, contacts and memory belong to this cat.
cat::environment::CursorSession cursor_;
cat::environment::CursorAction cursorLastAttack_=cat::environment::CursorAction::None;
cat::animation::Motion cursorMotion_=cat::animation::Motion::Watch;
cat::cursor::PawContact cursorContact_;
cat::sprite_sequence::Clip actionClip_=cat::sprite_sequence::Clip::None;
cat::sprite_sequence::Clip wakeQueuedClip_=cat::sprite_sequence::Clip::None;
std::uint64_t cursorLaunchedAt_=0,cursorLanded_=0,nextFly_=0;
double cursorLaunchVy_=0,cursorAirPawHeight_=55;
double cursorTakeoffVelocity_=0;
bool cursorLaunched_=false,wallApproaching_=false;
bool keyboardActive_=false,keyboardRun_=false,keyboardJump_=false,explicitTravel_=false;
int keyboardDirection_=0;
cat::gait_transition::Transition keyboardGait_;
unsigned cursorContacts_=0;
cat::toy::PhysicsBall ball_;
bool ballContact_=false;

void ResetControls() {
    wakeQueuedClip_=cat::sprite_sequence::Clip::None;
    cursor_={};cursorContact_.Reset();cursorLastAttack_=cat::environment::CursorAction::None;
    actionClip_=cat::sprite_sequence::Clip::None;cursorLaunchedAt_=cursorLanded_=nextFly_=0;
    cursorLaunched_=wallApproaching_=false;cursorContacts_=0;
    keyboardActive_=keyboardJump_=explicitTravel_=false;keyboardDirection_=0;keyboardGait_.Reset();
}
void PauseControls(std::uint64_t elapsed) {
    cursor_.Pause(elapsed,{motion_==cursorMotion_,waitingForStop_,motionStart_,motionSerial_});
    if(cursorLaunchedAt_)cursorLaunchedAt_+=elapsed;
    if(cursorLanded_)cursorLanded_+=elapsed;
    if(nextFly_)nextFly_+=elapsed;
    keyboardGait_.Pause(elapsed);
}
std::uint64_t CursorPreparationMs() const {
    const auto* art=Registered(cat::sprite_sequence::Clip::CursorPounce);
    return art?cat::sprite_sequence::Duration(art->program,art->program.intro):240;
}
bool LaunchCursor(const Context& c) {
    const auto body=cat::appearance::VisualModifiers(state_->nutrition,state_->preferences.character);
    const auto species=cat::species::ProfileFor(state_->preferences.character);
    const double growth=cat::GrowthScale(state_->pet.bornUtc,state_->pet.updatedUtc);
    const auto physical=physics_.CurrentPose();
    const bool travelling=std::abs(physical.velocityX-physical.supportVelocityX)>12 &&
        (physical.velocityX-physical.supportVelocityX)*direction_>0 && !state_->pet.roamingPaused;
    const bool launched=travelling?
        cat::cursor::LaunchRunningReachHop(physics_,cursor_.TargetHeight(),minX_,maxX_,growth,
            body.jump*species.jumpHeightMultiplier,cursorAirPawHeight_):
        cat::cursor::LaunchReachHop(physics_,cursor_.TargetX(),cursor_.TargetHeight(),minX_,maxX_,growth,
            body.jump*species.jumpHeightMultiplier,!state_->pet.roamingPaused,cursorAirPawHeight_);
    cursor_.ConfirmLaunch(launched);
    if(launched) {cursorLaunched_=true;cursorLaunchedAt_=c.ticks;cursorLanded_=0;cursorLaunchVy_=physics_.CurrentPose().velocityY;}
    return launched;
}

public:
bool BeginKeyboard(std::uint64_t ticks) {
    if(!Bound()||!ready_||Busy()||!physics_.CurrentPose().grounded||physics_.CurrentPose().onPlatform)return false;
    ResetControls();interaction_=Interaction::None;brain_->Interrupt(ticks);
    keyboardActive_=true;SetMotion(cat::animation::Motion::Idle,ticks,3600000,0);return true;
}
void KeyboardDrive(int direction,bool run,bool jump) {
    keyboardDirection_=std::clamp(direction,-1,1);keyboardRun_=run;keyboardJump_=keyboardJump_||jump;
}
void EndKeyboard(std::uint64_t ticks) {
    if(!keyboardActive_)return;
    keyboardActive_=false;keyboardGait_.Reset();keyboardDirection_=0;keyboardJump_=false;
    if(physics_.CurrentPose().grounded&&!jumpPending_)BeginQuiet(ticks);
}
// Explicit actions are available from this animal's own menu. They never write
// the primary's movement, feeding history or memory.
bool Perform(cat::sprite_sequence::Clip clip,std::uint64_t ticks) {
    using C=cat::sprite_sequence::Clip;using M=cat::animation::Motion;
    if(!Bound()||!ready_||!state_->enabled||!physics_.CurrentPose().grounded||Feeding(ticks)||
            runningTurn_.Active()||physics_.WallClimbing()||!Registered(clip))return false;
    if(wakeQueuedClip_!=C::None&&motion_==M::Stretch&&wakingFromSleep_) {wakeQueuedClip_=clip;return true;}
    const bool wakeFirst=motion_==M::Sleep&&clip!=C::Sleep;
    cursor_={};cursorContact_.Reset();cursorLaunched_=false;cursorLanded_=0;
    keyboardActive_=false;keyboardGait_.Reset();explicitTravel_=false;
    interaction_=Interaction::None;brain_->Interrupt(ticks);storyActive_=true;quietActive_=false;
    actionClip_=C::None;wallApproaching_=false;
    if(wakeFirst) {
        SetMotion(M::Stretch,ticks,3500,0);wakeQueuedClip_=clip;return true;
    }
    if(clip==C::Walk||clip==C::Run||clip==C::Jump||clip==C::Climb) {
        direction_=physics_.CurrentPose().x<(minX_+maxX_)*.5?1:-1;
        const auto* art=Registered(clip==C::Run?C::Run:C::Walk);
        const double speed=art?cat::sprite_assets::NominalLocomotionSpeed(*art):40;
        SetMotion(clip==C::Jump?M::Jump:clip==C::Run?M::Zoomies:M::Walk,ticks,clip==C::Climb?120000:6000,speed);
        explicitTravel_=true;
        if(clip==C::Climb){direction_=1;wallApproaching_=true;}
    } else {
        const auto duration=RegisteredDuration(clip,4500);
        SetMotion(clip==C::Groom?M::Groom:clip==C::Sleep?M::Sleep:
            clip==C::Knead?M::Knead:clip==C::Punch?M::Punch:M::ToyPaw,ticks,duration,0);
        // Repeating an explicit gesture is still a new complete movie.
        motionStart_=ticks;++motionSerial_;nextDecision_=ticks+duration;
        actionClip_=clip;
        if(clip==C::BallPlay) {
            const auto body=cat::appearance::VisualModifiers(state_->nutrition,state_->preferences.character);
            const double growth=cat::GrowthScale(state_->pet.bornUtc,state_->pet.updatedUtc);
            const auto paw=cat::video_actions::BallPaws(static_cast<unsigned>(state_->preferences.character),
                state_->preferences.character==cat::Character::Tuxedo?66:48)[1];
            ball_.Reset(physics_.CurrentPose().x+(facing_<0?-1:1)*(paw.x*body.bodyWidth*growth+8));
            ballContact_=false;
        }
    }
    return true;
}
cat::environment::CursorAction CursorAction() const {return cursor_.Action();}
unsigned CursorContacts() const {return cursorContacts_;}
private:
void UpdateControls(const Context& c,std::uint64_t elapsed) {
    using C=cat::sprite_sequence::Clip;using A=cat::environment::CursorAction;using M=cat::animation::Motion;
    const auto physical=physics_.CurrentPose();
    if(wakeQueuedClip_!=C::None&&c.ticks>=nextDecision_&&physical.grounded&&!waitingForStop_) {
        const auto queued=wakeQueuedClip_;wakeQueuedClip_=C::None;Perform(queued,c.ticks);return;
    }
    if(actionClip_==C::BallPlay&&!waitingForStop_) {
        ball_.Advance(c.elapsedSeconds,std::max(minX_,physical.x-95),std::min(maxX_,physical.x+95));
        const auto* art=Registered(C::BallPlay);const auto age=c.ticks>=motionStart_?c.ticks-motionStart_:0;
        cat::sprite_sequence::Options options;options.sheetFrames=art->frameCount;
        const auto frame=cat::sprite_sequence::SampleProgram(C::BallPlay,art->program,age,options).frame;
        const auto paw=cat::video_actions::BallPaws(static_cast<unsigned>(state_->preferences.character),frame)[1];
        const double growth=cat::GrowthScale(state_->pet.bornUtc,state_->pet.updatedUtc);
        const auto body=cat::appearance::VisualModifiers(state_->nutrition,state_->preferences.character);
        const bool tuxedo=state_->preferences.character==cat::Character::Tuxedo;
        if(cat::toy::BallPawContactAt(age,ballContact_,physical.x+(facing_<0?-1:1)*paw.x*growth*body.bodyWidth,
                paw.y*growth*body.bodyHeight,ball_.CurrentPose(),growth,true,(tuxedo?54:36)*1000/24,(tuxedo?102:84)*1000/24))
            ball_.Kick(facing_,58,26);
    }
    if(keyboardActive_) {
        if(keyboardGait_.Finish(c.ticks))gait_.SetAuthoredPhase(cat::video_actions::GaitArrivalPhase(
            static_cast<unsigned>(state_->preferences.character),keyboardGait_.ToRun()));
        if(physical.grounded&&!jumpPending_&&!StationaryLanding(c.ticks,physical)) {
            if(keyboardDirection_)direction_=keyboardDirection_;
            const auto* gait=Registered(keyboardRun_?C::Run:C::Walk);
            const double speed=gait?cat::sprite_assets::NominalLocomotionSpeed(*gait):40;
            if(!runningTurn_.Active()&&!keyboardGait_.Active()) {
                if(keyboardJump_) {SetMotion(M::Jump,c.ticks,5000,0);keyboardJump_=false;}
                else {
                    const auto desired=keyboardDirection_?(keyboardRun_?M::Zoomies:M::Walk):M::Idle;
                    if(motion_!=desired) {
                        if(cat::animation::Walking(motion_)&&cat::animation::Walking(desired)) {
                            if(direction_==facing_) {
                                if(keyboardRun_!=(gait_.AuthoredMode()==cat::authored_locomotion::Mode::Run)&&groundArtBridge_.Side())
                                    if(const auto* run=Registered(C::Run))keyboardGait_.Begin(keyboardRun_,c.ticks,
                                        cat::sprite_sequence::Duration(run->program,keyboardRun_?run->program.intro:run->program.outro),
                                        std::abs(physical.velocityX-physical.supportVelocityX),speed);
                                motion_=walkingMotion_=desired;
                            }
                        } else SetMotion(desired,c.ticks,3600000,speed);
                    }
                }
            }
            speed_=keyboardDirection_&&direction_==facing_?(keyboardGait_.Active()?keyboardGait_.Speed(c.ticks):speed):0;
        }
        return;
    }
    const auto* jumpArt=Registered(C::Jump);
    const bool explicitJumpRecovered=motion_==M::Jump&&!jumpPending_&&landedAt_&&c.ticks>=landedAt_&&jumpArt&&
        c.ticks-landedAt_>=cat::sprite_sequence::Duration(jumpArt->program,jumpArt->program.outro);
    if(explicitTravel_&&(c.ticks>=nextDecision_||explicitJumpRecovered)&&physical.grounded&&!runningTurn_.Active()&&!wallApproaching_) {
        explicitTravel_=false;BeginQuiet(c.ticks);
    }
    if(wallApproaching_&&physical.grounded&&physical.x>=maxX_-1.9&&
            std::abs(physical.velocityX-physical.supportVelocityX)<3) {
        wallApproaching_=false;SetMotion(M::Hang,c.ticks,RegisteredDuration(C::Climb,6000),0);
        actionClip_=C::Climb;waitingForStop_=false;
        if(!physics_.BeginWallClimb(std::min(150.,maxLift_*.6)))actionClip_=C::None;
    }
    if(wallApproaching_&&physical.x>maxX_-45)speed_=std::min(speed_,std::max(6.,(maxX_-physical.x)*2));
    if(actionClip_==C::Climb&&physics_.WallClimbing()) {
        const auto age=c.ticks>=motionStart_?c.ticks-motionStart_:0;
        physics_.SetWallClimbSpeed(cat::lively::ClimbSpeed(age)*cat::GrowthScale(state_->pet.bornUtc,state_->pet.updatedUtc));
        if(age>=5000)physics_.EndWallClimb();
    }
    if(actionClip_!=C::None&&!waitingForStop_&&c.ticks>=nextDecision_) {
        const bool waking=actionClip_==C::Sleep;
        if(actionClip_==C::Climb)physics_.EndWallClimb();
        actionClip_=C::None;
        if(waking&&physics_.CurrentPose().grounded)SetMotion(M::Stretch,c.ticks,3200,0);
        else if(physics_.CurrentPose().grounded)BeginQuiet(c.ticks);
        else {SetMotion(M::Jump,c.ticks,3000,0,true);landedAt_=0;}
    }
    if(!nextFly_)nextFly_=c.ticks+90000;
    if(!Busy()&&!state_->pet.roamingPaused&&quietActive_&&c.ticks>=nextFly_) {
        nextFly_=c.ticks+120000+static_cast<std::uint64_t>(Random()*180000);
        Perform(C::FlyHunt,c.ticks);
    }
    cat::environment::CursorInput input;
    input.nowMs=c.ticks;input.enabled=c.cursorEnabled;input.available=c.cursorAvailable;
    input.cursorX=c.cursorX;input.cursorHeight=c.cursorWorldHeight-physical.lift;
    input.supportHeight=physical.lift;input.catX=physical.x;input.catVelocityX=physical.velocityX-physical.supportVelocityX;
    input.catFacing=facing_;input.minX=minX_;input.maxX=maxX_;
    input.sizeScale=cat::GrowthScale(state_->pet.bornUtc,state_->pet.updatedUtc);
    const auto body=cat::appearance::VisualModifiers(state_->nutrition,state_->preferences.character);
    input.movementScale=body.speed*cat::species::ProfileFor(state_->preferences.character).movementSpeedMultiplier;
    input.repeatNearby=input.pursuePointer=input.pawEndsPlanted=true;
    input.pawExitMs=cat::video_actions::PunchExitMs(static_cast<unsigned>(state_->preferences.character));
    input.canApproach=!state_->pet.roamingPaused;input.canAttack=!runningTurn_.Active();
    input.grounded=physical.grounded;input.interest=.8;input.roll=.5;
    input.pawDurationMs=input.punchDurationMs=RegisteredDuration(C::Punch,4458);
    if(const auto* swat=Registered(C::Punch)) {
        double best=1e9;
        for(unsigned frame=0;frame<swat->frameCount;++frame) {
            if(!cat::video_actions::PunchContact(static_cast<unsigned>(state_->preferences.character),frame))continue;
            const auto paw=cat::video_actions::PunchPaws(static_cast<unsigned>(state_->preferences.character),frame)[0];
            if(paw.x<16)continue;
            const double error=std::abs(paw.y*body.bodyHeight-input.cursorHeight/input.sizeScale);
            if(error<best){best=error;input.pawStandOff=std::clamp(paw.x*body.bodyWidth,12.,80.);}
        }
    }
    input.tiptoeDurationMs=input.bothPawsDurationMs=RegisteredDuration(C::Reach,4500);
    input.pouncePreparationMs=CursorPreparationMs();
    input.settleDurationMs=cat::video_actions::ReachSettleDuration(static_cast<unsigned>(state_->preferences.character));
    if(const auto* jump=Registered(C::CursorPounce)) {
        input.landingRecoveryMs=cat::sprite_sequence::Duration(jump->program,jump->program.outro);
        const auto paw=cat::video_actions::CursorPouncePaws(static_cast<unsigned>(state_->preferences.character),
            jump->program.loop.first+jump->program.loop.count/2)[0];
        input.airPawX=paw.x*body.bodyWidth;input.airPawHeight=paw.y*body.bodyHeight;
    }
    cursorAirPawHeight_=input.airPawHeight;
    input.canContinue=actionClip_==C::None&&!wallApproaching_&&interaction_==Interaction::None&&
        (physical.grounded||cursor_.InFlight())&&motion_!=M::Eat&&motion_!=M::Treat&&motion_!=M::Groom&&
        motion_!=M::Sleep&&!wakingFromSleep_&&!AuthoredCareHolding(c.ticks);
    input.canStart=input.canContinue&&!waitingForStop_&&!randomZoomies_.Active()&&
        (motion_==M::Idle||motion_==M::Watch||motion_==M::Walk||motion_==M::Stalk);
    const auto previous=cursor_.Action();
    const auto output=cursor_.Step(input,elapsed,{motion_==cursorMotion_,waitingForStop_,motionStart_,motionSerial_});
    if(output.changed) {
        const auto m=output.action==A::Chase?M::Zoomies:output.action==A::Pounce?M::Pounce:
            output.action==A::Hunt||output.action==A::Sneak?M::Stalk:
            output.action==A::Paw||output.action==A::Punch||output.action==A::Tiptoe||output.action==A::BothPaws?M::ToyPaw:M::Watch;
        brain_->Interrupt(c.ticks);cursorMotion_=m;
        SetMotion(m,c.ticks,8000,0);
        // The two swat strengths share a motion name but own distinct clocks.
        motionStart_=c.ticks;++motionSerial_;nextDecision_=c.ticks+8000;
        if(output.action==A::Paw||output.action==A::Punch||output.action==A::Tiptoe||output.action==A::BothPaws||output.action==A::Pounce)
            cursorLastAttack_=output.action;
        if(output.action==A::Rest||output.action==A::Chase) {
            waitingForStop_=false;
            if(output.action==A::Chase&&(previous==A::Rest||previous==A::Paw||previous==A::Punch||previous==A::Pounce))
                groundArtBridge_.ContinueLocomotion(c.ticks);
        }
        if(output.action==A::Pounce) {cursorLaunched_=false;cursorLanded_=0;}
        if(output.action==A::Pounce&&previous==A::Chase&&std::abs(physical.velocityX-physical.supportVelocityX)>12&&
                (physical.velocityX-physical.supportVelocityX)*output.direction>0&&!state_->pet.roamingPaused) {
            cursorTakeoffVelocity_=physical.velocityX-physical.supportVelocityX;
            waitingForStop_=false;groundArtBridge_.ContinueLocomotion(c.ticks);
        } else if(output.action!=A::Pounce)cursorTakeoffVelocity_=0;
        cursor_.AdoptMotion(motionStart_,waitingForStop_,motionSerial_);cursorContact_.Reset();
    }
    if(cursor_.Active())direction_=output.direction;
}
void ApplyControlsPose(cat::animation::Pose& pose,std::uint64_t ticks,const cat::physics::Pose& physical) const {
    using C=cat::sprite_sequence::Clip;using A=cat::environment::CursorAction;
    const auto age=ticks>=motionStart_?ticks-motionStart_:0;
    if(keyboardActive_&&keyboardGait_.Active()&&physical.grounded&&!runningTurn_.Active()) {
        cat::sprite_sequence::Options options;
        options.stage=keyboardGait_.ToRun()?cat::sprite_sequence::Stage::Entering:cat::sprite_sequence::Stage::Exiting;
        cat::animation::AttachSprite(pose,C::Run,keyboardGait_.Elapsed(ticks),options);pose.flip=facing_<0;return;
    }
    if(actionClip_!=C::None&&!waitingForStop_) {
        cat::animation::AttachSprite(pose,actionClip_,age);pose.flip=actionClip_==C::Climb?false:facing_<0;
        pose.groundContact=physical.grounded;cat::animation::UseAuthoredSprite(pose);return;
    }
    if(interaction_==Interaction::Paw&&motion_==cat::animation::Motion::Punch&&!waitingForStop_) {
        cat::animation::AttachSprite(pose,C::Punch,age);pose.flip=facing_<0;
    }
    if(!cursor_.OwnsSerial(motionSerial_)||motion_!=cursorMotion_||waitingForStop_)return;
    const auto action=cursor_.Action();
    C clip=C::None;std::uint64_t sourceAge=age;
    cat::sprite_sequence::Options options;
    const auto* jump=Registered(C::CursorPounce);
    const auto recovery=jump?cat::sprite_sequence::Duration(jump->program,jump->program.outro):1240;
    if(action==A::Pounce||(action==A::Rest&&cursorLastAttack_==A::Pounce&&cursorLaunched_&&cursorLanded_&&
            ticks-cursorLanded_<recovery)) {
        clip=C::CursorPounce;options.grounded=physical.grounded;options.velocityY=physical.velocityY;
        options.jump=cursorLaunched_?cat::sprite_sequence::JumpPhase::Landing:cat::sprite_sequence::JumpPhase::Crouch;
        options.contactElapsedMs=cursorLanded_&&ticks>=cursorLanded_?ticks-cursorLanded_:0;
        if(cursorLaunched_&&cursorLaunchVy_>0)options.flightProgress=std::clamp((cursorLaunchVy_-physical.velocityY)/(2*cursorLaunchVy_),0.,1.);
    } else if(action==A::Paw||action==A::Punch)clip=C::Punch;
    else if(action==A::Tiptoe||action==A::BothPaws)clip=C::Reach;
    else if(action==A::Rest) {
        clip=cursorLastAttack_==A::Paw||cursorLastAttack_==A::Punch?C::Punch:C::Reach;
        sourceAge=RegisteredDuration(clip,4500);
        if(cursorLastAttack_==A::Pounce&&cursorLanded_&&ticks>=cursorLanded_+recovery)
            sourceAge=cat::video_actions::ReachSettleStart(static_cast<unsigned>(state_->preferences.character))+ticks-cursorLanded_-recovery;
    }
    if(clip==C::None)return;
    cat::animation::AttachSprite(pose,clip,sourceAge,options);pose.flip=direction_<0;
    if(const auto* art=Registered(clip)) {
        options=pose.spriteOptions;options.sheetFrames=art->frameCount;
        const auto frame=cat::sprite_sequence::SampleProgram(clip,art->program,sourceAge,options).frame;
        pose.spritePaws=clip==C::Punch?cat::video_actions::PunchPaws(static_cast<unsigned>(state_->preferences.character),frame):
            clip==C::Reach?cat::video_actions::ReachPaws(static_cast<unsigned>(state_->preferences.character),frame):
            cat::video_actions::CursorPouncePaws(static_cast<unsigned>(state_->preferences.character),frame);
        pose.spritePawsValid=true;
    }
    cat::animation::UseAuthoredSprite(pose);
}
void UpdateCursorContact(const Context& c) {
    using C=cat::sprite_sequence::Clip;
    if(!cursor_.Active()||!cursor_.ContactAllowed()||waitingForStop_||!c.cursorAvailable)return;
    const auto physical=physics_.RenderPose();cat::animation::Pose pose;
    ApplyControlsPose(pose,c.ticks,physical);
    const auto* art=Registered(pose.spriteClip);if(!art||!pose.spritePawsValid)return;
    auto options=pose.spriteOptions;options.sheetFrames=art->frameCount;
    const auto frame=cat::sprite_sequence::SampleProgram(pose.spriteClip,art->program,pose.spriteElapsedMs,options).frame;
    const unsigned index=pose.spriteClip==C::Punch?cat::video_actions::PunchContact(static_cast<unsigned>(state_->preferences.character),frame):
        pose.spriteClip==C::Reach?cat::video_actions::ReachContact(static_cast<unsigned>(state_->preferences.character),frame):
        cursor_.InFlight()&&(state_->preferences.character==cat::Character::Tuxedo?frame>=19&&frame<=31:frame>=18&&frame<=34)?1:0;
    const auto body=cat::appearance::VisualModifiers(state_->nutrition,state_->preferences.character);
    const double growth=cat::GrowthScale(state_->pet.bornUtc,state_->pet.updatedUtc);
    bool contact=false;
    for(unsigned paw=0;paw<(cursor_.InFlight()?2u:1u);++paw) {
        const auto point=pose.spritePaws[paw];
        contact=cursorContact_.Observe(index,true,physical.x+(pose.flip?-1:1)*point.x*growth*body.bodyWidth,
            point.y*growth*body.bodyHeight,c.cursorX,c.cursorWorldHeight-physical.lift,7*growth)||contact;
    }
    if(contact) {++cursorContacts_;cat::memory::Record(state_->memory,cat::memory::Event::Play,state_->pet.updatedUtc);}
}
