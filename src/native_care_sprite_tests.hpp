#pragma once

// Synthetic-atlas regression only. This exercises the real shared GDI+
// dispatcher and registered anchors; it does not certify user artwork that
// has not been imported, or execute audio, storage, nutrition or physics.
int NativeCareSpriteRenderTest(std::string& context) {
    using Clip=cat::sprite_sequence::Clip;
    constexpr int cellWidth=24,cellHeight=32,canvasWidth=180,canvasHeight=140;
    constexpr REAL center=90,bottom=110;
    std::array<cat::sprite_assets::Descriptor,8> layouts{};
    std::array<std::vector<std::uint32_t>,8> pixels;
    struct RestoreAtlases {
        std::vector<std::unique_ptr<CareSpriteAtlas>> original;
        ~RestoreAtlases() { g.careSprites.clear();g.careSprites=std::move(original); }
    } restore{std::move(g.careSprites)};
    Surface canvas,from,to;
    if(!canvas.Create(canvasWidth,canvasHeight))return 280;
    const auto sample=[&](const std::vector<BYTE>& image,int x,int y) {
        const auto offset=static_cast<std::size_t>(y*canvasWidth+x)*4;
        return static_cast<std::uint32_t>(image[offset])|
            (static_cast<std::uint32_t>(image[offset+1])<<8)|
            (static_cast<std::uint32_t>(image[offset+2])<<16)|
            (static_cast<std::uint32_t>(image[offset+3])<<24);
    };
    for(unsigned clip=0;clip<layouts.size();++clip) {
        auto& layout=layouts[clip];layout.clip=cat::sprite_sequence::kClips[clip];
        const auto grid=cat::sprite_sequence::GridFor(layout.clip);
        layout.character=static_cast<unsigned>(cat::Character::Cream);
        layout.resourceId=cat::sprite_assets::ResourceId(layout.clip,layout.character);
        layout.columns=grid.columns;layout.rows=grid.rows;layout.frameCount=grid.count;
        layout.imageWidth=grid.columns*cellWidth;layout.imageHeight=grid.rows*cellHeight;
        layout.referenceHeight=cellHeight;
        auto& atlas=pixels[clip];atlas.assign(layout.imageWidth*layout.imageHeight,0);
        const std::uint32_t color=0xff000000u|((65+clip*20)<<16)|((180-clip*13)<<8)|(40+clip*17);
        for(unsigned index=0;index<grid.count;++index) {
            const int left=static_cast<int>(index%grid.columns)*cellWidth;
            const int top=static_cast<int>(index/grid.columns)*cellHeight;
            layout.frames[index]={{left,top,cellWidth,cellHeight},{12,30}};
            const auto rectangle=[&](int x0,int y0,int x1,int y1,std::uint32_t fill) {
                for(int y=y0;y<y1;++y)for(int x=x0;x<x1;++x)
                    atlas[static_cast<std::size_t>(top+y)*layout.imageWidth+left+x]=fill;
            };
            // Different silhouette heights share the SAME canvas, scale and
            // foot anchor. A per-frame tight crop would make the paws jump.
            rectangle(8,index%2?9:4,20,14,color);
            rectangle(8,14,18,28,color);rectangle(3,16,8,21,color);
            rectangle(8,28,18,30,0xffeee6d2u);
            rectangle(11,11,14,13,0xff000000u|((35+index*7)<<16)|0x004783u);
        }
        if(!cat::sprite_assets::Valid(layout))return 281;
        auto sprite=std::make_unique<CareSpriteAtlas>();sprite->layout=&layout;
        sprite->image=std::make_unique<Bitmap>(layout.imageWidth,layout.imageHeight,layout.imageWidth*4,
            PixelFormat32bppPARGB,reinterpret_cast<BYTE*>(atlas.data()));
        if(sprite->image->GetLastStatus()!=Ok)return 282;
        g.careSprites.push_back(std::move(sprite));
    }
    const auto body=cat::nutrition::VisualModifiers(g.nutrition);
    auto normal=body;normal.bodyWidth=normal.bodyHeight=1;
    const auto draw=[&](const cat::animation::Pose& pose,cat::Character character,
                        const cat::nutrition::Visual& shape,REAL desiredHeight,bool direct) {
        Graphics graphics(canvas.image.get());
        graphics.SetCompositingMode(CompositingModeSourceCopy);graphics.Clear(Color(0,0,0,0));
        graphics.SetCompositingMode(CompositingModeSourceOver);
        graphics.SetInterpolationMode(InterpolationModeHighQualityBicubic);
        graphics.SetPixelOffsetMode(PixelOffsetModeHighQuality);
        const REAL scale=desiredHeight/std::max(1,CurrentArt(character).base.standingHeight);
        const bool success=direct?DrawCareSprite(graphics,pose,center,bottom,scale,character,shape):true;
        if(!direct)DrawStyledAnimation(graphics,pose,center,bottom,scale,character,cat::Outfit::None,
            shape,from,to,canvasWidth,canvasHeight);
        graphics.Flush(FlushIntentionSync);
        return success&&graphics.GetLastStatus()==Ok;
    };
    const auto copy=[&] {return std::vector<BYTE>(canvas.pixels,canvas.pixels+canvasWidth*canvasHeight*4);};
    const auto bounds=[&](const std::vector<BYTE>& image) {
        std::array<int,4> result{{canvasWidth,canvasHeight,0,0}};
        for(int y=0;y<canvasHeight;++y)for(int x=0;x<canvasWidth;++x)
            if((sample(image,x,y)>>24)>200) {
                result[0]=std::min(result[0],x);result[1]=std::min(result[1],y);
                result[2]=std::max(result[2],x+1);result[3]=std::max(result[3],y+1);
            }
        return result;
    };
    for(unsigned clip=0;clip<layouts.size();++clip) {
        context="synthetic care atlas clip="+std::to_string(clip);
        cat::animation::Pose pose;pose.spriteClip=layouts[clip].clip;pose.spriteFrame=3;
        if(!draw(pose,cat::Character::Cream,normal,32,true))return 283;
        const auto expected=copy();
        // Authored pixels already describe the pose. Legacy procedural
        // articulation and whole-body transformations must not double it.
        pose.angle=3;pose.scaleX=.83;pose.scaleY=1.17;pose.centerPivot=true;
        pose.joints.headNod=.8;pose.joints.frontLeft=.7;pose.joints.crouch=.6;
        pose.face.blink=.9;pose.bellyRig=true;pose.walkingRig=true;pose.jointWalk=true;
        if(!draw(pose,cat::Character::Cream,normal,32,false)||copy()!=expected)return 284;
        const auto color=0xff000000u|((65+clip*20)<<16)|((180-clip*13)<<8)|(40+clip*17);
        if(sample(expected,90,100)!=color)return 285;
    }
    cat::animation::Pose pose;pose.spriteClip=Clip::Groom;
    context="synthetic care atlas fixed support across content heights";
    if(!draw(pose,cat::Character::Cream,normal,32,false))return 283;
    const auto tall=copy();pose.spriteFrame=1;
    if(!draw(pose,cat::Character::Cream,normal,32,false))return 283;
    const auto shortFrame=copy();
    const auto tallBounds=bounds(tall),shortBounds=bounds(shortFrame);
    if(tallBounds[1]>=shortBounds[1]||tallBounds[3]!=shortBounds[3])return 286;
    for(int y=96;y<canvasHeight;++y)for(int x=0;x<canvasWidth;++x)
        if(sample(tall,x,y)!=sample(shortFrame,x,y))return 286;
    context="synthetic care atlas horizontal flip";
    pose.spriteFrame=0;pose.flip=true;
    if(!draw(pose,cat::Character::Cream,normal,32,false))return 283;
    const auto mirrored=copy();
    for(int y=0;y<canvasHeight;++y)for(int x=0;x<canvasWidth;++x)for(int channel=0;channel<4;++channel) {
        const auto a=static_cast<std::size_t>(y*canvasWidth+x)*4+channel;
        const auto b=static_cast<std::size_t>(y*canvasWidth+(canvasWidth-1-x))*4+channel;
        if(std::abs(static_cast<int>(mirrored[a])-static_cast<int>(tall[b]))>1)return 287;
    }
    context="synthetic care atlas body and DPI scaling";
    pose.flip=false;auto shaped=normal;shaped.bodyWidth=1.25;shaped.bodyHeight=.75;
    if(!draw(pose,cat::Character::Cream,shaped,64,false))return 283;
    const auto grown=bounds(copy());
    // Source alpha bounds are [3,4,20,30]. Desired height doubles the
    // source scale; body width/height apply independently around (12,30).
    const std::array<double,4> expectedBounds{{center+(3-12)*2.5,bottom+(4-30)*1.5,
        center+(20-12)*2.5,bottom}};
    for(unsigned edge=0;edge<4;++edge)
        if(std::abs(grown[edge]-expectedBounds[edge])>1.1)return 288;
    context="synthetic care atlas airborne ground-clip rejection";
    pose.groundContact=false;
    if(CareSpriteFor(pose,cat::Character::Cream)||draw(pose,cat::Character::Cream,normal,32,true))return 289;
    if(!draw(pose,cat::Character::Cream,normal,32,false))return 283;
    const auto groundedFallback=copy();pose.spriteClip=Clip::None;
    if(!draw(pose,cat::Character::Cream,normal,32,false)||copy()!=groundedFallback)return 289;
    context="synthetic care atlas missing species keeps its own fallback";
    pose.groundContact=true;pose.spriteClip=Clip::Groom;
    if(CareSpriteFor(pose,cat::Character::SilverTabby))return 290;
    if(!draw(pose,cat::Character::SilverTabby,normal,32,false))return 283;
    const auto silver=copy();pose.spriteClip=Clip::None;
    if(!draw(pose,cat::Character::SilverTabby,normal,32,false)||copy()!=silver||silver==tall)return 290;
    context="synthetic care atlas actual airborne jump selection";
    pose.spriteClip=Clip::Jump;pose.spriteFrame=8;pose.groundContact=false;
    if(!CareSpriteFor(pose,cat::Character::Cream)||!draw(pose,cat::Character::Cream,normal,32,true))return 291;
    const auto flight=copy();
    if(!draw(pose,cat::Character::Cream,normal,32,false)||copy()!=flight)return 291;
    context="synthetic care atlas fish variant selection";
    pose.groundContact=true;pose.spriteClip=Clip::Eat;pose.spriteFrame=3;pose.fishMeal=true;
    const auto* fish=CareSpriteFor(pose,cat::Character::Cream);
    if(!fish||fish->layout->clip!=Clip::Fish||!draw(pose,cat::Character::Cream,normal,32,false))return 292;
    const auto fishPixels=copy();pose.fishMeal=false;pose.spriteClip=Clip::Fish;
    if(!draw(pose,cat::Character::Cream,normal,32,false)||copy()!=fishPixels)return 292;
    pose.spriteClip=Clip::Eat;
    if(!draw(pose,cat::Character::Cream,normal,32,false)||copy()==fishPixels)return 292;
    context="synthetic care atlas invalid frame fallback";
    pose.spriteFrame=cat::sprite_assets::kMaxFrames;
    if(CareSpriteFor(pose,cat::Character::Cream))return 293;
    return 0;
}
