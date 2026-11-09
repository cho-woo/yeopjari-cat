#pragma once

// Real GDI+ dispatcher checks for both available characters. Color may differ;
// source choice, alpha, support, size, timing and physics must be identical.
int NativeCoatRenderTest(std::string& context) {
    using C=cat::Character;
    Surface cream,tuxedo,from,to,review;
    if(!cream.Create(420,340)||!tuxedo.Create(420,340)||!from.Create(420,340)||
            !to.Create(420,340)||!review.Create(1200,2400))return 440;
    {Graphics graphics(review.image.get());graphics.Clear(Color(255,235,239,243));}
    const auto body=cat::nutrition::VisualModifiers(g.nutrition);
    std::size_t coatPixels=0;unsigned row=0;
    const auto draw=[&](Surface& surface,C character,const cat::animation::Pose& pose) {
        Graphics graphics(surface.image.get());graphics.Clear(Color(0,0,0,0));
        graphics.SetSmoothingMode(SmoothingModeAntiAlias);
        graphics.SetInterpolationMode(InterpolationModeHighQualityBicubic);
        graphics.SetPixelOffsetMode(PixelOffsetModeHighQuality);
        DrawStyledAnimationDirect(graphics,pose,210,300,110.0f/CurrentArt(character).base.standingHeight,
            character,cat::Outfit::None,body,from,to,420,340);
        graphics.Flush(FlushIntentionSync);return graphics.GetLastStatus()==Ok;
    };
    for(const auto& asset:g.careSprites) {
        const auto& d=*asset->layout;
        context="two-coat native parity clip="+std::to_string(static_cast<int>(d.clip));
        if(cat::sprite_assets::Find(d.clip,8,cat::sprite_assets::kCareSprites)!=&d||
                LoadedCareSprite(d.clip,C::Tuxedo)!=asset.get())return 441;
        for(unsigned frame=0;frame<d.frameCount;++frame) {
            context="two-coat native parity clip="+std::to_string(static_cast<int>(d.clip))+" frame="+std::to_string(frame);
            const auto& a=asset->framePixels[frame];const auto& b=asset->tuxedoPixels[frame];
            if(a.size()!=b.size()||!asset->tuxedoFrames[frame])return 442;
            coatPixels+=b.size();std::size_t changed=0;
            for(std::size_t p=0;p<a.size();++p) {
                if((a[p]>>24)!=(b[p]>>24))return 443;
                changed+=a[p]!=b[p];
            }
            if(changed<100)return 444;
            cat::animation::Pose pose;pose.spriteClip=d.clip;pose.spriteFrame=frame;
            cat::animation::UseAuthoredSprite(pose);
            for(bool flip:{false,true}) {
                pose.flip=flip;
                if(!draw(cream,C::Cream,pose)||!draw(tuxedo,C::Tuxedo,pose))return 445;
                for(int p=3;p<420*340*4;p+=4)if(cream.pixels[p]!=tuxedo.pixels[p])return 446;
            }
            if(frame==0||frame==d.frameCount/2||frame==d.frameCount-1) {
                const int pair=frame==0?0:frame==d.frameCount/2?1:2;
                Graphics graphics(review.image.get());graphics.SetInterpolationMode(InterpolationModeNearestNeighbor);
                for(unsigned coat=0;coat<2;++coat) {
                    graphics.DrawImage(coat?tuxedo.image.get():cream.image.get(),Rect(pair*400+coat*200,row*240+25,200,210),110,100,200,210,UnitPixel);
                    Font font(L"Segoe UI",12,FontStyleRegular,UnitPixel);SolidBrush ink(Color(255,35,45,55));
                    const std::wstring title=(coat?L"Tuxedo":L"Cream")+std::wstring(L" / clip ")+
                        std::to_wstring(static_cast<int>(d.clip))+L" / frame "+std::to_wstring(frame);
                    graphics.DrawString(title.c_str(),-1,&font,PointF(static_cast<REAL>(pair*400+coat*200+5),static_cast<REAL>(row*240+5)),&ink);
                }
            }
        }
        ++row;
    }
    if(coatPixels!=careSpriteMemory.tuxedoPixels||coatPixels+careSpriteMemory.residentPixels>
            cat::sprite_assets::kMaxResidentPixels)return 447;
    // Fallback poses and targeted paw deformations still share the canonical
    // actor; no retired species atlas may be selected by persisted ID 8.
    for(int frame:{0,1,2,3,4,5,6,7,8,12,24,25,26,27,28}) {
        cat::animation::Pose pose;pose.frame=frame;
        if(!draw(cream,C::Cream,pose)||!draw(tuxedo,C::Tuxedo,pose))return 448;
        for(int p=3;p<420*340*4;p+=4)if(cream.pixels[p]!=tuxedo.pixels[p])return 449;
    }
    if(!SaveNativeWalkingReview(*review.image,L"render-self-test/two-cats-gdi.png"))return 450;
    return 0;
}
