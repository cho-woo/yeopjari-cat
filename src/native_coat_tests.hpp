#pragma once

namespace native_coat_tests {
struct Resource {
    IStream* stream=nullptr;
    std::unique_ptr<Bitmap> image;
    ~Resource(){image.reset();if(stream)stream->Release();}
    bool Open(int id,const char* expectedHash) {
        const auto resource=FindResourceW(g.instance,MAKEINTRESOURCEW(id),RT_RCDATA);
        if(!resource)return false;
        const auto loaded=LoadResource(g.instance,resource);
        const auto count=SizeofResource(g.instance,resource);
        const auto* bytes=static_cast<const BYTE*>(LockResource(loaded));
        if(!bytes||!count||count>cat::sprite_assets::kMaxImageBytes||!expectedHash)return false;
        BCRYPT_ALG_HANDLE algorithm=nullptr;BCRYPT_HASH_HANDLE hash=nullptr;
        if(BCryptOpenAlgorithmProvider(&algorithm,BCRYPT_SHA256_ALGORITHM,nullptr,0)<0)return false;
        DWORD objectSize=0,resultSize=0;
        bool ok=BCryptGetProperty(algorithm,BCRYPT_OBJECT_LENGTH,reinterpret_cast<PUCHAR>(&objectSize),
            sizeof(objectSize),&resultSize,0)>=0&&objectSize<1048576;
        std::vector<BYTE> object(ok?objectSize:0);std::array<BYTE,32> digest{};
        if(ok)ok=BCryptCreateHash(algorithm,&hash,object.data(),objectSize,nullptr,0,0)>=0;
        if(ok)ok=BCryptHashData(hash,const_cast<PUCHAR>(bytes),count,0)>=0&&
            BCryptFinishHash(hash,digest.data(),static_cast<ULONG>(digest.size()),0)>=0;
        if(hash)BCryptDestroyHash(hash);
        BCryptCloseAlgorithmProvider(algorithm,0);
        constexpr char hex[]="0123456789abcdef";std::string actual;
        for(const auto byte:digest){actual+=hex[byte>>4];actual+=hex[byte&15];}
        if(!ok||actual!=expectedHash)return false;
        const auto memory=GlobalAlloc(GMEM_MOVEABLE,count);if(!memory)return false;
        const auto target=GlobalLock(memory);if(!target){GlobalFree(memory);return false;}
        std::memcpy(target,bytes,count);GlobalUnlock(memory);
        if(FAILED(CreateStreamOnHGlobal(memory,TRUE,&stream))){GlobalFree(memory);return false;}
        image.reset(Bitmap::FromStream(stream,FALSE));
        return image&&image->GetLastStatus()==Ok;
    }
};

bool ValidPixels(const std::uint32_t* pixels,int width,int height,bool padded=false) {
    if(!pixels||width<=0||height<=0)return false;
    int left=width,top=height,right=-1,bottom=-1;std::size_t solid=0;
    for(int y=0;y<height;++y)for(int x=0;x<width;++x) {
        const auto pixel=pixels[static_cast<std::size_t>(y)*width+x];
        const unsigned alpha=pixel>>24;
        if(((pixel>>16)&255)>alpha||((pixel>>8)&255)>alpha||(pixel&255)>alpha)return false;
        if(alpha>16){++solid;left=std::min(left,x);top=std::min(top,y);right=std::max(right,x);bottom=std::max(bottom,y);}
    }
    return solid>=32&&left<=right&&top<=bottom&&
        (!padded||(left>0&&top>0&&right<width-1&&bottom<height-1));
}
std::uint64_t PixelsHash(const std::vector<std::uint32_t>& pixels) {
    return cat::Checksum(std::string(reinterpret_cast<const char*>(pixels.data()),pixels.size()*4));
}
} // namespace native_coat_tests

