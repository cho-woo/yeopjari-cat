#pragma once

inline bool NativeCarePngEncoder(CLSID& result) {
    UINT count=0,bytes=0;
    if(GetImageEncodersSize(&count,&bytes)!=Ok||!count||!bytes)return false;
    std::vector<BYTE> storage(bytes);auto* encoders=reinterpret_cast<ImageCodecInfo*>(storage.data());
    if(GetImageEncoders(count,bytes,encoders)!=Ok)return false;
    for(UINT index=0;index<count;++index)if(encoders[index].MimeType&&std::wcscmp(encoders[index].MimeType,L"image/png")==0) {
        result=encoders[index].Clsid;return true;
    }
    return false;
}

// Isolate the retained canonical renderer without altering its pixel proofs.
// Authored resources are exercised separately below and restored on every exit.
class DisabledCareSpriteScope {
public:
    DisabledCareSpriteScope() { saved_.swap(g.careSprites); }
    ~DisabledCareSpriteScope() { g.careSprites.clear();saved_.swap(g.careSprites); }
    DisabledCareSpriteScope(const DisabledCareSpriteScope&)=delete;
    DisabledCareSpriteScope& operator=(const DisabledCareSpriteScope&)=delete;
private:
    std::vector<std::unique_ptr<CareSpriteAtlas>> saved_;
};

