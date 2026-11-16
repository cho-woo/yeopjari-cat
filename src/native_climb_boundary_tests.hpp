#pragma once
// Included inside native_manual_motion_tests. Paint the actual authored final
// climb poses and the ordinary planted neutral in the shipping renderer.
namespace climb_boundary {
int Run(std::string& detail) {
    CreateDirectoryW(L"render-self-test",nullptr);
    CreateDirectoryW(L"render-self-test/climb-boundary",nullptr);
    Surface image,previous,direct,previousDirect;
    constexpr int W=260,H=260;constexpr std::size_t bytes=W*H*4;
    if(!image.Create(W,H)||!previous.Create(W,H)||!direct.Create(W,H)||!previousDirect.Create(W,H))return 560;
    std::ostringstream report;report.imbue(std::locale::classic());
    report<<"{\"version\":\"" YEOPJARI_VERSION_STRING "\",\"capture\":\"shipping GDI+, synthetic clock, fixed planted root\",\"cases\":[";
    bool comma=false;
    for(const auto character:cat::kAvailableCharacters) {
        Host host;host.Reset(character,true);if(!host.artworkReady)return 561;
        const auto saved=host.Saved();CarePresentationRenderer presentation;
        const auto* climb=LoadedCareSprite(cat::sprite_sequence::Clip::Climb,character);
        if(!climb)return 561;
        const auto duration=cat::sprite_sequence::ProgramDuration(climb->layout->program);
        double boundary=0,directBoundary=0,endpoint=0;bool held=false;
        for(unsigned frame=0;frame<42;++frame) {
            auto pose=cat::animation::Evaluate(Motion::Idle,0,host.now+frame*16,1);pose.groundContact=true;pose.flip=false;
            if(frame<19)cat::animation::AttachSprite(pose,cat::sprite_sequence::Clip::Climb,duration-304+frame*16);
            else {
                pose.spriteClip=cat::sprite_sequence::Clip::Sleep;pose.spriteFrame=0;pose.spriteTimed=false;
                cat::animation::UseAuthoredSprite(pose);
            }
            const auto draw=[&](Surface& surface,CarePresentationRenderer* state) {
                Graphics graphics(surface.image.get());graphics.SetCompositingMode(CompositingModeSourceCopy);
                graphics.Clear(Color(0,0,0,0));graphics.SetCompositingMode(CompositingModeSourceOver);
                graphics.SetInterpolationMode(InterpolationModeHighQualityBicubic);graphics.SetSmoothingMode(SmoothingModeAntiAlias);
                graphics.SetPixelOffsetMode(PixelOffsetModeHighQuality);
                DrawPetStyle(graphics,character,cat::Outfit::None,pose,cat::appearance::VisualModifiers(g.nutrition,character),130,210,110,state,host.now+frame*16);
                graphics.Flush(FlushIntentionSync);
            };
            draw(image,&presentation);draw(direct,nullptr);
            const auto difference=[&](const BYTE* a,const BYTE* b) {
                double total=0;std::size_t active=0;
                for(std::size_t i=0;i<bytes;i+=4)if(std::max(a[i+3],b[i+3])>16) {
                    for(unsigned channel=0;channel<4;++channel)total+=std::abs(int(a[i+channel])-b[i+channel]);++active;
                }
                return active?total/(active*4*255):0.;
            };
            if(frame==19){boundary=difference(previous.pixels,image.pixels);directBoundary=difference(previousDirect.pixels,direct.pixels);held=presentation.transition.Active(host.now+frame*16);}
            if(frame==41)endpoint=difference(direct.pixels,image.pixels);
            const auto file=L"render-self-test/climb-boundary/c"+std::to_wstring(static_cast<unsigned>(character))+L"-f"+std::to_wstring(frame)+L".png";
            const auto directFile=L"render-self-test/climb-boundary/direct-c"+std::to_wstring(static_cast<unsigned>(character))+L"-f"+std::to_wstring(frame)+L".png";
            if(!SaveNativeWalkingReview(*image.image,file.c_str())||!SaveNativeWalkingReview(*direct.image,directFile.c_str()))return 562;
            std::memcpy(previous.pixels,image.pixels,bytes);
            std::memcpy(previousDirect.pixels,direct.pixels,bytes);
        }
        if(comma)report<<',';comma=true;
        report<<"{\"coat\":"<<static_cast<unsigned>(character)<<",\"direct_boundary_pixel_delta\":"<<directBoundary<<",\"first_neutral_pixel_delta\":"<<boundary
            <<",\"endpoint_direct_delta\":"<<endpoint<<",\"transition_active\":"<<(held?"true":"false")<<'}';
        detail="climb planted boundary coat="+std::to_string(static_cast<unsigned>(character))+" first_delta="+std::to_string(boundary);
        if(!held||boundary>.02||endpoint>.001||host.Saved()!=saved) {
            report<<"],\"passed\":false}";WriteBytes(L"render-self-test/climb-boundary/report.json",report.str());return 563;
        }
    }
    report<<"],\"passed\":true,\"profile_unchanged\":true}";
    return WriteBytes(L"render-self-test/climb-boundary/report.json",report.str())?0:564;
}
}
