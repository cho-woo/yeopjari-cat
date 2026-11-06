#pragma once

// Tiny synthetic PNGs exercise capacity and ownership, not the visual quality
// or performance of real artwork. Ordinary-app old/new metrics are separate.
struct NativeCareUsage {
    double cpuSeconds=0;
    std::uint64_t privateBytes=0,workingSetBytes=0,peakWorkingSetBytes=0,peakCommitBytes=0;
};
inline bool ReadNativeCareUsage(NativeCareUsage& result) {
    struct Memory {
        DWORD size,faults;
        SIZE_T peakWorking,working,peakPaged,paged,peakNonPaged,nonPaged,pagefile,peakPagefile,privateUsage;
    } memory{};
    using ReadMemory=BOOL(WINAPI*)(HANDLE,Memory*,DWORD);
    static auto read=reinterpret_cast<ReadMemory>(GetProcAddress(GetModuleHandleW(L"kernel32.dll"),"K32GetProcessMemoryInfo"));
    if(!read)return false;
    memory.size=sizeof(memory);
    FILETIME created{},exited{},kernel{},user{};
    if(!read(GetCurrentProcess(),&memory,sizeof(memory))||!GetProcessTimes(GetCurrentProcess(),&created,&exited,&kernel,&user))return false;
    const auto value=[](FILETIME time){return(static_cast<std::uint64_t>(time.dwHighDateTime)<<32)|time.dwLowDateTime;};
    result.cpuSeconds=(value(kernel)+value(user))/10000000.;
    result.privateBytes=memory.privateUsage;result.workingSetBytes=memory.working;
    result.peakWorkingSetBytes=memory.peakWorking;result.peakCommitBytes=memory.peakPagefile;
    return true;
}
int NativeCareCacheCapacityTest(std::string& context) {
    using namespace cat::sprite_sequence;
    if(!CreateDirectoryW(L"render-self-test",nullptr)&&GetLastError()!=ERROR_ALREADY_EXISTS)return 370;
    CLSID png{};if(!NativeCarePngEncoder(png))return 370;
    DisabledCareSpriteScope restore;
    Surface canvas;if(!canvas.Create(180,140))return 370;
    auto body=cat::nutrition::VisualModifiers(g.nutrition);body.bodyWidth=body.bodyHeight=1;
    const auto savedStats=careSpriteMemory;
    std::ostringstream report;report.precision(9);
    report<<"{\"schema\":1,\"status\":\"passed\",\"test\":\"native_frame_cache_capacity\","
        "\"scope\":\"tiny synthetic PNG decode/cache/render regression; not real-app old/new performance\","
        "\"per_atlas_pixel_budget\":"<<cat::sprite_assets::kMaxImagePixels<<",\"resident_pixel_budget\":"
        <<cat::sprite_assets::kMaxResidentPixels<<",\"cases\":[";
    bool comma=false;
    for(unsigned count:{64u,128u,256u}) {
        context="native prewarmed PNG frame cache count="+std::to_string(count);
        auto layout=std::make_unique<cat::sprite_assets::Descriptor>();
        constexpr int cellWidth=24,cellHeight=32;
        layout->clip=Clip::Groom;layout->character=0;layout->resourceId=cat::sprite_assets::ResourceId(Clip::Groom,0);
        layout->columns=8;layout->rows=count/8;layout->frameCount=count;
        layout->imageWidth=8*cellWidth;layout->imageHeight=layout->rows*cellHeight;layout->referenceHeight=cellHeight;
        layout->program.stepCount=count;layout->program.loop={0,count};layout->program.finish=Finish::Hold;
        std::vector<std::uint32_t> source(layout->imageWidth*layout->imageHeight,0);
        const auto color=[](unsigned frame){return 0xff000000u|(((frame*73+41)&255)<<16)|(((frame*31+109)&255)<<8)|((frame*17+67)&255);};
        for(unsigned frame=0;frame<count;++frame) {
            const int x=static_cast<int>(frame%8)*cellWidth,y=static_cast<int>(frame/8)*cellHeight;
            layout->frames[frame]={{x,y,cellWidth,cellHeight},{12,30}};
            layout->program.steps[frame]={frame,37};
            for(int sy=4;sy<30;++sy)for(int sx=6;sx<20;++sx)
                source[static_cast<std::size_t>(y+sy)*layout->imageWidth+x+sx]=color(frame);
        }
        if(!cat::sprite_assets::Valid(*layout))return 371;
        Bitmap original(layout->imageWidth,layout->imageHeight,layout->imageWidth*4,PixelFormat32bppPARGB,reinterpret_cast<BYTE*>(source.data()));
        IStream* stream=nullptr;
        if(FAILED(CreateStreamOnHGlobal(nullptr,TRUE,&stream)))return 372;
        struct StreamRelease {IStream*& value;~StreamRelease(){if(value)value->Release();}} streamRelease{stream};
        if(original.Save(stream,&png,nullptr)!=Ok)return 372;
        LARGE_INTEGER zero{};if(FAILED(stream->Seek(zero,STREAM_SEEK_SET,nullptr)))return 372;
        NativeCareUsage decodeBefore{},decodeAfter{};
        if(!ReadNativeCareUsage(decodeBefore))return 373;
        const auto decodeStart=GetTickCount64();
        auto decoded=std::unique_ptr<Bitmap>(Bitmap::FromStream(stream,FALSE));
        if(!decoded||decoded->GetLastStatus()!=Ok)return 372;
        auto sprite=std::make_unique<CareSpriteAtlas>();sprite->layout=layout.get();std::size_t resident=0;
        if(!PrepareCareSpriteFrames(*sprite,*decoded,resident)||resident!=static_cast<std::size_t>(count)*cellWidth*cellHeight)return 374;
        decoded.reset();stream->Release();stream=nullptr; // No decoder survives playback.
        if(!ReadNativeCareUsage(decodeAfter))return 373;
        const auto decodeMs=GetTickCount64()-decodeStart;
        std::array<const Bitmap*,cat::sprite_assets::kMaxFrames> images{};
        std::array<const std::uint32_t*,cat::sprite_assets::kMaxFrames> buffers{};
        std::array<std::size_t,cat::sprite_assets::kMaxFrames> capacities{};
        std::array<std::uint64_t,cat::sprite_assets::kMaxFrames> hashes{};
        for(unsigned frame=0;frame<count;++frame) {
            images[frame]=sprite->frames[frame].get();buffers[frame]=sprite->framePixels[frame].data();capacities[frame]=sprite->framePixels[frame].capacity();
            hashes[frame]=cat::Checksum(std::string(reinterpret_cast<const char*>(buffers[frame]),cellWidth*cellHeight*4));
        }
        // A failed budget reservation must not replace even the first frame,
        // increment the caller's reservation or partially clear a valid cache.
        auto overBudget=static_cast<std::size_t>(cat::sprite_assets::kMaxResidentPixels)-resident+1;
        const auto overBudgetBefore=overBudget;
        if(PrepareCareSpriteFrames(*sprite,original,overBudget)||overBudget!=overBudgetBefore)return 375;
        auto invalid=std::make_unique<cat::sprite_assets::Descriptor>(*layout);invalid->frameCount=257;
        sprite->layout=invalid.get();auto unchanged=resident;
        if(PrepareCareSpriteFrames(*sprite,original,unchanged)||unchanged!=resident)return 375;
        sprite->layout=layout.get();
        g.careSprites.push_back(std::move(sprite));
        auto* cached=g.careSprites.back().get();
        NativeCareUsage playbackBefore{},playbackAfter{};if(!ReadNativeCareUsage(playbackBefore))return 373;
        const auto playbackStart=GetTickCount64();
        constexpr unsigned repeats=3;
        for(unsigned loop=0;loop<repeats;++loop)for(unsigned frame=0;frame<count;++frame) {
            cat::animation::Pose pose;pose.spriteClip=Clip::Groom;pose.spriteTimed=true;
            pose.spriteElapsedMs=(loop*count+frame)*37+18;pose.spriteOptions.stage=Stage::Looping;
            if(SpriteFrameFor(pose,*layout)!=frame)return 376;
            {
                Graphics graphics(canvas.image.get());graphics.SetCompositingMode(CompositingModeSourceCopy);graphics.Clear(Color(0,0,0,0));
                graphics.SetCompositingMode(CompositingModeSourceOver);graphics.SetInterpolationMode(InterpolationModeHighQualityBicubic);
                graphics.SetPixelOffsetMode(PixelOffsetModeHighQuality);
                const REAL scale=32.0f/std::max(1,CurrentArt(cat::Character::Cream).base.standingHeight);
                if(!DrawCareSprite(graphics,pose,90,110,scale,cat::Character::Cream,body))return 377;
                graphics.Flush(FlushIntentionSync);
            }
            // Unique opaque frame colors make frame255 distinct from frame0;
            // a byte-index wrap, placeholder or per-frame normalization fails.
            const auto* pixel=reinterpret_cast<const std::uint32_t*>(canvas.pixels)+100*180+90;
            if(*pixel!=color(frame))return 378;
            for(unsigned index=0;index<count;++index)
                if(cached->frames[index].get()!=images[index]||cached->framePixels[index].data()!=buffers[index]||
                        cached->framePixels[index].capacity()!=capacities[index])return 379;
        }
        if(!ReadNativeCareUsage(playbackAfter))return 373;
        const auto playbackMs=GetTickCount64()-playbackStart;
        for(unsigned frame=0;frame<count;++frame)
            if(cat::Checksum(std::string(reinterpret_cast<const char*>(buffers[frame]),cellWidth*cellHeight*4))!=hashes[frame])return 379;
        cat::animation::Pose invalidFrame;invalidFrame.spriteClip=Clip::Groom;invalidFrame.spriteFrame=count;
        if(CareSpriteFor(invalidFrame,cat::Character::Cream))return 379;
        if(careSpriteMemory.decodedAtlases!=savedStats.decodedAtlases||careSpriteMemory.residentPixels!=savedStats.residentPixels||
                careSpriteMemory.residentFrames!=savedStats.residentFrames)return 379;
        if(comma)report<<',';comma=true;
        report<<"{\"frame_count\":"<<count<<",\"resident_pixels\":"<<resident<<",\"resident_bytes\":"<<resident*4
            <<",\"render_calls\":"<<count*repeats<<",\"repeats\":"<<repeats
            <<",\"pointer_reuse\":true,\"budget_rejection_atomic\":true,\"source_pixels_unchanged\":true,"
            "\"decode_prepare_wall_ms\":"<<decodeMs<<",\"decode_prepare_cpu_seconds\":"<<decodeAfter.cpuSeconds-decodeBefore.cpuSeconds
            <<",\"playback_wall_ms\":"<<playbackMs<<",\"playback_cpu_seconds\":"<<playbackAfter.cpuSeconds-playbackBefore.cpuSeconds
            <<",\"private_bytes_before_playback\":"<<playbackBefore.privateBytes<<",\"private_bytes_after_playback\":"<<playbackAfter.privateBytes
            <<",\"os_peak_working_set_bytes\":"<<playbackAfter.peakWorkingSetBytes<<",\"os_peak_commit_bytes\":"<<playbackAfter.peakCommitBytes<<'}';
        g.careSprites.clear();
    }
    report<<"]}\n";
    if(!WriteBytes(L"render-self-test/care-cache-stress.json",report.str()))return 380;
    return 0;
}
