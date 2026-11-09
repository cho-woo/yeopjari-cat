#pragma once

// Exercises the optional presentation path with real embedded resources and
// actual premultiplied GDI+ DIBs. Existing direct-render tests remain separate.
int NativeCarePresentationRenderTest(std::string& context) {
    using Clip=cat::sprite_sequence::Clip;
    if(!cat::sprite_assets::Find(Clip::Groom,0,cat::sprite_assets::kCareSprites))return 0;
    Surface canvas;
    if(!canvas.Create(420,320))return 330;
    const auto body=cat::nutrition::VisualModifiers(g.nutrition);
    auto idle=cat::animation::Evaluate(Motion::Idle,0,500000,1);idle.face={};idle.joints={};
    auto groom=idle;cat::animation::AttachSprite(groom,Clip::Groom,0);cat::animation::UseAuthoredSprite(groom);
    auto neutral=idle;neutral.spriteClip=Clip::Sleep;neutral.spriteFrame=0;neutral.spriteTimed=false;
    cat::animation::UseAuthoredSprite(neutral);
    const auto draw=[&](const cat::animation::Pose& pose,CarePresentationRenderer* state,
                        std::uint64_t ticks,std::uint64_t generation=0,bool paused=false,
                        cat::Character character=cat::Character::Cream) {
        Graphics graphics(canvas.image.get());
        graphics.SetCompositingMode(CompositingModeSourceCopy);graphics.Clear(Color(0,0,0,0));
        graphics.SetCompositingMode(CompositingModeSourceOver);
        graphics.SetInterpolationMode(InterpolationModeHighQualityBicubic);
        graphics.SetSmoothingMode(SmoothingModeAntiAlias);graphics.SetPixelOffsetMode(PixelOffsetModeHighQuality);
        DrawPetStyle(graphics,character,cat::Outfit::None,pose,body,210,280,110,state,ticks,generation,paused);
        graphics.Flush(FlushIntentionSync);
        return std::vector<BYTE>(canvas.pixels,canvas.pixels+420*320*4);
    };
    CarePresentationRenderer other;
    auto& primary=PrimaryCarePresentation();primary.Reset();
    context="single-drawing care boundaries: no ghosting or canvas capture allocation";
    for(auto character:cat::kAvailableCharacters) {
        for(const auto& pose:{idle,groom,neutral,idle,groom}) {
            const auto actual=draw(pose,&primary,1126,0,false,character);
            if(actual!=draw(pose,nullptr,1126,0,false,character))return 331;
            if(primary.previous.image||primary.from.image||primary.target.image)return 332;
        }
    }
    // Contact, flight and locomotion take the exact direct path, without a
    // transparent trailing face/paw or delayed physical impact.
    for(const auto motion:{Motion::Punch,Motion::Walk,Motion::Jump}) {
        primary.Reset();draw(idle,&primary,7000);
        auto pose=cat::animation::Evaluate(motion,350,7000,1);
        if(motion==Motion::Jump)pose=cat::animation::AirbornePose(150,1);
        if(CareSpriteFor(pose,cat::Character::Cream))cat::animation::UseAuthoredSprite(pose);
        if(draw(pose,&primary,7016)!=draw(pose,nullptr,7016))return 339;
    }
    primary.Reset();draw(idle,&primary,8000);draw(groom,&primary,8016);
    if(draw(groom,&primary,8126,2)!=draw(groom,&other,8126,2))return 340;
    ResetPrimaryCarePresentation();other.Reset();CompanionCarePresentation().Reset();
    return 0;
}
