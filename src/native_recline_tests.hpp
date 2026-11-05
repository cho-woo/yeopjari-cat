#pragma once

// Actual Windows resource/GDI+ coverage. Model proofs cannot detect a host
// silently falling back to the old unregistered, double-face dissolve.
int NativeReclineRenderTest(std::string& context) {
    constexpr int width=420,height=320;
    Surface canvas,from,to,rawFirst,rawSecond;
    for(auto* surface:{&canvas,&from,&to,&rawFirst,&rawSecond})
        if(!surface->Create(width,height))return 160;
    const auto preferences=cat::SerializePreferences(g.preferences);
    const auto nutrition=cat::nutrition::Serialize(g.nutrition);
    const auto configure=[](Graphics& graphics) {
        graphics.SetCompositingMode(CompositingModeSourceCopy);graphics.Clear(Color(0,0,0,0));
        graphics.SetCompositingMode(CompositingModeSourceOver);
        graphics.SetInterpolationMode(InterpolationModeHighQualityBicubic);
        graphics.SetSmoothingMode(SmoothingModeAntiAlias);graphics.SetPixelOffsetMode(PixelOffsetModeHighQuality);
    };
    const auto copy=[](const Surface& surface) {
        return std::vector<BYTE>(surface.pixels,surface.pixels+width*height*4);
    };
    const auto inspect=[](const Surface& surface) {
        std::size_t visible=0;
        for(int y=0;y<height;++y)for(int x=0;x<width;++x) {
            const auto* pixel=surface.pixels+(y*width+x)*4;
            if(pixel[0]>pixel[3]+1||pixel[1]>pixel[3]+1||pixel[2]>pixel[3]+1)return false;
            if(pixel[3]>=32)++visible;
            if((x==0||x==width-1||y==0||y==height-1)&&pixel[3]>=32)return false;
            if(y>282&&pixel[3]>=128)return false;
        }
        return visible>=500;
    };
    const std::array<std::pair<int,int>,4> turns{{{4,24},{24,25},{25,24},{24,4}}};
    for(int c=0;c<cat::kCharacterCount;++c)for(int facing:{-1,1})for(double condition:{0.,50.,100.}) {
        const auto character=static_cast<cat::Character>(c);
        auto state=g.nutrition;state.bodyCondition=condition;
        const auto body=cat::nutrition::VisualModifiers(state);
        const REAL scale=110.0f/CurrentArt(character).base.standingHeight;
        const auto draw=[&](const cat::animation::Pose& pose) {
            Graphics graphics(canvas.image.get());configure(graphics);
            DrawStyledAnimation(graphics,pose,210,280,scale,character,cat::Outfit::Crown,body,
                from,to,width,height);
            graphics.Flush(FlushIntentionSync);
            return graphics.GetLastStatus()==Ok;
        };
        const auto raw=[&](Surface& target,int frame,const cat::animation::Pose& pose) {
            Graphics graphics(target.image.get());configure(graphics);
            DrawPet(graphics,frame,210,280,scale,pose.flip,static_cast<REAL>(body.bodyWidth),
                static_cast<REAL>(body.bodyHeight),0,false,pose.joints,true,false,character,cat::Outfit::None,pose.face);
            graphics.Flush(FlushIntentionSync);
            return graphics.GetLastStatus()==Ok;
        };
        for(const auto& turn:turns)for(int sample=0;sample<=4;++sample) {
            cat::animation::Pose pose;pose.frame=turn.first;pose.blendFrame=turn.second;
            pose.blend=sample/4.;pose.flip=facing<0;
            context="recline c="+std::to_string(c)+" facing="+std::to_string(facing)+
                " body="+std::to_string(condition)+" turn="+std::to_string(turn.first)+"/"+
                std::to_string(turn.second)+" sample="+std::to_string(sample);
            if(sample>0&&sample<4 && !DrawRegisteredReclineLayers(pose,210,280,scale,character,body,from,to,width,height))return 161;
            if(!draw(pose)||!inspect(canvas))return 162;
            const auto pixels=copy(canvas);
            if(sample==0||sample==4) {
                // Settled art retains its own supported neck/head. No canonical
                // sitting head, mask fragments or residual morph survives.
                if(!raw(rawFirst,sample==0?turn.first:turn.second,pose)||copy(rawFirst)!=pixels)return 163;
            } else if(sample==2) {
                if(!raw(rawFirst,turn.first,pose)||!raw(rawSecond,turn.second,pose))return 164;
                std::size_t changed=0;
                for(std::size_t i=0;i<pixels.size();++i) {
                    const unsigned naive=(rawFirst.pixels[i]*127u+rawSecond.pixels[i]*128u+127u)/255u;
                    changed+=std::abs(static_cast<int>(pixels[i])-static_cast<int>(naive))>3;
                }
                if(changed<40)return 165; // Runtime must use registration, not silently dissolve old heads.
            }
        }
        for(int frame:{24,25}) {
            cat::animation::Pose pose;pose.frame=frame;pose.flip=facing<0;
            context="recline eyelids c="+std::to_string(c)+" facing="+std::to_string(facing)+
                " body="+std::to_string(condition)+" frame="+std::to_string(frame);
            if(!draw(pose))return 166;
            const auto awake=copy(canvas);pose.face.blink=1;
            if(!draw(pose)||!inspect(canvas))return 167;
            const auto asleep=copy(canvas);std::size_t changed=0;
            for(int y=0;y<height;++y)for(int x=0;x<width;++x) {
                const auto i=static_cast<std::size_t>(y*width+x)*4;
                if(awake[i+3]!=asleep[i+3])return 168; // Eyelids cannot change the silhouette.
                const bool different=std::memcmp(awake.data()+i,asleep.data()+i,4)!=0;
                const bool behindHead=facing>0?x<195:x>225;
                if(behindHead&&different)return 169; // Body and tail stay untouched.
                changed+=different;
            }
            if(changed<5)return 170;
        }
    }
    if(cat::SerializePreferences(g.preferences)!=preferences||cat::nutrition::Serialize(g.nutrition)!=nutrition)return 171;
    return 0;
}
