#pragma once

// Actual GDI+ resource rendering through the same canonical pose transition as
// the primary/companion hosts. The fixture never loads or writes user files.
int NativePostureRenderTest(std::string& context) {
    Surface canvas;
    if (!canvas.Create(420,320)) return 141;
    const auto savedPet=cat::Serialize(g.pet);
    const auto savedPreferences=cat::SerializePreferences(g.preferences);
    const auto savedNutrition=cat::nutrition::Serialize(g.nutrition);
    const auto savedMemory=cat::memory::Serialize(g.memory.state);
    const auto savedLife=cat::life::Serialize(g.life.state);
    const auto body=cat::appearance::VisualModifiers(g.nutrition,g.preferences.character);
    const auto draw=[&](cat::Character character,cat::Outfit outfit,const cat::animation::Pose& pose) {
        Graphics graphics(canvas.image.get());
        graphics.SetCompositingMode(CompositingModeSourceCopy);graphics.Clear(Color(0,0,0,0));
        graphics.SetCompositingMode(CompositingModeSourceOver);
        graphics.SetSmoothingMode(SmoothingModeAntiAlias);
        graphics.SetInterpolationMode(InterpolationModeHighQualityBicubic);
        graphics.SetPixelOffsetMode(PixelOffsetModeHighQuality);
        DrawPetStyle(graphics,character,outfit,pose,body,210,280,110);
        graphics.Flush(FlushIntentionSync);
        return graphics.GetLastStatus()==Ok;
    };
    const auto copy=[&] {return std::vector<BYTE>(canvas.pixels,canvas.pixels+420*320*4);};
    const auto inspect=[&](int& top) {
        top=320;std::size_t visible=0;
        for(int y=0;y<320;++y)for(int x=0;x<420;++x) {
            const auto* pixel=canvas.pixels+static_cast<std::size_t>(y*420+x)*4;
            if(pixel[0]>pixel[3]+1 || pixel[1]>pixel[3]+1 || pixel[2]>pixel[3]+1)return false;
            if(pixel[3]>=64) {++visible;top=std::min(top,y);}
            if((x==0||x==419||y==0||y==319) && pixel[3]>=32)return false;
        }
        return visible>=700;
    };
    for(int c=0;c<static_cast<int>(cat::kAvailableCharacters.size());++c)for(int facing:{-1,1}) {
        const auto character=cat::kAvailableCharacters[c];
        if(!EnsureCareCoats(character))return 141;
        auto loaf=cat::animation::Evaluate(Motion::Loaf,6000,500000,facing);
        auto idle=cat::animation::Evaluate(Motion::Idle,0,500000,facing);
        // Compare the posture itself, without an unrelated blinking/breathing
        // clock changing the endpoint image between otherwise identical draws.
        loaf.face=idle.face={};loaf.joints.breath=0;idle.joints={};
        for(int o=0;o<cat::kOutfitCount;++o) {
            const auto outfit=static_cast<cat::Outfit>(o);
            context="posture c="+std::to_string(c)+" facing="+std::to_string(facing)+" outfit="+std::to_string(o);
            if(!draw(character,outfit,loaf))return 142;
            const auto folded=copy();int foldedTop=0;
            if(!inspect(foldedTop))return 143;
            if(!draw(character,outfit,idle))return 142;
            const auto standing=copy();int standingTop=0;
            if(!inspect(standingTop))return 143;
            cat::animation::PoseTransition transition;
            transition.Apply(loaf,1000,1,static_cast<unsigned>(character));
            const auto first=transition.Apply(idle,1016,2,static_cast<unsigned>(character));
            if(!draw(character,outfit,first) || copy()!=folded)return 144;
            const auto middle=transition.Apply(idle,1106,2,static_cast<unsigned>(character));
            if(middle.frame!=4 || middle.blendFrame!=-1 || middle.bellyRig ||
                    middle.x!=0 || middle.lift!=0 || middle.angle!=0 ||
                    middle.scaleX!=1 || middle.scaleY!=1)return 145;
            if(!draw(character,outfit,middle))return 142;
            const auto halfway=copy();int halfwayTop=0;
            if(!inspect(halfwayTop) || halfway==folded || halfway==standing)return 146;
            // The one-source head rises with the local supported crouch. This
            // checks actual raster placement without guessing face landmarks.
            if(outfit==cat::Outfit::None && !(foldedTop>halfwayTop && halfwayTop>standingTop))return 147;
            const auto last=transition.Apply(idle,1196,2,static_cast<unsigned>(character));
            if(!draw(character,outfit,last) || copy()!=standing)return 148;
            // Reuse the same native cached skin in reverse to catch stale
            // joint controls or a transition accidentally mutating its source.
            if(!draw(character,outfit,loaf) || copy()!=folded)return 149;
        }
    }
    if(!EnsureCareCoats(cat::Character::Cream,cat::Character::Tuxedo))return 141;
    if(cat::Serialize(g.pet)!=savedPet || cat::SerializePreferences(g.preferences)!=savedPreferences ||
            cat::nutrition::Serialize(g.nutrition)!=savedNutrition || cat::memory::Serialize(g.memory.state)!=savedMemory ||
            cat::life::Serialize(g.life.state)!=savedLife)return 150;
    return 0;
}
