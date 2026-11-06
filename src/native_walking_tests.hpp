#pragma once

// These files are generated only by the Windows render self-test. The release
// workflow exposes them for inspection; they are not application/package assets.
inline bool SaveNativeWalkingReview(Bitmap& image,const wchar_t* path) {
    UINT count=0,bytes=0;
    if(GetImageEncodersSize(&count,&bytes)!=Ok || !count || !bytes)return false;
    std::vector<BYTE> memory(bytes);
    auto* encoders=reinterpret_cast<ImageCodecInfo*>(memory.data());
    if(GetImageEncoders(count,bytes,encoders)!=Ok)return false;
    for(UINT index=0;index<count;++index)
        if(encoders[index].MimeType && std::wcscmp(encoders[index].MimeType,L"image/png")==0)
            return image.Save(path,&encoders[index].Clsid,nullptr)==Ok;
    return false;
}

int NativeWalkingRenderTest(std::string& context) {
    Surface canvas;
    if(!canvas.Create(420,320))return 151;
    const auto savedPreferences=cat::SerializePreferences(g.preferences);
    const auto savedNutrition=cat::nutrition::Serialize(g.nutrition);
    const auto normalBody=cat::nutrition::VisualModifiers(g.nutrition);
    const auto copy=[&] {return std::vector<BYTE>(canvas.pixels,canvas.pixels+420*320*4);};
    const auto draw=[&](cat::Character character,cat::Outfit outfit,const cat::animation::Pose& pose,
                        const cat::nutrition::Visual& body,bool direct,REAL bottom=280,REAL desiredHeight=110) {
        Graphics graphics(canvas.image.get());
        graphics.SetCompositingMode(CompositingModeSourceCopy);graphics.Clear(Color(0,0,0,0));
        graphics.SetCompositingMode(CompositingModeSourceOver);
        graphics.SetSmoothingMode(SmoothingModeAntiAlias);
        graphics.SetInterpolationMode(InterpolationModeHighQualityBicubic);
        graphics.SetPixelOffsetMode(PixelOffsetModeHighQuality);
        const REAL scale=desiredHeight/std::max(1,CurrentArt(character).base.standingHeight);
        bool success=true;
        if(direct)success=cat::species::UsesNewArt(character)?
            DrawWalkingPet(graphics,pose,210,bottom,scale,character,outfit,body):
            DrawOriginalWalkingPet(graphics,pose,210,bottom,scale,character,body);
        else DrawPetStyle(graphics,character,outfit,pose,body,210,bottom,desiredHeight);
        graphics.Flush(FlushIntentionSync);
        return success && graphics.GetLastStatus()==Ok;
    };
    const auto poseFor=[](Motion motion,int facing) {
        cat::animation::Locomotion gait;gait.Advance(motion,1,1.0/60);
        auto pose=cat::animation::Evaluate(motion,0,500000,facing,gait);
        const auto settings=cat::gait::ForSpeed(motion==Motion::Stalk,motion==Motion::Zoomies?165:22);
        pose.walkStride=settings.stride;pose.walkDuty=settings.duty;pose.walkLift=settings.lift;
        pose.walkFeetValid=false;
        return pose;
    };
    // Independently decode the complete original atlas rectangles. Never use
    // the production skin, mask, joints, body plate or its cached sourceImage
    // to form the reference. Matching every pixel detects a missing shoulder,
    // an internal transparent slit and a source crop that old reach tests miss.
    if(!CreateDirectoryW(L"render-self-test",nullptr)&&GetLastError()!=ERROR_ALREADY_EXISTS)return 350;
    for(const auto character:{cat::Character::Cream,cat::Character::SilverTabby}) {
        auto& atlas=CurrentArt(character).base;
        std::array<std::unique_ptr<Bitmap>,4> reference;
        std::array<int,4> supportInset{};
        std::array<std::uint64_t,4> sourceHashes{};
        for(int frame=0;frame<4;++frame) {
            reference[frame].reset(atlas.image->Clone(atlas.frames[frame],PixelFormat32bppPARGB));
            if(!reference[frame]||reference[frame]->GetLastStatus()!=Ok)return 351;
            Rect rectangle(0,0,reference[frame]->GetWidth(),reference[frame]->GetHeight());BitmapData pixels{};
            if(reference[frame]->LockBits(&rectangle,ImageLockModeRead,PixelFormat32bppPARGB,&pixels)!=Ok)return 351;
            int last=-1;
            for(int y=0;y<rectangle.Height;++y) {
                const auto* row=static_cast<const BYTE*>(pixels.Scan0)+static_cast<ptrdiff_t>(y)*pixels.Stride;
                for(int x=0;x<rectangle.Width;++x)if(row[x*4+3]>153)last=y;
            }
            reference[frame]->UnlockBits(&pixels);
            if(last<0)return 351;
            supportInset[frame]=rectangle.Height-1-last;
            const auto& source=atlas.skins[frame].source;
            sourceHashes[frame]=cat::Checksum(std::string(reinterpret_cast<const char*>(source.data()),source.size()*4));
        }
        Surface evidence;
        if(!evidence.Create(1760,1010))return 352;
        {Graphics graphics(evidence.image.get());graphics.Clear(Color(255,235,239,243));}
        unsigned captured[3][2]={{0,0},{0,0},{0,0}};
        for(int facing:{-1,1})
        for(const auto dimensions:{std::pair<double,double>{1,1},{.78,.96},{.82,1.04},{1.26,.98}})
        for(REAL desiredHeight:{80.0f,90.2f,110.0f,165.0f})
        for(const auto motion:{Motion::Walk,Motion::Stalk,Motion::Zoomies}) {
            auto body=normalBody;body.bodyWidth=dimensions.first;body.bodyHeight=dimensions.second;
            cat::animation::Locomotion gait;unsigned seen=0;
            const double speed=motion==Motion::Zoomies?165:motion==Motion::Stalk?12.5:22;
            for(int tick=0;tick<240&&seen!=15;++tick) {
                if(!gait.Advance(motion,speed/60,1./60))return 353;
                const auto pose=cat::animation::Evaluate(motion,tick*1000/60,500000,facing,gait);
                if(!pose.originalWalk||pose.jointWalk||pose.walkingRig||pose.frame<0||pose.frame>3||
                        pose.blendFrame!=-1||pose.blend!=0||pose.x!=0||pose.lift!=0||pose.angle!=0||
                        pose.scaleX!=1||pose.scaleY!=1||!cat::deform::Zero(pose.joints))return 353;
                const unsigned bit=1u<<pose.frame;if(seen&bit)continue;seen|=bit;
                context="whole original walking c="+std::to_string(static_cast<int>(character))+
                    " facing="+std::to_string(facing)+" frame="+std::to_string(pose.frame)+
                    " height="+std::to_string(desiredHeight)+" width="+std::to_string(body.bodyWidth);
                if(!draw(character,cat::Outfit::None,pose,body,true,280,desiredHeight))return 354;
                const auto actual=copy();
                if(!draw(character,cat::Outfit::None,pose,body,false,280,desiredHeight)||copy()!=actual)return 355;
                // A stale legacy rig flag must not outrank intact source art.
                if(dimensions.first==1&&desiredHeight==110&&motion==Motion::Walk) {
                    auto stale=pose;stale.jointWalk=stale.walkingRig=true;
                    stale.joints.frontLeft=.8;stale.joints.crouch=.7;
                    if(!draw(character,cat::Outfit::None,stale,body,false,280,desiredHeight)||copy()!=actual)return 355;
                }
                {
                    Graphics graphics(canvas.image.get());
                    graphics.SetCompositingMode(CompositingModeSourceCopy);graphics.Clear(Color(0,0,0,0));
                    graphics.SetCompositingMode(CompositingModeSourceOver);
                    graphics.SetSmoothingMode(SmoothingModeAntiAlias);
                    graphics.SetInterpolationMode(InterpolationModeHighQualityBicubic);
                    graphics.SetPixelOffsetMode(PixelOffsetModeHighQuality);
                    const REAL scale=desiredHeight/std::max(1,atlas.standingHeight);
                    const REAL width=reference[pose.frame]->GetWidth()*scale*static_cast<REAL>(body.bodyWidth);
                    const REAL height=reference[pose.frame]->GetHeight()*scale*static_cast<REAL>(body.bodyHeight);
                    graphics.TranslateTransform(210,280+supportInset[pose.frame]*scale*static_cast<REAL>(body.bodyHeight));
                    graphics.ScaleTransform(facing<0?-1.0f:1.0f,1);
                    ImageAttributes attributes;attributes.SetWrapMode(WrapModeClamp,Color(0,0,0,0));
                    if(graphics.DrawImage(reference[pose.frame].get(),RectF(-width/2,-height,width,height),
                            0,0,static_cast<REAL>(reference[pose.frame]->GetWidth()),
                            static_cast<REAL>(reference[pose.frame]->GetHeight()),UnitPixel,&attributes)!=Ok)return 356;
                    graphics.Flush(FlushIntentionSync);
                }
                const auto expected=copy();std::size_t visible=0;
                for(int y=0;y<320;++y)for(int x=0;x<420;++x) {
                    const auto offset=static_cast<std::size_t>(y*420+x)*4;
                    for(unsigned channel=0;channel<4;++channel)if(std::abs(static_cast<int>(actual[offset+channel])-
                            static_cast<int>(expected[offset+channel]))>2) {
                        context+=" source pixel loss/alteration x="+std::to_string(x)+" y="+std::to_string(y);return 357;
                    }
                    const auto* pixel=actual.data()+offset;
                    if(pixel[0]>pixel[3]+1||pixel[1]>pixel[3]+1||pixel[2]>pixel[3]+1)return 358;
                    if(pixel[3]>=64)++visible;
                    if(((x==0||x==419||y==0||y>=283)&&pixel[3]>=128))return 358;
                }
                if(visible<400)return 358;
                // Store actual native80/110/165px views, both directions,
                // then magnify only the already-rendered110px lower body.
                const int sizeRow=desiredHeight==80?0:desiredHeight==110?1:desiredHeight==165?2:-1;
                const unsigned side=facing<0?1:0;
                if(dimensions.first==1&&sizeRow>=0&&motion==Motion::Walk&&!(captured[sizeRow][side]&bit)) {
                    if(!draw(character,cat::Outfit::None,pose,body,false,280,desiredHeight))return 359;
                    Graphics graphics(evidence.image.get());graphics.SetInterpolationMode(InterpolationModeNearestNeighbor);
                    graphics.SetPixelOffsetMode(PixelOffsetModeHalf);
                    const int column=pose.frame*440,row=sizeRow*260;
                    Pen ground(Color(255,145,154,165),1);
                    graphics.DrawLine(&ground,column+side*220,row+220,column+(side+1)*220,row+220);
                    graphics.DrawImage(canvas.image.get(),Rect(column+side*220,row+20,220,220),100,80,220,220,UnitPixel);
                    Font font(L"Segoe UI",12,FontStyleRegular,UnitPixel);SolidBrush ink(Color(255,30,43,57));
                    const std::wstring label=L"Native ref"+std::to_wstring(static_cast<int>(desiredHeight))+L"px / frame"+
                        std::to_wstring(pose.frame)+(facing<0?L" / left":L" / right");
                    graphics.DrawString(label.c_str(),-1,&font,PointF(static_cast<REAL>(column+side*220+5),static_cast<REAL>(row+2)),&ink);
                    if(desiredHeight==110&&facing>0) {
                        graphics.DrawLine(&ground,column,965,column+440,965);
                        graphics.DrawImage(canvas.image.get(),Rect(column+10,800,420,195),140,225,140,65,UnitPixel);
                        graphics.DrawString(L"Native110px lower body / 3x nearest",-1,&font,
                            PointF(static_cast<REAL>(column+8),782),&ink);
                    }
                    captured[sizeRow][side]|=bit;
                }
            }
            if(seen!=15){context+=" did not visit all original poses";return 353;}
        }
        for(const auto& size:captured)if(size[0]!=15||size[1]!=15)return 359;
        const auto path=character==cat::Character::Cream?L"render-self-test/walk-cream-gdi.png":L"render-self-test/walk-silver-gdi.png";
        if(!SaveNativeWalkingReview(*evidence.image,path))return 359;
        for(int frame=0;frame<4;++frame) {
            const auto& source=atlas.skins[frame].source;
            if(cat::Checksum(std::string(reinterpret_cast<const char*>(source.data()),source.size()*4))!=sourceHashes[frame])return 359;
        }
    }
    // The later eight animals retain their existing inverse-warp fallback.
    // Their real source/head/nutrition regressions remain independent below.
    for(int c=2;c<cat::kCharacterCount;++c)for(int facing:{-1,1}) {
        const auto character=static_cast<cat::Character>(c);
        auto& art=CurrentArt(character);
        const auto& source=art.base.skins[cat::species::UsesNewArt(character)?2:1];
        const double reference=110.0/std::max(1,art.base.standingHeight);
        const double height=source.height*reference*normalBody.bodyHeight;
        // Stay above the limb roots and the bicubic sampling fringe. This
        // includes the entire face and upper body for every canonical source.
        const int headBottom=static_cast<int>(std::floor(280+2*height/source.height-.34*height));
        for(const auto motion:{Motion::Walk,Motion::Stalk,Motion::Zoomies}) {
            if((motion==Motion::Walk || motion==Motion::Zoomies) && !cat::species::UsesNewArt(character))continue;
            auto pose=poseFor(motion,facing);
            // These later animals retain the inverse-warp source path;
            // Cream/Silver whole-frame rendering was verified independently above.
            pose.jointWalk=false;
            if(!pose.originalWalk || pose.jointWalk || pose.walkingRig || pose.frame<0 || pose.frame>3 ||
                    pose.x!=0 || pose.lift!=0 || pose.angle!=0 || pose.scaleX!=1 || pose.scaleY!=1)return 152;
            std::array<std::vector<BYTE>,9> bare;
            for(int o=0;o<cat::kOutfitCount;++o) {
                // Persisted retired outfit IDs must render the same bare
                // character throughout each complete physical gait.
                if(motion!=Motion::Walk && o!=0 && o!=static_cast<int>(cat::Outfit::Hoodie))continue;
                const auto outfit=static_cast<cat::Outfit>(o);
                context="walking c="+std::to_string(c)+" facing="+std::to_string(facing)+
                    " motion="+std::to_string(static_cast<int>(motion))+" outfit="+std::to_string(o);
                const auto caseContext=context;
                std::vector<BYTE> planted;
                std::size_t changedLegs=0,plantedVisible=0;
                double startingCompression=0;
                for(unsigned phase=0;phase<=8;++phase) {
                    context=caseContext+" phase="+std::to_string(phase)+"/8";
                    pose.walkPhase=phase/8.0;
                    if(!draw(character,outfit,pose,normalBody,true) || !art.walking || !art.walking->rig.Ready())return 153;
                    const auto pixels=copy();
                    if(outfit==cat::Outfit::None)bare[phase]=pixels;
                    else if(pixels!=bare[phase]) {
                        context+=" retired outfit changed the bare character";return 159;
                    }
                    const auto& rig=art.walking->rig;
                    if(rig.SourceWidth()!=source.width || rig.Height()!=source.height ||
                            rig.Width()!=source.width+2*rig.PaddingX())return 154;
                    // Actual resource RGBA, not just abstract head landmarks.
                    for(int y=0;y<static_cast<int>(source.height*.68);++y)
                        if(std::memcmp(source.source.data()+y*source.width,rig.Pixels()+y*rig.Width()+rig.PaddingX(),
                                static_cast<std::size_t>(source.width)*4)!=0)return 154;
                    if(phase==0) {planted=pixels;startingCompression=rig.BodyOffset();}
                    std::size_t visible=0;
                    for(int y=0;y<320;++y)for(int x=0;x<420;++x) {
                        const auto p=static_cast<std::size_t>(y*420+x)*4;
                        const auto* pixel=pixels.data()+p;
                        if(pixel[0]>pixel[3]+1 || pixel[1]>pixel[3]+1 || pixel[2]>pixel[3]+1){context+=" invalid premultiplied alpha";return 155;}
                        if(pixel[3]>=64)++visible;
                        if((x==0||x==419||y==0||y==319) && pixel[3]>=32){context+=" canvas edge";return 155;}
                        if(outfit==cat::Outfit::None && y>282 && pixel[3]>=128){context+=" below support";return 155;}
                        const bool changed=std::memcmp(planted.data()+p,pixel,4)!=0;
                        if(y>=headBottom && changed)++changedLegs;
                    }
                    // Remove only the renderer's rigid weight shift, then
                    // compare the real head raster at the same screen origin.
                    // This permits compression without accepting face scaling,
                    // rotation, source changes or unrelated head deformation.
                    if(!draw(character,outfit,pose,normalBody,true,
                            static_cast<REAL>(280+startingCompression-rig.BodyOffset())))return 156;
                    const auto aligned=copy();
                    for(int y=0;y<headBottom;++y)for(int x=0;x<420;++x)for(int channel=0;channel<4;++channel) {
                        const auto p=static_cast<std::size_t>(y*420+x)*4+channel;
                        if(std::abs(static_cast<int>(aligned[p])-static_cast<int>(planted[p]))>2)return 156;
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
    for(const auto character:{cat::Character::Rabbit}) {
        auto& art=CurrentArt(character);
        const auto& source=art.base.skins[cat::species::UsesNewArt(character)?2:1];
        const double reference=110.0/std::max(1,art.base.standingHeight);
        auto pose=poseFor(cat::species::UsesNewArt(character)?Motion::Walk:Motion::Stalk,1);pose.walkPhase=.375;
        // This block exercises the retained Rabbit inverse-warp fallback;
        // original Cream/Silver source rendering has no mutable limb cache.
        pose.jointWalk=false;
        for(const auto dimensions:{std::pair<double,double>{1,1},{.82,1.04},{1.25,.96},{1,1}}) {
            auto body=normalBody;body.bodyWidth=dimensions.first;body.bodyHeight=dimensions.second;
            context="walking nutrition c="+std::to_string(static_cast<int>(character))+" width="+std::to_string(body.bodyWidth);
            if(!draw(character,cat::Outfit::Scarf,pose,body,true) || !art.walking || !art.walking->rig.Ready())return 159;
            cat::walk::Rig expected;
            if(!expected.Prepare(source.source.data(),source.width,source.height,source.width*4,cat::walk::ProfileFor(character)) ||
                    !expected.Raster(pose.walkPhase,pose.walkStride,source.width*reference*body.bodyWidth,
                        source.height*reference*body.bodyHeight,pose.walkDuty,pose.walkLift,
                        pose.walkFeetValid?&pose.walkFeet:nullptr) ||
                    expected.Width()!=art.walking->rig.Width() || expected.Height()!=art.walking->rig.Height() ||
                    std::memcmp(expected.Pixels(),art.walking->rig.Pixels(),static_cast<std::size_t>(expected.Width())*expected.Height()*4)!=0)return 159;
        }
    }
    if(cat::SerializePreferences(g.preferences)!=savedPreferences || cat::nutrition::Serialize(g.nutrition)!=savedNutrition)return 159;
    return 0;
}
