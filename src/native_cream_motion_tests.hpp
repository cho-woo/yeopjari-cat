#pragma once

// Actual GDI+ dispatch using the production rig and Box2D, not the old 8-cut
// walking preview. Only called by --render-self-test; no saved data is loaded.
int NativeCreamMotionRenderTest(std::string& context) {
    context="Cream continuous rig resources";
    if(!g.creamImage || cat::kAvailableCharacters.size()!=1)return 440;
    for(int id:{103,104,109,111})if(FindResourceW(g.instance,MAKEINTRESOURCEW(id),RT_RCDATA))return 440;
    Surface canvas,from,to,review;
    if(!canvas.Create(420,340)||!from.Create(420,340)||!to.Create(420,340)||!review.Create(1800,1560))return 441;
    {Graphics graphics(review.image.get());graphics.Clear(Color(255,237,240,244));}
    const auto prefs=cat::SerializePreferences(g.preferences),nutrition=cat::nutrition::Serialize(g.nutrition);
    const auto body=cat::nutrition::VisualModifiers(g.nutrition);
    unsigned row=0;
    for(const REAL desiredHeight:{80.f,110.f,165.f})for(int facing:{1,-1}) {
        cat::cream::Motion controller;
        cat::physics::Controller physical;physical.Reset(600,0,1800);
        unsigned captured=0;std::uint64_t lastHash=0;unsigned changes=0;
        const auto* buffer=g.creamRig.Pixels();
        for(unsigned tick=0;tick<440;++tick) {
            const bool walk=tick>=60&&tick<270;const double dt=1./60;
            const auto before=physical.CurrentPose();
            const bool ready=controller.Prepare(dt,walk||std::abs(before.velocityX)>2,true,before.grounded);
            physical.SetTargetVelocity(ready&&walk?facing*22:0);
            const auto previous=physical.RenderPose();physical.Update(dt);const auto current=physical.RenderPose();
            controller.Travel(current.relativeTravelX-previous.relativeTravelX,dt,current.grounded);
            const auto action=walk?Motion::Walk:Motion::Idle;
            auto pose=cat::animation::Evaluate(action,tick*1000/60,tick*1000/60,facing);
            if(!cat::animation::ApplyCreamLocomotion(pose,action,controller)||!pose.creamRig)return 442;
            context="Cream continuous GDI height="+std::to_string(desiredHeight)+" facing="+std::to_string(facing)+" tick="+std::to_string(tick);
            {
                Graphics graphics(canvas.image.get());graphics.Clear(Color(0,0,0,0));
                graphics.SetInterpolationMode(InterpolationModeHighQualityBicubic);
                graphics.SetPixelOffsetMode(PixelOffsetModeHighQuality);
                DrawStyledAnimationDirect(graphics,pose,210,280,
                    desiredHeight/CurrentArt(cat::Character::Cream).base.standingHeight,
                    cat::Character::Cream,cat::Outfit::None,body,from,to,420,340);
                graphics.Flush(FlushIntentionSync);if(graphics.GetLastStatus()!=Ok)return 443;
            }
            if(g.creamRig.Pixels()!=buffer)return 444;
            std::uint64_t hash=1469598103934665603ULL;unsigned visible=0;
            for(int y=0;y<340;++y)for(int x=0;x<420;++x) {
                const auto* p=canvas.pixels+(y*420+x)*4;
                for(unsigned c=0;c<3;++c)if(p[c]>p[3]+1)return 444;
                if(p[3]>128){++visible;if(x==0||x==419||y==0||y>282)return 444;}
                for(unsigned c=0;c<4;++c){hash^=p[c];hash*=1099511628211ULL;}
            }
            if(visible<350)return 444;
            if(tick>=130&&tick<250&&hash!=lastHash)++changes;
            lastHash=hash;
            if(tick==0||tick==82||tick==130||tick==208||tick==300||tick==435){
                Graphics graphics(review.image.get());
                Pen ground(Color(255,145,154,165),1);
                const int left=captured*300,top=row*260;
                graphics.DrawLine(&ground,left,top+230,left+300,top+230);
                graphics.DrawImage(canvas.image.get(),Rect(left,top,300,260),60,50,300,260,UnitPixel);
                ++captured;
            }
        }
        if(changes<100||captured!=6||controller.Sit()!=1)return 445;
        ++row;
    }
    if(!SaveNativeWalkingReview(*review.image,L"render-self-test/cream-walk-stand-sit-gdi.png"))return 446;
    if(prefs!=cat::SerializePreferences(g.preferences)||nutrition!=cat::nutrition::Serialize(g.nutrition))return 446;
    return 0;
}
