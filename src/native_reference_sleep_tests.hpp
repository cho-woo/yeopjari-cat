#pragma once

// Actual primary/companion pose selection versus the original, undeformed
// base-atlas pixels. These are offscreen GDI+ captures, not desktop recordings.
int NativeReferenceSleepTest(std::string& context) {
    using Clip=cat::sprite_sequence::Clip;
    using Motion=cat::animation::Motion;
    if(!CreateDirectoryW(L"render-self-test/sleep-reference",nullptr)&&GetLastError()!=ERROR_ALREADY_EXISTS)return 650;
    constexpr int width=260,height=240;
    constexpr REAL center=130,floor=220,desiredHeight=110;
    Surface canvas,review,neutralReview;
    if(!canvas.Create(width,height)||!review.Create(width*7,height*2)||!neutralReview.Create(width*3,height*2))return 650;
    {Graphics graphics(review.image.get());graphics.Clear(Color(255,241,242,243));}
    {Graphics graphics(neutralReview.image.get());graphics.Clear(Color(255,241,242,243));}
    std::ostringstream report;report.imbue(std::locale::classic());
    report<<"{\"schema\":1,\"status\":\"passed\",\"test\":\"reference-rest-poses\",\"version\":\""
        YEOPJARI_VERSION_STRING "\",\"render_source\":\"Windows GDI+ / production pose selection\",\"desktop_recording\":false,"
        "\"source_asset\":\"assets/cat.png\",\"base_resource_id\":101,\"source_frame\":6,\"neutral_source_frame\":4,\"new_motion_drawings\":0,\"characters\":[";
    unsigned row=0;
    for(const auto character:cat::kAvailableCharacters) {
        context="native exact curled sleep character="+std::to_string(static_cast<unsigned>(character));
        g.preferences={character,cat::Outfit::None};g.waitingForStop=false;g.jumpPending=g.jumpLaunched=false;
        g.facing=g.direction=1;g.landedAt=0;g.perch={};g.workInterference=false;cursorInteraction={};
        g.toyEngaged=g.approachingToy=g.approachingUser=false;g.pendingCare=-1;
        const auto utc=UtcNow();g.pet=cat::NewPet(utc-366*cat::life::Day);g.pet.updatedUtc=utc;
        g.nutrition=cat::nutrition::NewState(g.pet.bornUtc,utc,80);g.nutrition.bodyCondition=50;
        g.physics.Reset(400,96,1184);g.physicsReady=true;
        for(unsigned i=0;i<12;++i)g.physics.Update(1./60);
        constexpr std::uint64_t started=100000;
        SetMotion(Motion::Sleep,120000);g.motionStart=started;g.nextDecision=started+120000;
        g.groundArtBridge.Reset(started);g.poseTransition.Reset();
        const auto* sleep=LoadedCareSprite(Clip::Sleep,character);
        if(!sleep)return 650;
        const auto intro=cat::animation::ReferenceSleepIntroMs(&sleep->layout->program,sleep->layout->frameCount);
        const std::array<std::uint64_t,3> elapsed{{intro,intro+8000,intro+20000}};
        const auto body=cat::appearance::VisualModifiers(g.nutrition,character);
        const auto clear=[&](Graphics& graphics) {
            graphics.SetCompositingMode(CompositingModeSourceCopy);graphics.Clear(Color(0,0,0,0));
            graphics.SetCompositingMode(CompositingModeSourceOver);
            graphics.SetInterpolationMode(InterpolationModeHighQualityBicubic);
            graphics.SetSmoothingMode(SmoothingModeAntiAlias);graphics.SetPixelOffsetMode(PixelOffsetModeHighQuality);
        };
        // Source pixels were decoded from resource101 (or the matching Tux
        // styled101 texture), before any joint deformation/render selection.
        const auto paintReference=[&](unsigned frame) {
            const auto& art=CurrentArt(character);auto& skin=CurrentArt(character).base.skins[frame];
            Bitmap raw(skin.width,skin.height,skin.width*4,PixelFormat32bppPARGB,reinterpret_cast<BYTE*>(skin.source.data()));
            Graphics graphics(canvas.image.get());clear(graphics);
            const REAL scale=desiredHeight/std::max(1,art.base.standingHeight);
            const REAL w=static_cast<REAL>(skin.width*scale*body.bodyWidth),h=static_cast<REAL>(skin.height*scale*body.bodyHeight);
            ImageAttributes attributes;attributes.SetWrapMode(WrapModeTileFlipXY);
            graphics.TranslateTransform(center,floor);
            graphics.DrawImage(&raw,RectF(-w/2,-h,w,h),0,0,skin.width,skin.height,UnitPixel,&attributes);
            graphics.Flush(FlushIntentionSync);
        };
        paintReference(6);
        const std::vector<BYTE> reference(canvas.pixels,canvas.pixels+width*height*4);
        const std::string coat=character==cat::Character::Cream?"cream":"tuxedo";
        const std::wstring wideCoat(coat.begin(),coat.end());
        const auto save=[&](const std::wstring& file,unsigned column) {
            if(!SaveNativeWalkingReview(*canvas.image,(L"render-self-test/sleep-reference/"+file).c_str()))return false;
            Graphics graphics(review.image.get());
            graphics.DrawImage(canvas.image.get(),Rect(static_cast<int>(column)*width,static_cast<int>(row)*height,width,height),0,0,width,height,UnitPixel);
            Pen ground(Color(255,149,159,169),1);graphics.DrawLine(&ground,static_cast<int>(column)*width,
                static_cast<int>(row)*height+static_cast<int>(floor),static_cast<int>(column+1)*width,static_cast<int>(row)*height+static_cast<int>(floor));
            return true;
        };
        if(!save(wideCoat+L"-reference.png",0))return 651;
        // A real shared-sleep story holds the same drawing in the companion.
        auto state=cat::companion::NewState(g.pet.bornUtc);
        state.pet=g.pet;state.preferences={character,cat::Outfit::None};state.nutrition=g.nutrition;
        cat::companion::Model companion;companion.Bind(state);companion.Reset(96,1184,740);
        cat::companion::Context peer;peer.nowUtc=utc;peer.ticks=started;peer.elapsedSeconds=.016;
        peer.primaryVisible=true;peer.primary=companion.Physical();peer.primary.x+=65;peer.primaryMotion=Motion::Sleep;
        if(!companion.TrySocial(cat::companion::Interaction::SharedSleep,peer))return 652;
        // Begin the story at a known real model tick; its action starts here.
        peer.ticks+=16;companion.Tick(peer);const auto companionStarted=peer.ticks;
        if(row)report<<',';
        report<<"{\"character\":"<<static_cast<unsigned>(character)<<",\"reference_png\":\""<<coat
            <<"-reference.png\",\"intro_ms\":"<<intro<<",\"samples\":[";
        for(unsigned i=0;i<elapsed.size();++i) {
            while(peer.ticks<companionStarted+elapsed[i]) {
                const auto step=std::min<std::uint64_t>(16,companionStarted+elapsed[i]-peer.ticks);
                peer.ticks+=step;peer.elapsedSeconds=step/1000.;companion.Tick(peer);
            }
            const auto physical=g.physics.RenderPose();
            const auto primary=PrimaryDisplayedPose(started+elapsed[i],physical);
            const auto secondary=companion.View(peer.ticks).animation;
            const auto correct=[](const cat::animation::Pose& pose) {
                return pose.frame==6&&pose.spriteClip==Clip::None&&!pose.spriteTimed&&pose.groundContact&&
                    !pose.fishMeal&&!pose.bellyRig&&!pose.centerPivot&&pose.blendFrame<0&&
                    pose.x==0&&pose.lift==0&&pose.angle==0&&pose.scaleX==1&&pose.scaleY==1&&cat::deform::Zero(pose.joints);
            };
            if(!correct(primary)||!correct(secondary))return 653;
            for(unsigned owner=0;owner<2;++owner) {
                {
                    Graphics graphics(canvas.image.get());clear(graphics);
                    DrawPetStyle(graphics,character,cat::Outfit::None,owner?secondary:primary,body,center,floor,desiredHeight);
                    graphics.Flush(FlushIntentionSync);
                }
                if(std::memcmp(canvas.pixels,reference.data(),reference.size())!=0)return 654;
                const auto file=wideCoat+(owner?L"-companion-":L"-primary-")+std::to_wstring(elapsed[i])+L".png";
                if(!save(file,1+i*2+owner))return 651;
            }
            if(i)report<<',';
            report<<"{\"elapsed_ms\":"<<elapsed[i]<<",\"base_frame\":6,\"sprite_clip\":8,\"ground_root_y\":220,\"reference_height_px\":110,"
                "\"primary_matches_reference\":true,\"companion_matches_reference\":true,\"primary_png\":\""<<coat
                <<"-primary-"<<elapsed[i]<<".png\",\"companion_png\":\""<<coat<<"-companion-"<<elapsed[i]<<".png\"}";
        }
        const auto entrance=PrimaryDisplayedPose(started+intro-1,g.physics.RenderPose());
        if(entrance.spriteClip!=Clip::Sleep)return 655;
        SetMotion(Motion::Stretch,3500);g.motionStart=started;g.nextDecision=started+3500;
        const auto waking=PrimaryDisplayedPose(started,g.physics.RenderPose());
        if(waking.spriteClip!=Clip::Sleep||waking.spriteOptions.sleep!=cat::sprite_sequence::SleepStage::Waking)return 655;
        report<<"],\"entrance_preserved\":true,\"waking_preserved\":true";
        // A fully settled neutral uses the same supplied smiling base4,
        // rather than a different front drawing from a care/video sheet.
        paintReference(4);
        const std::vector<BYTE> neutral(canvas.pixels,canvas.pixels+width*height*4);
        const auto saveNeutral=[&](const std::wstring& file,unsigned column) {
            if(!SaveNativeWalkingReview(*canvas.image,(L"render-self-test/sleep-reference/"+file).c_str()))return false;
            Graphics graphics(neutralReview.image.get());
            graphics.DrawImage(canvas.image.get(),Rect(static_cast<int>(column)*width,static_cast<int>(row)*height,width,height),0,0,width,height,UnitPixel);
            return true;
        };
        if(!saveNeutral(wideCoat+L"-neutral-reference.png",0))return 651;
        SetMotion(Motion::Idle,10000);g.motionStart=started;g.nextDecision=started+10000;
        g.groundArtBridge.Reset(started);g.waitingForStop=false;
        companion.DragTo(companion.Physical().x);peer.elapsedSeconds=.016;peer.primaryVisible=false;
        for(unsigned settle=0;settle<12;++settle){peer.ticks+=16;companion.Tick(peer);}
        const auto neutralPrimary=PrimaryDisplayedPose(started,g.physics.RenderPose());
        const auto neutralCompanion=companion.View(peer.ticks).animation;
        for(unsigned owner=0;owner<2;++owner) {
            const auto& pose=owner?neutralCompanion:neutralPrimary;
            if(pose.spriteClip!=Clip::None||pose.spriteTimed||pose.frame!=4)return 656;
            {
                Graphics graphics(canvas.image.get());clear(graphics);
                DrawPetStyle(graphics,character,cat::Outfit::None,pose,body,center,floor,desiredHeight);
                graphics.Flush(FlushIntentionSync);
            }
            if(std::memcmp(canvas.pixels,neutral.data(),neutral.size())!=0)return 656;
            if(!saveNeutral(wideCoat+(owner?L"-neutral-companion.png":L"-neutral-primary.png"),owner+1))return 651;
        }
        report<<",\"neutral_base_frame\":4,\"neutral_matches_reference\":true,\"neutral_reference_png\":\""<<coat
            <<"-neutral-reference.png\",\"neutral_primary_png\":\""<<coat<<"-neutral-primary.png\",\"neutral_companion_png\":\""
            <<coat<<"-neutral-companion.png\"}";++row;
    }
    report<<"],\"comparison_png\":\"sleep-reference-hold.png\",\"neutral_comparison_png\":\"reference-neutral-hold.png\"}\n";
    if(!SaveNativeWalkingReview(*review.image,L"render-self-test/sleep-reference/sleep-reference-hold.png")||
            !SaveNativeWalkingReview(*neutralReview.image,L"render-self-test/sleep-reference/reference-neutral-hold.png")||
            !WriteBytes(L"render-self-test/sleep-reference/report.json",report.str()))return 651;
    context="primary and companion retain exact curled base6 for both coats at registered scale/support";return 0;
}
