#pragma once

int NativeWalkingRenderTest(std::string& context) {
    Surface canvas;
    if(!canvas.Create(420,320))return 151;
    const auto savedPreferences=cat::SerializePreferences(g.preferences);
    const auto savedNutrition=cat::nutrition::Serialize(g.nutrition);
    const auto normalBody=cat::nutrition::VisualModifiers(g.nutrition);
    const auto copy=[&] {return std::vector<BYTE>(canvas.pixels,canvas.pixels+420*320*4);};
    const auto draw=[&](cat::Character character,cat::Outfit outfit,const cat::animation::Pose& pose,
                        const cat::nutrition::Visual& body,bool direct,REAL bottom=280) {
        Graphics graphics(canvas.image.get());
        graphics.SetCompositingMode(CompositingModeSourceCopy);graphics.Clear(Color(0,0,0,0));
        graphics.SetCompositingMode(CompositingModeSourceOver);
        graphics.SetSmoothingMode(SmoothingModeAntiAlias);
        graphics.SetInterpolationMode(InterpolationModeHighQualityBicubic);
        graphics.SetPixelOffsetMode(PixelOffsetModeHighQuality);
        const REAL scale=110.0f/std::max(1,CurrentArt(character).base.standingHeight);
        const bool success=direct?DrawWalkingPet(graphics,pose,210,bottom,scale,character,outfit,body):true;
        if(!direct)DrawPetStyle(graphics,character,outfit,pose,body,210,bottom,110);
        graphics.Flush(FlushIntentionSync);
        return success && graphics.GetLastStatus()==Ok;
    };
    const auto poseFor=[](Motion motion,int facing) {
        cat::animation::Locomotion gait;gait.Advance(motion,1,1.0/60);
        return cat::animation::Evaluate(motion,0,500000,facing,gait);
    };
    for(int c=0;c<cat::kCharacterCount;++c)for(int facing:{-1,1}) {
        const auto character=static_cast<cat::Character>(c);
        auto& art=CurrentArt(character);
        const auto& source=art.base.skins[cat::species::UsesNewArt(character)?2:1];
        const double reference=110.0/std::max(1,art.base.standingHeight);
        const double height=source.height*reference*normalBody.bodyHeight;
        // Stay above the limb roots and the bicubic sampling fringe. This
        // includes the entire face and upper body for every canonical source.
        const int headBottom=static_cast<int>(std::floor(280+2*height/source.height-.34*height));
        for(const auto motion:{Motion::Walk,Motion::Stalk,Motion::Zoomies}) {
            auto pose=poseFor(motion,facing);
            if(!pose.walkingRig || pose.frame!=1 || pose.blendFrame!=-1 ||
                    pose.x!=0 || pose.lift!=0 || pose.angle!=0 || pose.scaleX!=1 || pose.scaleY!=1)return 152;
            std::array<std::vector<BYTE>,9> bare;
            for(int o=0;o<cat::kOutfitCount;++o) {
                // Every outfit traverses a complete walk; the other two
                // physical gait modes also exercise bare and body-clothed art.
                if(motion!=Motion::Walk && o!=0 && o!=static_cast<int>(cat::Outfit::Hoodie))continue;
                const auto outfit=static_cast<cat::Outfit>(o);
                context="walking c="+std::to_string(c)+" facing="+std::to_string(facing)+
                    " motion="+std::to_string(static_cast<int>(motion))+" outfit="+std::to_string(o);
                const auto caseContext=context;
                std::vector<BYTE> planted;
                std::size_t changedLegs=0,plantedVisible=0;
                for(unsigned phase=0;phase<=8;++phase) {
                    context=caseContext+" phase="+std::to_string(phase)+"/8";
                    pose.walkPhase=phase/8.0;
                    if(!draw(character,outfit,pose,normalBody,true) || !art.walking || !art.walking->rig.Ready())return 153;
                    const auto pixels=copy();
                    if(outfit==cat::Outfit::None)bare[phase]=pixels;
                    else {
                        std::size_t changed=0,bodyPixels=0,covered=0;
                        for(std::size_t p=0;p<pixels.size();p+=4) {
                            changed+=std::memcmp(bare[phase].data()+p,pixels.data()+p,4)!=0;
                            if(bare[phase][p+3]>=200) {++bodyPixels;if(pixels[p+3]>=190)++covered;}
                        }
                        if(changed<8 || covered*100<bodyPixels*95){context+=" missing outfit or erased body";return 159;}
                    }
                    const auto& rig=art.walking->rig;
                    if(rig.SourceWidth()!=source.width || rig.Height()!=source.height ||
                            rig.Width()!=source.width+2*rig.PaddingX())return 154;
                    // Actual resource RGBA, not just abstract head landmarks.
                    for(int y=0;y<static_cast<int>(source.height*.68);++y)
                        if(std::memcmp(source.source.data()+y*source.width,rig.Pixels()+y*rig.Width()+rig.PaddingX(),
                                static_cast<std::size_t>(source.width)*4)!=0)return 154;
                    if(phase==0)planted=pixels;
                    std::size_t visible=0;
                    for(int y=0;y<320;++y)for(int x=0;x<420;++x) {
                        const auto p=static_cast<std::size_t>(y*420+x)*4;
                        const auto* pixel=pixels.data()+p;
                        if(pixel[0]>pixel[3]+1 || pixel[1]>pixel[3]+1 || pixel[2]>pixel[3]+1){context+=" invalid premultiplied alpha";return 155;}
                        if(pixel[3]>=64)++visible;
                        if((x==0||x==419||y==0||y==319) && pixel[3]>=32){context+=" canvas edge";return 155;}
                        if(outfit==cat::Outfit::None && y>282 && pixel[3]>=128){context+=" below support";return 155;}
                        const bool changed=std::memcmp(planted.data()+p,pixel,4)!=0;
                        if(y<headBottom && changed)return 156; // Head and headwear never pulse or crossfade.
                        if(y>=headBottom && changed)++changedLegs;
                    }
                    if(phase==0)plantedVisible=visible;
                    if(visible<700 || visible*100<plantedVisible*90 || visible*100>plantedVisible*110 ||
                            (phase==8 && pixels!=planted))return 157;
                    // The real top-level dispatcher must select this rig, not
                    // silently pass by falling back to the old sprite strip.
                    if(!draw(character,outfit,pose,normalBody,false) || copy()!=pixels)return 158;
                }
                if(changedLegs<40)return 157;
                pose.walkPhase=0;
                if(!draw(character,outfit,pose,normalBody,true) || copy()!=planted)return 157;
            }
        }
    }
    // At the same stride/phase, changing body condition must invalidate the
    // cached limb normalization. Compare it with an independent freshly
    // prepared rig using the actual embedded legacy and new-art sources.
    for(const auto character:{cat::Character::Cream,cat::Character::Rabbit}) {
        auto& art=CurrentArt(character);
        const auto& source=art.base.skins[cat::species::UsesNewArt(character)?2:1];
        const double reference=110.0/std::max(1,art.base.standingHeight);
        auto pose=poseFor(Motion::Walk,1);pose.walkPhase=.375;
        for(const auto dimensions:{std::pair<double,double>{1,1},{.82,1.04},{1.25,.96},{1,1}}) {
            auto body=normalBody;body.bodyWidth=dimensions.first;body.bodyHeight=dimensions.second;
            context="walking nutrition c="+std::to_string(static_cast<int>(character))+" width="+std::to_string(body.bodyWidth);
            if(!draw(character,cat::Outfit::Scarf,pose,body,true) || !art.walking || !art.walking->rig.Ready())return 159;
            cat::walk::Rig expected;
            if(!expected.Prepare(source.source.data(),source.width,source.height,source.width*4,cat::walk::ProfileFor(character)) ||
                    !expected.Raster(pose.walkPhase,pose.walkStride,source.width*reference*body.bodyWidth,
                        source.height*reference*body.bodyHeight,pose.walkDuty,pose.walkLift) ||
                    expected.Width()!=art.walking->rig.Width() || expected.Height()!=art.walking->rig.Height() ||
                    std::memcmp(expected.Pixels(),art.walking->rig.Pixels(),static_cast<std::size_t>(expected.Width())*expected.Height()*4)!=0)return 159;
        }
    }
    if(cat::SerializePreferences(g.preferences)!=savedPreferences || cat::nutrition::Serialize(g.nutrition)!=savedNutrition)return 159;
    return 0;
}