// Real GDI+ checks for independent authored coats. Shared registration and
// physical support do not require the same pixels or silhouette.
int NativeCoatRenderTest(std::string& context) {
    using C=cat::Character;
    Surface cream,tuxedo,from,to,review;
    if(!cream.Create(420,340)||!tuxedo.Create(420,340)||!from.Create(420,340)||
            !to.Create(420,340)||!review.Create(1200,2400))return 440;
    {Graphics graphics(review.image.get());graphics.Clear(Color(255,235,239,243));}
    const auto body=cat::appearance::VisualModifiers(g.nutrition,g.preferences.character);
    std::size_t coatPixels=0;unsigned row=0;
    const auto drawVisual=[&](Surface& surface,C character,const cat::animation::Pose& pose,
                              const cat::nutrition::Visual& visual) {
        Graphics graphics(surface.image.get());graphics.Clear(Color(0,0,0,0));
        graphics.SetSmoothingMode(SmoothingModeAntiAlias);
        graphics.SetInterpolationMode(InterpolationModeHighQualityBicubic);
        graphics.SetPixelOffsetMode(PixelOffsetModeHighQuality);
        DrawStyledAnimationDirect(graphics,pose,210,300,110.0f/CurrentArt(character).base.standingHeight,
            character,cat::Outfit::None,visual,from,to,420,340);
        graphics.Flush(FlushIntentionSync);return graphics.GetLastStatus()==Ok;
    };
    const auto draw=[&](Surface& surface,C character,const cat::animation::Pose& pose) {
        return drawVisual(surface,character,pose,body);
    };
    const auto fallback=[&](const SpriteAtlas& atlas,int base) {
        const auto* style=cat::styled_assets::Find(8,base,cat::styled_assets::kStyledAtlases);
        native_coat_tests::Resource source;
        return style&&atlas.baseResourceId==base&&atlas.resourceId==style->resourceId&&
            source.Open(style->resourceId,style->sha256)&&
            source.image->GetWidth()==style->imageWidth&&source.image->GetHeight()==style->imageHeight;
    };
    context="authored tuxedo fallback resource selection and SHA256";
    if(!fallback(CurrentArt(C::Tuxedo).base,101)||!fallback(CurrentArt(C::Tuxedo).actions,102)||
            !fallback(g.tuxedoJumpTreat,105)||!fallback(g.tuxedoGestures,106)||
            !fallback(g.tuxedoRecline,107)||!fallback(g.tuxedoHanging,110)||
            !fallback(g.tuxedoSleepStyles,112))return 441;
    for(const auto& asset:g.careSprites) {
        const auto& d=*asset->layout;
        context="independent native coats clip="+std::to_string(static_cast<int>(d.clip));
        if(!cat::sprite_assets::Valid(d)||LoadedCareSprite(d.clip,C::Cream)!=asset.get()||
                LoadedCareSprite(d.clip,C::Tuxedo)!=asset.get())return 441;
        if(d.clip==cat::sprite_sequence::Clip::Box) {
            if(asset->tuxedoStyle)return 441;
            for(unsigned frame=0;frame<d.frameCount;++frame)
                if(!asset->tuxedoPixels[frame].empty()||asset->tuxedoFrames[frame])return 442;
            continue; // Archived box art has no product event or new tuxedo asset.
        }
        const auto* style=cat::styled_assets::Find(8,d.resourceId,cat::styled_assets::kStyledAtlases);
        if(!style||asset->tuxedoStyle!=style||!cat::styled_assets::Fits(*style,d))return 441;
        native_coat_tests::Resource source;
        if(!source.Open(style->resourceId,style->sha256))return 441;
        CareSpriteAtlas expected;expected.layout=&d;std::size_t expectedPixels=0;
        if(!PrepareCareSpriteFrames(expected,*source.image,expectedPixels))return 442;
        for(unsigned frame=0;frame<d.frameCount;++frame) {
            context="independent native coats clip="+std::to_string(static_cast<int>(d.clip))+" frame="+std::to_string(frame);
            const auto& a=asset->framePixels[frame];const auto& b=asset->tuxedoPixels[frame];
            const auto& r=d.frames[frame].rect;
            const auto count=static_cast<std::size_t>(r.width)*r.height;
            if(a.size()!=count||b.size()!=count||!asset->frames[frame]||!asset->tuxedoFrames[frame]||
                    a.data()==b.data()||asset->frames[frame].get()==asset->tuxedoFrames[frame].get()||
                    asset->frames[frame]->GetWidth()!=static_cast<UINT>(r.width)||
                    asset->frames[frame]->GetHeight()!=static_cast<UINT>(r.height)||
                    asset->tuxedoFrames[frame]->GetWidth()!=static_cast<UINT>(r.width)||
                    asset->tuxedoFrames[frame]->GetHeight()!=static_cast<UINT>(r.height))return 442;
            if(!native_coat_tests::ValidPixels(a.data(),r.width,r.height)||
                    !native_coat_tests::ValidPixels(b.data(),r.width,r.height))return 443;
            // The independently decoded declared PNG must supply these cached
            // pixels; correct metadata alone cannot prove the loader used it.
            if(b!=expected.framePixels[frame])return 444;
            coatPixels+=b.size();
            const auto* creamBuffer=a.data();const auto* tuxedoBuffer=b.data();
            const auto* creamImage=asset->frames[frame].get();const auto* tuxedoImage=asset->tuxedoFrames[frame].get();
            const auto creamCapacity=a.capacity(),tuxedoCapacity=b.capacity();
            const auto creamHash=native_coat_tests::PixelsHash(a),tuxedoHash=native_coat_tests::PixelsHash(b);
            cat::animation::Pose pose;pose.spriteClip=d.clip;pose.spriteFrame=frame;
            cat::animation::UseAuthoredSprite(pose);
            if(CareSpriteFor(pose,C::Cream)!=asset.get()||CareSpriteFor(pose,C::Tuxedo)!=asset.get()||
                    SpriteFrameFor(pose,d)!=frame)return 445;
            for(bool flip:{false,true}) {
                pose.flip=flip;
                if(!draw(cream,C::Cream,pose)||!draw(tuxedo,C::Tuxedo,pose))return 445;
                if(!native_coat_tests::ValidPixels(reinterpret_cast<const std::uint32_t*>(cream.pixels),420,340,true)||
                        !native_coat_tests::ValidPixels(reinterpret_cast<const std::uint32_t*>(tuxedo.pixels),420,340,true))return 446;
                if(a.data()!=creamBuffer||b.data()!=tuxedoBuffer||a.capacity()!=creamCapacity||b.capacity()!=tuxedoCapacity||
                        asset->frames[frame].get()!=creamImage||asset->tuxedoFrames[frame].get()!=tuxedoImage||
                        native_coat_tests::PixelsHash(a)!=creamHash||native_coat_tests::PixelsHash(b)!=tuxedoHash)return 446;
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
    // Fallback silhouettes are independently authored as well; every pose must
    // remain nonempty, inside its canvas, and correctly premultiplied.
    for(int frame:{0,1,2,3,4,5,6,7,8,12,24,25,26,27,28}) {
        context="independent fallback coats frame="+std::to_string(frame);
        cat::animation::Pose pose;pose.frame=frame;
        if(!draw(cream,C::Cream,pose)||!draw(tuxedo,C::Tuxedo,pose))return 448;
        if(!native_coat_tests::ValidPixels(reinterpret_cast<const std::uint32_t*>(cream.pixels),420,340,true)||
                !native_coat_tests::ValidPixels(reinterpret_cast<const std::uint32_t*>(tuxedo.pixels),420,340,true))return 449;
    }
    // Same authored pose must produce identical real GDI+ pixels at every
    // weight. Nutrition still changes speed/jump; it cannot stretch the face.
    Surface proportions;
    if(!proportions.Create(1260,680))return 451;
    {Graphics graphics(proportions.image.get());graphics.Clear(Color(255,235,239,243));}
    for(unsigned coat=0;coat<2;++coat) {
        const auto character=coat?C::Tuxedo:C::Cream;
        std::vector<BYTE> expected;
        for(unsigned sample=0;sample<3;++sample) {
            auto state=g.nutrition;state.bodyCondition=sample*50.;
            const auto visual=cat::appearance::VisualModifiers(state,character);
            const auto health=cat::nutrition::VisualModifiers(state);
            context="reference proportions nutrition="+std::to_string(sample*50)+" coat="+std::to_string(coat);
            if(visual.bodyWidth!=1||visual.bodyHeight!=1||visual.speed!=health.speed||visual.jump!=health.jump)return 451;
            cat::animation::Pose pose;pose.frame=4;
            if(!drawVisual(cream,character,pose,visual))return 451;
            const std::size_t count=420*340*4;
            if(expected.empty())expected.assign(cream.pixels,cream.pixels+count);
            else if(!std::equal(expected.begin(),expected.end(),cream.pixels))return 451;
            Graphics graphics(proportions.image.get());
            graphics.DrawImage(cream.image.get(),Rect(sample*420,coat*340,420,340),0,0,420,340,UnitPixel);
            Font font(L"Segoe UI",13,FontStyleRegular,UnitPixel);SolidBrush ink(Color(255,35,45,55));
            const std::wstring label=(coat?L"Tuxedo":L"Cream")+std::wstring(L" / condition ")+std::to_wstring(sample*50);
            graphics.DrawString(label.c_str(),-1,&font,PointF(static_cast<REAL>(sample*420+8),static_cast<REAL>(coat*340+8)),&ink);
        }
    }
    if(!SaveNativeWalkingReview(*proportions.image,L"render-self-test/reference-proportions-gdi.png"))return 452;
    if(!SaveNativeWalkingReview(*review.image,L"render-self-test/two-cats-gdi.png"))return 450;
    return 0;
}
