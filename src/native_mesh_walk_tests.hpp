#pragma once

// The authored mesh is baked offline. This review draws those original PNG
// frames through the ordinary GDI+ DrawCareSprite/cache path, not a second rig
// renderer and not a desktop screen recording.
int NativeMeshWalkReview(std::string& context,bool required=false) {
#if !YEOPJARI_HAS_MESH_WALK_REVIEW
    context="No authored mesh Walk review has been registered";
    return required?490:0;
#else
    (void)required;
    context="native mesh Walk registration and resource decode";
    using Clip=cat::sprite_sequence::Clip;
    const auto& candidate=cat::mesh_walk::kDescriptor;
    if(!cat::sprite_assets::Valid(candidate)||cat::mesh_walk::kSamples.size()!=candidate.frameCount)return 490;
    const auto* registered=LoadedCareSprite(Clip::Walk,cat::Character::Cream);
    if(!registered||registered->layout==&candidate)return 491;
    std::size_t slot=0;while(slot<g.careSprites.size()&&g.careSprites[slot].get()!=registered)++slot;
    if(slot==g.careSprites.size())return 491;
    const auto originalStats=careSpriteMemory;
    auto baseline=std::move(g.careSprites[slot]);
    struct Restore {
        std::size_t slot;std::unique_ptr<CareSpriteAtlas>& baseline;CareSpriteMemoryStats stats;
        ~Restore(){g.careSprites[slot]=std::move(baseline);careSpriteMemory=stats;g.companionModel.SetWalkRegistration(nullptr);}
    } restore{slot,baseline,originalStats};
    // Keep both old and new immutable pixels only in this diagnostic. Count
    // the temporary coexistence as well as both coats against the same budget.
    auto review=LoadReviewCareSprite(candidate,cat::mesh_walk::kResource,
        originalStats.residentPixels,originalStats.tuxedoPixels);
    if(!review)return 492;
    for(unsigned frame=0;frame<candidate.frameCount;++frame) {
        context="native mesh Walk source alpha/sole coverage frame="+std::to_string(frame);
        const auto& r=candidate.frames[frame].rect;const auto& a=review->framePixels[frame];const auto& b=review->tuxedoPixels[frame];
        if(a.size()!=b.size())return 492;
        for(int y=0;y<r.height;++y)for(int x=0;x<r.width;++x) {
            const unsigned alpha=a[static_cast<std::size_t>(y)*r.width+x]>>24;
            if(alpha!=(b[static_cast<std::size_t>(y)*r.width+x]>>24))return 492;
            if((x<2||x>=r.width-2||y<2||y>=r.height-2)&&alpha>64)return 492;
        }
        const auto& sample=cat::mesh_walk::kSamples[frame];
        for(const auto& foot:sample.feet)if(foot.planted) {
            unsigned coverage=0;const int px=static_cast<int>(std::lround(foot.x)),py=static_cast<int>(std::lround(foot.y));
            for(int y=std::max(0,py-3);y<=std::min(r.height-1,py+3);++y)
                for(int x=std::max(0,px-3);x<=std::min(r.width-1,px+3);++x)
                    coverage=std::max(coverage,a[static_cast<std::size_t>(y)*r.width+x]>>24);
            if(coverage<=128)return 492;
        }
    }
    g.careSprites[slot]=std::move(review);
    const auto& old=*baseline->layout;
    CreateDirectoryW(L"render-self-test",nullptr);
    CreateDirectoryW(L"render-self-test/mesh-walk",nullptr);
    std::string trace="scene,tick,lane,travel,phase,frame,foot,planted,source_x,source_y,world_x,world_y\n";
    const cat::nutrition::Visual body{}; // Exact common adult reference, independent of earlier fixture nutrition.
    constexpr double speed=22,dt=1./60.;
    const std::array<const wchar_t*,4> names{{L"NF",L"FF",L"NR",L"FR"}};
    const std::array<Color,4> colors{{Color(255,230,120,15),Color(255,35,115,215),Color(255,180,50,65),Color(255,20,145,130)}};
    double maximumPlantDrift=0,maximumPlantGroundGap=0;
    constexpr std::size_t minimumActorPixels=1000;
    std::array<std::size_t,4> minimumObservedActorPixels;
    minimumObservedActorPixels.fill(std::numeric_limits<std::size_t>::max());
    std::size_t actorPresenceSamples=0;
    for(unsigned scene=0;scene<2;++scene) {
        const bool enlarged=scene==1;
        const int width=enlarged?1100:900,height=enlarged?760:520;
        Surface canvas,from,to;if(!canvas.Create(width,height))return 493;
        cat::physics::Controller physical;physical.Reset(160,20,800);physical.SetTargetVelocity(speed);
        for(int warm=0;warm<180;++warm)physical.Update(dt);
        if(std::abs(physical.CurrentPose().velocityX-speed)>.01)return 494;
        const double start=physical.RenderPose().relativeTravelX;
        const double duration=(enlarged?candidate.stridePixels:3*std::max(old.stridePixels,candidate.stridePixels))/speed;
        const unsigned ticks=static_cast<unsigned>(std::ceil(duration*60))+1;
        cat::animation::Locomotion oldGait,newGait;
        oldGait.ConfigureAuthoredStrides(old.stridePixels,150,old.standingFrame);
        newGait.ConfigureAuthoredStrides(candidate.stridePixels,150,candidate.standingFrame);
        double travel=0;
        std::array<bool,4> priorPlanted{};
        std::array<double,4> plantedWorldX{};
        for(unsigned tick=0;tick<ticks;++tick) {
            context="native mesh Walk scene="+std::to_string(scene)+" tick="+std::to_string(tick);
            if(tick) {
                const auto previous=physical.RenderPose();physical.Update(dt);const auto now=physical.RenderPose();
                const double distance=now.relativeTravelX-previous.relativeTravelX;
                if(!oldGait.Advance(Motion::Walk,distance,dt,now.grounded)||
                        !newGait.Advance(Motion::Walk,distance,dt,now.grounded))return 495;
            }
            travel=physical.RenderPose().relativeTravelX-start;
            const double factor=enlarged?3.:1.;
            Graphics graphics(canvas.image.get());graphics.Clear(Color(255,235,239,243));
            graphics.SetSmoothingMode(SmoothingModeAntiAlias);graphics.SetInterpolationMode(InterpolationModeHighQualityBicubic);
            graphics.SetPixelOffsetMode(PixelOffsetModeHighQuality);
            Font font(L"Segoe UI",14,FontStyleRegular,UnitPixel);SolidBrush ink(Color(255,30,43,57));
            const std::wstring title=enlarged?L"UNAPPROVED mesh rig / native GDI+ / sole controls / 3x size / 3x slow":
                L"UNAPPROVED mesh rig / native GDI+ + Box2D / 22 px/s / 60 FPS / NOT desktop capture";
            graphics.DrawString(title.c_str(),-1,&font,PointF(12,8),&ink);
            const unsigned firstLane=enlarged?1:0;
            for(unsigned lane=firstLane;lane<2;++lane) {
                auto& gait=lane?newGait:oldGait;
                const auto& layout=lane?candidate:old;
                if(!lane)std::swap(g.careSprites[slot],baseline);
                struct RestoreLane {
                    std::size_t slot;std::unique_ptr<CareSpriteAtlas>& other;bool swapped;
                    ~RestoreLane(){if(swapped)std::swap(g.careSprites[slot],other);}
                } restoreLane{slot,baseline,!lane};
                auto pose=cat::animation::Evaluate(Motion::Walk,tick*1000/60,tick*1000/60,1,gait);
                cat::animation::ApplyAuthoredLocomotion(pose,Motion::Walk,gait,true);
                const unsigned frame=SpriteFrameFor(pose,layout);
                if(pose.spriteClip!=Clip::Walk||frame>=layout.frameCount)return 496;
                const REAL floor=enlarged?650.f:lane?490.f:245.f;
                const REAL center=static_cast<REAL>(enlarged?450+travel*factor:160+travel);
                Pen line(Color(255,150,162,175),1);graphics.DrawLine(&line,0.f,floor,static_cast<REAL>(width),floor);
                for(REAL x=0;x<width;x+=static_cast<REAL>(20*factor))graphics.DrawLine(&line,x,floor,x,floor+5);
                const std::wstring label=(lane?L"UNAPPROVED editable mesh bake":L"Approved 8 drawings")+std::wstring(L" / frame ")+
                    std::to_wstring(frame)+L" / travel "+std::to_wstring(travel);
                graphics.DrawString(label.c_str(),-1,&font,PointF(12,enlarged?40.f:lane?280.f:45.f),&ink);
                DrawStyledAnimationDirect(graphics,pose,center,floor,
                    static_cast<REAL>(110*factor/CurrentArt(cat::Character::Cream).base.standingHeight),
                    cat::Character::Cream,cat::Outfit::None,body,from,to,width,height);
                if(!enlarged)DrawStyledAnimationDirect(graphics,pose,center+420,floor,
                    static_cast<REAL>(110.0/CurrentArt(cat::Character::Tuxedo).base.standingHeight),
                    cat::Character::Tuxedo,cat::Outfit::None,body,from,to,width,height);
                if(lane) {
                    const auto& sample=cat::mesh_walk::kSamples[frame];
                    for(unsigned foot=0;foot<4;++foot) {
                        const auto& p=sample.feet[foot];const auto& anchor=layout.frames[frame].anchor;
                        const double sx=110*factor/layout.referenceHeight;
                        const double x=center+(p.x-anchor.x)*sx,y=floor+(p.y-anchor.y)*sx;
                        if(p.planted) {
                            const double groundGap=std::abs((y-floor)/factor);
                            maximumPlantGroundGap=std::max(maximumPlantGroundGap,groundGap);
                            if(groundGap>1.001)return 501;
                            if(!priorPlanted[foot])plantedWorldX[foot]=x/factor;
                            const double drift=std::abs(x/factor-plantedWorldX[foot]);
                            maximumPlantDrift=std::max(maximumPlantDrift,drift);
                            if(drift>1.001)return 500;
                        }
                        priorPlanted[foot]=p.planted;
                        if(enlarged) {
                            Pen marker(colors[foot],2);SolidBrush fill(colors[foot]);
                            if(p.planted)graphics.FillEllipse(&fill,static_cast<REAL>(x-4),static_cast<REAL>(y-4),8.f,8.f);
                            else graphics.DrawEllipse(&marker,static_cast<REAL>(x-4),static_cast<REAL>(y-4),8.f,8.f);
                            if(p.planted)graphics.DrawEllipse(&marker,static_cast<REAL>(plantedWorldX[foot]*factor-6),floor+7,12.f,12.f);
                            const std::wstring info=std::wstring(names[foot])+(p.planted?L" planted":L" swing")+L" / x "+std::to_wstring(x/factor);
                            graphics.DrawString(info.c_str(),-1,&font,PointF(12,75+foot*22.f),&fill);
                        }
                        trace+=std::to_string(scene)+","+std::to_string(tick)+",1,"+std::to_string(travel)+","+
                            std::to_string(gait.AuthoredPhase())+","+std::to_string(frame)+","+std::to_string(foot)+","+
                            std::to_string(p.planted)+","+std::to_string(p.x)+","+std::to_string(p.y)+","+
                            std::to_string(x/factor)+","+std::to_string(y/factor)+"\n";
                    }
                }
            }
            graphics.Flush(FlushIntentionSync);
            if(!enlarged)for(unsigned lane=0;lane<2;++lane)for(unsigned coat=0;coat<2;++coat) {
                // Inspect the actual final raster, not source alpha or a draw
                // call count. These actor ROIs omit the row title and ground.
                // A zero scale must fail even if every source/cache check passed.
                const double center=160+travel+(coat?420:0);
                const int floor=lane?490:245;
                const int left=std::max(0,static_cast<int>(std::floor(center-80)));
                const int right=std::min(width,static_cast<int>(std::ceil(center+80)));
                std::size_t colored=0;
                for(int y=floor-140;y<floor-8;++y)for(int x=left;x<right;++x) {
                    const auto* pixel=canvas.pixels+(static_cast<std::size_t>(y)*width+x)*4;
                    const int difference=std::abs(static_cast<int>(pixel[0])-243)+
                        std::abs(static_cast<int>(pixel[1])-239)+std::abs(static_cast<int>(pixel[2])-235);
                    if(pixel[3]>128&&difference>24)++colored;
                }
                const unsigned actor=lane*2+coat;
                minimumObservedActorPixels[actor]=std::min(minimumObservedActorPixels[actor],colored);
                ++actorPresenceSamples;
                if(colored<minimumActorPixels) {
                    context="native mesh Walk actor missing: tick="+std::to_string(tick)+
                        " lane="+std::to_string(lane)+" coat="+(coat?"Tuxedo":"Cream")+
                        " colored_pixels="+std::to_string(colored);
                    return 502;
                }
            }
            const auto path=L"render-self-test/mesh-walk/"+std::wstring(enlarged?L"contact-":L"compare-")+
                std::to_wstring(10000+tick)+L".png";
            if(!SaveNativeWalkingReview(*canvas.image,path.c_str()))return 497;
        }
        if(!enlarged && travel+1e-4<3*std::max(old.stridePixels,candidate.stridePixels))return 498;
    }
    if(!WriteBytes(L"render-self-test/mesh-walk/contacts.csv",trace))return 499;
    const auto description=std::string("{\"render\":\"actual GDI+ DrawCareSprite and Box2D, not desktop capture\",\"visual_approved\":false,\"production_approved\":false,\"art_status\":\"UNAPPROVED mesh rig review\",\"display_height_logical\":110,\"speed_logical\":22,\"physics_hz\":60,\"comparison_output_fps\":60,\"contact_input_hz\":60,\"contact_slow_output_fps\":20,\"contact_scale\":3,\"old_stride\":")+
        std::to_string(old.stridePixels)+",\"new_stride\":"+std::to_string(candidate.stridePixels)+
        ",\"new_frames\":"+std::to_string(candidate.frameCount)+",\"new_loop_drawings\":"+
        std::to_string(cat::mesh_walk::kLoopFrameCount)+",\"standing_hold_frame\":"+
        std::to_string(candidate.standingFrame)+",\"plant_drift_measurement\":\"rig sole annotation plus actual displayed Box2D root, not raster optical tracking\",\"maximum_displayed_control_plant_drift_logical_px\":"+
        std::to_string(maximumPlantDrift)+",\"maximum_planted_ground_gap_logical_px\":"+
        std::to_string(maximumPlantGroundGap)+",\"comparison_coats_checked\":2,\"actor_presence_threshold_pixels\":"+
        std::to_string(minimumActorPixels)+",\"actor_presence_samples\":"+std::to_string(actorPresenceSamples)+
        ",\"minimum_actor_colored_pixels\":{\"baseline_cream\":"+std::to_string(minimumObservedActorPixels[0])+
        ",\"baseline_tuxedo\":"+std::to_string(minimumObservedActorPixels[1])+
        ",\"review_cream\":"+std::to_string(minimumObservedActorPixels[2])+
        ",\"review_tuxedo\":"+std::to_string(minimumObservedActorPixels[3])+"}}\n";
    if(!WriteBytes(L"render-self-test/mesh-walk/review.json",description))return 499;
    return 0;
#endif
}