// Synthetic-atlas regression only. This exercises the real shared GDI+
// dispatcher and registered anchors; it does not certify user artwork that
// has not been imported, or execute audio, storage, nutrition or physics.
int NativeCareSpriteRenderTest(std::string& context) {
    using Clip=cat::sprite_sequence::Clip;
    constexpr int cellWidth=24,cellHeight=32,canvasWidth=180,canvasHeight=140;
    constexpr REAL center=90,bottom=110;
    std::array<cat::sprite_assets::Descriptor,8> layouts{};
    std::array<std::vector<std::uint32_t>,8> pixels;
    DisabledCareSpriteScope restore;
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

// The production decoder releases its whole atlas after prewarming frames.
// Tests decode the embedded resource independently so source-reference checks
// do not accidentally compare a cached frame with the same cached frame.
class NativeCareSourceReference {
public:
    std::unique_ptr<Bitmap> image;
    IStream* stream=nullptr;
    ~NativeCareSourceReference(){image.reset();if(stream)stream->Release();}
    bool Load(const cat::sprite_assets::Descriptor& layout) {
        const auto resource=FindResourceW(g.instance,MAKEINTRESOURCEW(layout.resourceId),RT_RCDATA);
        const auto loaded=resource?LoadResource(g.instance,resource):nullptr;
        const DWORD count=resource?SizeofResource(g.instance,resource):0;
        const void* bytes=loaded?LockResource(loaded):nullptr;
        if(!bytes||!count)return false;
        HGLOBAL memory=GlobalAlloc(GMEM_MOVEABLE,count);if(!memory)return false;
        void* target=GlobalLock(memory);if(!target){GlobalFree(memory);return false;}
        std::memcpy(target,bytes,count);GlobalUnlock(memory);
        if(FAILED(CreateStreamOnHGlobal(memory,TRUE,&stream))){GlobalFree(memory);return false;}
        image.reset(Bitmap::FromStream(stream,FALSE));
        return image&&image->GetLastStatus()==Ok&&image->GetWidth()==layout.imageWidth&&image->GetHeight()==layout.imageHeight;
    }
};

// Unlike the fixture above, this reads every imported PNG from its actual
// embedded resource. The independent cropped Bitmap verifies that the common
// dispatcher selects the requested source rectangle and registered anchor.
int NativeActualCareSpriteRenderTest(std::string& context) {
    using Clip=cat::sprite_sequence::Clip;
    if(cat::sprite_assets::kCareSprites.empty()) {
        context="no imported care resources in this development build";return 0;
    }
    context="actual care resource inventory";
    if(g.careSprites.size()!=cat::sprite_assets::kCareSprites.size())return 294;
    std::vector<std::pair<unsigned,unsigned>> found;
    std::vector<int> resources;std::size_t totalPixels=0,totalFrames=0;
    for(const auto& asset:g.careSprites) {
        if(!asset||!asset->layout||asset->image||asset->stream||!cat::sprite_assets::Valid(*asset->layout))return 294;
        const auto& layout=*asset->layout;
        const auto key=std::make_pair(layout.character,static_cast<unsigned>(layout.clip));
        if(std::find(found.begin(),found.end(),key)!=found.end())return 294;
        found.push_back(key);
        if(std::find(resources.begin(),resources.end(),layout.resourceId)==resources.end())resources.push_back(layout.resourceId);
        if(!std::isfinite(layout.referenceHeight)||layout.referenceHeight<=0)return 294;
        for(unsigned index=0;index<layout.frameCount;++index) {
            const auto& rect=layout.frames[index].rect;
            const std::size_t pixels=static_cast<std::size_t>(rect.width)*rect.height;
            if(!asset->frames[index]||asset->framePixels[index].size()!=pixels)return 294;
            totalPixels+=pixels;++totalFrames;
        }
    }
    if(totalPixels!=careSpriteMemory.residentPixels||totalFrames!=careSpriteMemory.residentFrames||
            resources.size()!=careSpriteMemory.decodedAtlases||totalPixels>cat::sprite_assets::kMaxResidentPixels)return 294;
    constexpr int width=640,height=480;
    constexpr REAL center=320,bottom=400;
    Surface canvas,from,to;
    if(!canvas.Create(width,height))return 295;
    auto normal=cat::nutrition::VisualModifiers(g.nutrition);normal.bodyWidth=normal.bodyHeight=1;
    const auto preferences=cat::SerializePreferences(g.preferences);
    const auto nutrition=cat::nutrition::Serialize(g.nutrition);
    const auto configure=[](Graphics& graphics) {
        graphics.SetCompositingMode(CompositingModeSourceCopy);graphics.Clear(Color(0,0,0,0));
        graphics.SetCompositingMode(CompositingModeSourceOver);
        graphics.SetInterpolationMode(InterpolationModeHighQualityBicubic);
        graphics.SetPixelOffsetMode(PixelOffsetModeHighQuality);
    };
    const auto copy=[&] {return std::vector<BYTE>(canvas.pixels,canvas.pixels+width*height*4);};
    const auto draw=[&](const cat::animation::Pose& pose,cat::Character character,
                        const cat::nutrition::Visual& body,REAL desiredHeight,bool direct) {
        Graphics graphics(canvas.image.get());configure(graphics);
        const REAL scale=desiredHeight/std::max(1,CurrentArt(character).base.standingHeight);
        const bool success=direct?DrawCareSprite(graphics,pose,center,bottom,scale,character,body):true;
        if(!direct)DrawStyledAnimation(graphics,pose,center,bottom,scale,character,cat::Outfit::None,
            body,from,to,width,height);
        graphics.Flush(FlushIntentionSync);
        return success&&graphics.GetLastStatus()==Ok;
    };
    const auto inspect=[&](const std::vector<BYTE>& image,std::array<int,4>& bounds) {
        bounds={{width,height,0,0}};std::size_t visible=0;
        for(int y=0;y<height;++y)for(int x=0;x<width;++x) {
            const auto* p=image.data()+static_cast<std::size_t>(y*width+x)*4;
            if(p[0]>p[3]+1||p[1]>p[3]+1||p[2]>p[3]+1)return false;
            if((x<=1||x>=width-2||y<=1||y>=height-2)&&p[3]>=32)return false;
            if(p[3]>=128) {
                ++visible;bounds[0]=std::min(bounds[0],x);bounds[1]=std::min(bounds[1],y);
                bounds[2]=std::max(bounds[2],x+1);bounds[3]=std::max(bounds[3],y+1);
            }
        }
        return visible>=100;
    };
    const auto nearPixels=[](const std::vector<BYTE>& a,const std::vector<BYTE>& b) {
        if(a.size()!=b.size())return false;
        // Source GetPixel -> manual premultiplication and GDI+ LockBits use
        // slightly different RGB rounding. Bicubic filtering can amplify it
        // to2/255 in a few edge channels. Alpha/coverage stays at1/255, so
        // this cannot conceal a wrong frame, anchor or opaque padding.
        for(std::size_t i=0;i<a.size();++i)
            if(std::abs(static_cast<int>(a[i])-static_cast<int>(b[i]))>(i%4==3?1:2))return false;
        return true;
    };
    const auto decodeReference=[](Bitmap& atlas,const cat::sprite_assets::Descriptor& layout,unsigned index,std::vector<std::uint32_t>& pixels) {
        const auto& rect=layout.frames[index].rect;
        pixels.resize(static_cast<std::size_t>(rect.width)*rect.height);
        // GetPixel is independent of the production LockBits frame cache.
        for(int y=0;y<rect.height;++y)for(int x=0;x<rect.width;++x) {
            Color color;
            if(atlas.GetPixel(rect.x+x,rect.y+y,&color)!=Ok)return false;
            const unsigned alpha=color.GetA()<=16?0:color.GetA();
            pixels[static_cast<std::size_t>(y)*rect.width+x]=(alpha<<24)|
                (((color.GetR()*alpha+127)/255)<<16)|(((color.GetG()*alpha+127)/255)<<8)|
                ((color.GetB()*alpha+127)/255);
        }
        return true;
    };
    const auto drawReference=[&](const cat::sprite_assets::Descriptor& layout,unsigned index,
            const std::vector<std::uint32_t>& pixels,const cat::nutrition::Visual& body,REAL desiredHeight,bool flip) {
        const auto& f=layout.frames[index];
        Bitmap crop(f.rect.width,f.rect.height,f.rect.width*4,PixelFormat32bppPARGB,
            reinterpret_cast<BYTE*>(const_cast<std::uint32_t*>(pixels.data())));
        if(crop.GetLastStatus()!=Ok)return false;
        Graphics graphics(canvas.image.get());configure(graphics);
        // Match the physical transform exactly. An absolute RectF rounds its
        // origin differently from a translated local RectF (Groom14 differs
        // by1/65536px), changing GDI+'s bicubic alpha weights.
        const int standingHeight=CurrentArt(static_cast<cat::Character>(layout.character)).base.standingHeight;
        const REAL callerScale=desiredHeight/std::max(1,standingHeight);
        const double referenceScale=callerScale*standingHeight/layout.referenceHeight;
        const REAL sx=static_cast<REAL>(referenceScale*body.bodyWidth);
        const REAL sy=static_cast<REAL>(referenceScale*body.bodyHeight);
        graphics.TranslateTransform(center,bottom);
        graphics.ScaleTransform(flip&&layout.clip!=Clip::Box?-1.0f:1.0f,1);
        ImageAttributes attributes;attributes.SetWrapMode(WrapModeClamp,Color(0,0,0,0));
        const auto status=graphics.DrawImage(&crop,RectF(static_cast<REAL>(-f.anchor.x*sx),
            static_cast<REAL>(-f.anchor.y*sy),f.rect.width*sx,f.rect.height*sy),
            0,0,f.rect.width,f.rect.height,UnitPixel,&attributes);
        graphics.Flush(FlushIntentionSync);
        return status==Ok&&graphics.GetLastStatus()==Ok;
    };
    if(!CreateDirectoryW(L"render-self-test",nullptr)&&GetLastError()!=ERROR_ALREADY_EXISTS)return 310;
    Surface review;
    if(!review.Create(1920,static_cast<int>((totalFrames+2+7)/8)*260))return 310;
    {Graphics graphics(review.image.get());graphics.Clear(Color(255,235,239,243));}
    unsigned reviewCell=0;
    for(const auto& asset:g.careSprites) {
        const auto& layout=*asset->layout;
        const auto character=static_cast<cat::Character>(layout.character);
        NativeCareSourceReference original;
        if(!original.Load(layout))return 296;
        std::array<std::array<int,4>,cat::sprite_assets::kMaxFrames> sourceBounds{};
        {
            BitmapData decoded{};Rect whole(0,0,layout.imageWidth,layout.imageHeight);
            if(original.image->LockBits(&whole,ImageLockModeRead,PixelFormat32bppPARGB,&decoded)!=Ok)return 296;
            struct UnlockBitmap {Bitmap* image;BitmapData* data;~UnlockBitmap(){image->UnlockBits(data);}} unlock{original.image.get(),&decoded};
            for(unsigned frame=0;frame<layout.frameCount;++frame) {
                context="actual care source clip="+std::to_string(static_cast<unsigned>(layout.clip))+" frame="+std::to_string(frame);
                const auto& rect=layout.frames[frame].rect;
                auto& bounds=sourceBounds[frame];bounds={{rect.width,rect.height,0,0}};
                std::size_t clear=0,visible=0;unsigned clearCorners=0;
                for(int y=0;y<rect.height;++y) {
                    const auto* row=static_cast<const BYTE*>(decoded.Scan0)+static_cast<ptrdiff_t>(rect.y+y)*decoded.Stride+rect.x*4;
                    for(int x=0;x<rect.width;++x) {
                        const auto* p=row+x*4;
                        if(p[0]>p[3]+1||p[1]>p[3]+1||p[2]>p[3]+1)return 297;
                        clear+=p[3]<=16;
                        if((x==0||x==rect.width-1)&&(y==0||y==rect.height-1)&&p[3]<=16)++clearCorners;
                        if(p[3]>=128) {
                            ++visible;bounds[0]=std::min(bounds[0],x);bounds[1]=std::min(bounds[1],y);
                            bounds[2]=std::max(bounds[2],x+1);bounds[3]=std::max(bounds[3],y+1);
                        }
                    }
                }
                // An opaque white/beige panel or solid crop cannot pass as a
                // transparent sprite merely because another tile has alpha.
                if(clear<static_cast<std::size_t>(rect.width)*rect.height/100||visible<100||!clearCorners)return 298;
            }
        }
        for(unsigned frame=0;frame<layout.frameCount;++frame) {
            context="actual care selected ROI clip="+std::to_string(static_cast<unsigned>(layout.clip))+" frame="+std::to_string(frame);
            cat::animation::Pose pose;pose.spriteTimed=false;pose.spriteClip=layout.clip;pose.spriteFrame=frame;
            pose.groundContact=layout.clip!=Clip::Jump;
            if(CareSpriteFor(pose,character)!=asset.get()||!draw(pose,character,normal,110,true))return 299;
            const auto direct=copy();
            const auto& f=layout.frames[frame];
            // Independently decoded PARGB input verifies selected source
            // ownership without straight-alpha interpolation differences.
            std::vector<std::uint32_t> reference;
            if(!decodeReference(*original.image,layout,frame,reference)||
                    !drawReference(layout,frame,reference,normal,110,false))return 300;
            if(!nearPixels(copy(),direct)){
                const auto expected=copy();unsigned maxRgb=0,maxAlpha=0;std::size_t firstBad=expected.size(),changed=0;
                for(std::size_t i=0;i<expected.size();++i) {
                    const auto delta=static_cast<unsigned>(std::abs(static_cast<int>(expected[i])-direct[i]));
                    if(i%4==3)maxAlpha=std::max(maxAlpha,delta);else maxRgb=std::max(maxRgb,delta);
                    if(delta>(i%4==3?1u:2u)){if(firstBad==expected.size())firstBad=i;++changed;}
                }
                context+=" wrong source rectangle/anchor maxRgb="+std::to_string(maxRgb)+
                    " maxAlpha="+std::to_string(maxAlpha)+" channels="+std::to_string(changed);
                if(firstBad<expected.size())context+=" first=("+std::to_string(firstBad/4%width)+","+
                    std::to_string(firstBad/4/width)+",channel"+std::to_string(firstBad%4)+") expected="+
                    std::to_string(expected[firstBad])+" actual="+std::to_string(direct[firstBad]);
                return 301;
            }
            pose.angle=4;pose.scaleX=.8;pose.scaleY=1.2;pose.joints.headNod=.7;
            pose.joints.crouch=.8;pose.joints.frontLeft=.6;pose.face.blink=.9;
            if(!draw(pose,character,normal,110,false)||copy()!=direct)return 302;
            std::array<int,4> bounds{};
            if(!inspect(direct,bounds)){context+=" invalid alpha, missing body or canvas clipping";return 303;}
            // Authored props may extend below the anatomical support. The
            // shared host policy reserves this canvas without moving the root.
            const auto safety=cat::render::SpriteBottomSafetyInset(layout,110,normal.bodyHeight);
            if(pose.groundContact&&bounds[3]>bottom+safety+2){
                const auto& src=sourceBounds[frame];
                context+=" below registered support actualBottom="+std::to_string(bounds[3])+
                    " ground="+std::to_string(bottom)+" anchorY="+std::to_string(f.anchor.y)+
                    " reference="+std::to_string(layout.referenceHeight)+
                    " sourceBounds="+std::to_string(src[0])+","+std::to_string(src[1])+","+
                        std::to_string(src[2])+","+std::to_string(src[3]);return 304;
            }
            {
                Graphics graphics(review.image.get());graphics.SetInterpolationMode(InterpolationModeNearestNeighbor);
                graphics.SetPixelOffsetMode(PixelOffsetModeHalf);
                const int left=static_cast<int>(reviewCell%8)*240,top=static_cast<int>(reviewCell/8)*260;
                Pen ground(Color(255,145,154,165),1);graphics.DrawLine(&ground,left,top+220,left+240,top+220);
                graphics.DrawImage(canvas.image.get(),Rect(left,top+20,240,240),200,200,240,240,UnitPixel);
                Font font(L"Segoe UI",10,FontStyleRegular,UnitPixel);SolidBrush ink(Color(255,30,43,57));
                const std::wstring label=L"clip"+std::to_wstring(static_cast<unsigned>(layout.clip))+L" / frame"+
                    std::to_wstring(frame)+L" of"+std::to_wstring(layout.frameCount)+L" / native110ref";
                graphics.DrawString(label.c_str(),-1,&font,PointF(static_cast<REAL>(left+4),static_cast<REAL>(top+3)),&ink);
                ++reviewCell;
            }
        }
        // Independently change nutrition dimensions and DPI/age scale.
        // Thresholding alpha and bicubic resampling do not commute: scaling
        // bbox(source alpha>=128) cannot predict bbox(filtered alpha>=128).
        // Compare the complete independently decoded target raster instead,
        // retaining strict per-pixel coverage, canvas and support checks.
        const unsigned frame=layout.frameCount/2;
        std::vector<std::uint32_t> reference;
        if(!decodeReference(*original.image,layout,frame,reference))return 300;
        for(const auto dimensions:{std::pair<double,double>{1,1},{.78,.96},{1.26,.98}})
        for(int facing:{-1,1})for(REAL desiredHeight:{80.0f,165.0f}) {
            auto body=normal;body.bodyWidth=dimensions.first;body.bodyHeight=dimensions.second;
            cat::animation::Pose pose;pose.spriteTimed=false;pose.spriteClip=layout.clip;pose.spriteFrame=frame;
            pose.flip=facing<0;pose.groundContact=layout.clip!=Clip::Jump;
            context="actual care shape clip="+std::to_string(static_cast<unsigned>(layout.clip))+
                " facing="+std::to_string(facing)+" width="+std::to_string(dimensions.first)+
                " height="+std::to_string(desiredHeight);
            if(!draw(pose,character,body,desiredHeight,true))return 299;
            const auto direct=copy();
            if(!draw(pose,character,body,desiredHeight,false)||copy()!=direct)return 302;
            std::array<int,4> actual{};
            if(!inspect(direct,actual))return 303;
            if(!drawReference(layout,frame,reference,body,desiredHeight,pose.flip))return 300;
            const auto expected=copy();
            if(!nearPixels(expected,direct)) {
                unsigned maxRgb=0,maxAlpha=0;std::size_t firstBad=expected.size(),changed=0;
                for(std::size_t i=0;i<expected.size();++i) {
                    const auto delta=static_cast<unsigned>(std::abs(static_cast<int>(expected[i])-direct[i]));
                    if(i%4==3)maxAlpha=std::max(maxAlpha,delta);else maxRgb=std::max(maxRgb,delta);
                    if(delta>(i%4==3?1u:2u)){if(firstBad==expected.size())firstBad=i;++changed;}
                }
                context+=" unexpected anchored raster maxRgb="+std::to_string(maxRgb)+
                    " maxAlpha="+std::to_string(maxAlpha)+" channels="+std::to_string(changed);
                if(firstBad<expected.size())context+=" first=("+std::to_string(firstBad/4%width)+","+
                    std::to_string(firstBad/4/width)+",channel"+std::to_string(firstBad%4)+") expected="+
                    std::to_string(expected[firstBad])+" actual="+std::to_string(direct[firstBad]);
                return 305;
            }
            if(pose.groundContact&&actual[3]>bottom+cat::render::SpriteBottomSafetyInset(layout,desiredHeight,body.bodyHeight)+2)return 304;
        }
        if(layout.clip!=Clip::Jump) {
            cat::animation::Pose airborne;airborne.spriteClip=layout.clip;airborne.groundContact=false;
            if(CareSpriteFor(airborne,character))return 306;
        }

    }
    // Include the very same masked cardboard asset and front strip used by
    // the desktop toy renderer, rather than an independently redrawn box.
    for(const bool foreground:{false,true}) {
        bool drawn=false;
        {
            struct RestoreBoxContext {
                ToyKind toy=g.toy;bool small=g.smallBoxEvent;cat::Character character=g.preferences.character;
                ~RestoreBoxContext(){g.toy=toy;g.smallBoxEvent=small;g.preferences.character=character;}
            } restore;
            g.toy=ToyKind::Box;g.smallBoxEvent=false;g.preferences.character=cat::Character::Cream;
            Graphics graphics(canvas.image.get());configure(graphics);
            drawn=DrawAuthoredBoxProp(graphics,center,bottom,foreground);graphics.Flush(FlushIntentionSync);
        }
        if(!drawn)return 310;
        Graphics graphics(review.image.get());graphics.SetInterpolationMode(InterpolationModeNearestNeighbor);
        graphics.SetPixelOffsetMode(PixelOffsetModeHalf);
        const int left=static_cast<int>(reviewCell%8)*240,top=static_cast<int>(reviewCell/8)*260;
        Pen ground(Color(255,145,154,165),1);graphics.DrawLine(&ground,left,top+220,left+240,top+220);
        graphics.DrawImage(canvas.image.get(),Rect(left,top+20,240,240),200,200,240,240,UnitPixel);
        Font font(L"Segoe UI",10,FontStyleRegular,UnitPixel);SolidBrush ink(Color(255,30,43,57));
        graphics.DrawString(foreground?L"Runtime box prop / front strip":L"Runtime box prop / background",-1,&font,
            PointF(static_cast<REAL>(left+4),static_cast<REAL>(top+3)),&ink);++reviewCell;
    }
    CLSID png{};
    if(!NativeCarePngEncoder(png)||review.image->Save(L"render-self-test/care-sprites-gdi.png",&png,nullptr)!=Ok)return 310;
    context="actual care sheet declared species ownership";
    for(const auto clip:cat::sprite_sequence::kAuthoredClips)for(unsigned character=0;character<cat::kCharacterCount;++character) {
        const CareSpriteAtlas* expected=nullptr;
        for(const auto& asset:g.careSprites)if(asset->layout->clip==clip&&asset->layout->character==character)expected=asset.get();
        cat::animation::Pose pose;pose.spriteTimed=false;pose.spriteClip=clip;
        if(CareSpriteFor(pose,static_cast<cat::Character>(character))!=expected)return 307;
    }
    cat::animation::Pose pose;pose.spriteTimed=false;pose.spriteClip=Clip::Eat;pose.fishMeal=true;
    const auto* fish=CareSpriteFor(pose,cat::Character::Cream);
    if(fish) {
        if(fish->layout->clip!=Clip::Fish||!draw(pose,cat::Character::Cream,normal,110,false))return 308;
        const auto fishPixels=copy();pose.fishMeal=false;pose.spriteClip=Clip::Fish;
        if(!draw(pose,cat::Character::Cream,normal,110,false)||copy()!=fishPixels)return 308;
    }
    if(cat::SerializePreferences(g.preferences)!=preferences||cat::nutrition::Serialize(g.nutrition)!=nutrition)return 309;
    context="all actual authored care resources, source ROIs, anchors, shapes and dispatcher checks passed";
    return 0;
}
