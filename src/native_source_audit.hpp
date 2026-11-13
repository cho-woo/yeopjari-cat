#pragma once

// Isolated shipping-renderer evidence. Registered-drawing samples and normal
// dispatcher samples are labelled separately; neither opens a desktop pet or
// reads the user's profile. Special interaction hosts have their own reviews.
namespace native_source_audit {
int Run(std::string& context) {
    CreateDirectoryW(L"render-self-test",nullptr);
    CreateDirectoryW(L"render-self-test/source-audit",nullptr);
    Surface actor;
    // Registered airborne drawings extend above the physical root. Keep the
    // same full-height review surface as the native jump review; a small
    // seated-only fixture clipped real flight poses at the top edge.
    constexpr int width=420,height=420;
    if(!actor.Create(width,height))return 900;
    std::ostringstream report;report<<"{\"schema\":1,\"version\":\"" YEOPJARI_VERSION_STRING
        "\",\"renderer\":\"shipping Windows GDI+\",\"samples\":[";
    bool comma=false;
    auto paint=[&](cat::Character character,const cat::animation::Pose& pose) {
        Graphics graphics(actor.image.get());graphics.Clear(Color(0,0,0,0));
        graphics.SetInterpolationMode(InterpolationModeHighQualityBicubic);
        DrawPetStyle(graphics,character,cat::Outfit::None,pose,
            cat::appearance::VisualModifiers(g.nutrition,character),210,365,110);
        graphics.Flush(FlushIntentionSync);
        return graphics.GetLastStatus()==Ok&&native_coat_tests::ValidPixels(
            reinterpret_cast<const std::uint32_t*>(actor.pixels),width,height,true);
    };
    for(const auto character:cat::kAvailableCharacters) {
      for(const auto clip:cat::sprite_sequence::kAuthoredClips) {
            native_manual_motion_tests::Host host;host.Reset(character,true);
            if(!host.artworkReady)return 901;
            const auto saved=host.Saved();const auto* asset=LoadedCareSprite(clip,character);
            if(!asset)return 901;
            for(unsigned sample=0;sample<8;++sample) {
                cat::animation::Pose pose;pose.spriteClip=clip;pose.spriteFrame=(asset->layout->frameCount-1)*sample/7;
                cat::animation::UseAuthoredSprite(pose);
                context="registered clip="+std::to_string(static_cast<unsigned>(clip))+" coat="+
                    std::to_string(static_cast<unsigned>(character))+" sample="+std::to_string(sample)+" frame="+std::to_string(pose.spriteFrame);
                if(!paint(character,pose))return 902;
                wchar_t path[180]{};swprintf(path,180,L"render-self-test/source-audit/clip-%u-c%u-s%u.png",
                    static_cast<unsigned>(clip),static_cast<unsigned>(character),sample);
                if(!SaveNativeWalkingReview(*actor.image,path))return 903;
                if(comma)report<<',';comma=true;
                report<<"{\"kind\":\"registered_drawing\",\"clip\":"<<static_cast<unsigned>(clip)
                    <<",\"character\":"<<static_cast<unsigned>(character)<<",\"sample\":"<<sample<<",\"frame\":"<<pose.spriteFrame<<'}';
            }
            if(host.Saved()!=saved)return 904;
      }
      for(unsigned motion=0;motion<=static_cast<unsigned>(Motion::Pride);++motion) {
            native_manual_motion_tests::Host host;host.Reset(character,true);
            g.cursorPlay=g.workInterference=false;g.random.seed(931);
            if(motion==static_cast<unsigned>(Motion::Walk)||motion==static_cast<unsigned>(Motion::Zoomies)) {
                host.Command(motion==static_cast<unsigned>(Motion::Walk)?kManualWalkCommand:kManualRunCommand);
                g.nextDecision=host.now+8000;
            } else {
                SetMotion(static_cast<Motion>(motion),8000,host.now);host.Rebase();
                if(cat::animation::Walking(static_cast<Motion>(motion)))g.speed=RegisteredWalkSpeed()*g.dpiScale;
            }
            const auto start=host.now,duration=g.nextDecision-g.motionStart;const auto saved=host.Saved();
            unsigned sample=0;
            for(ULONGLONG elapsed=0;elapsed<duration;elapsed+=16) {
                if(elapsed)host.Frame(start+elapsed-host.now);
                const auto physical=g.physics.RenderPose();const auto pose=PrimaryDisplayedPose(host.now,physical);
                context="motion="+std::to_string(motion)+" coat="+std::to_string(static_cast<unsigned>(character));
                if(!std::isfinite(physical.x)||!std::isfinite(physical.lift)||!cat::deform::Finite(pose.joints)||
                        !std::isfinite(pose.scaleX)||!std::isfinite(pose.scaleY))return 905;
                if(sample<8&&elapsed>=(duration-17)*sample/7) {
                    if(!paint(character,pose))return 906;
                    wchar_t path[180]{};swprintf(path,180,L"render-self-test/source-audit/motion-%u-c%u-s%u.png",
                        motion,static_cast<unsigned>(character),sample);
                    if(!SaveNativeWalkingReview(*actor.image,path))return 903;
                    const auto* asset=CareSpriteFor(pose,character);
                    if(comma)report<<',';comma=true;
                    report<<"{\"kind\":\"normal_dispatcher\",\"motion\":"<<motion<<",\"character\":"<<static_cast<unsigned>(character)
                        <<",\"sample\":"<<sample<<",\"elapsed_ms\":"<<elapsed<<",\"clip\":"<<static_cast<unsigned>(pose.spriteClip)
                        <<",\"frame\":"<<(asset?SpriteFrameFor(pose,*asset->layout):pose.frame)<<",\"grounded\":"<<(physical.grounded?"true":"false")<<'}';
                    ++sample;
                }
            }
            if(sample!=8||host.Saved()!=saved)return 907;
      }
    }
    report<<"],\"registered_clips\":"<<cat::sprite_sequence::kAuthoredClips.size()<<",\"motion_states\":32,\"coats\":3,\"profile_unchanged\":true}\n";
    return WriteBytes(L"render-self-test/source-audit/report.json",report.str())?0:908;
}
}
int NativeSourceAudit(HINSTANCE instance) {
    g.instance=instance;ULONG_PTR token=0;GdiplusStartupInput startup;
    if(GdiplusStartup(&token,&startup,nullptr)!=Ok)return 899;
    std::string context;const int result=LoadAtlas()?native_source_audit::Run(context):900;
    WriteBytes(L"render-self-test/source-audit-report.txt","exit="+std::to_string(result)+" "+context+"\n");
    ReleaseAtlases();ReleaseCarePresentation();g.brain.reset();GdiplusShutdown(token);return result;
}
