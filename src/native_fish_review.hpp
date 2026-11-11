#pragma once

namespace native_fish_review {
using Clip=cat::sprite_sequence::Clip;
int Run(std::string& context) {
    CreateDirectoryW(L"render-self-test",nullptr);CreateDirectoryW(L"render-self-test/fish-review",nullptr);
    Surface actor,board;
    if(!actor.Create(340,240)||!board.Create(2720,720))return 860;
    {Graphics graphics(board.image.get());graphics.Clear(Color(255,245,247,249));}
    std::ostringstream report;report<<"{\"schema\":1,\"version\":\"" YEOPJARI_VERSION_STRING
        "\",\"capture\":\"native Windows GDI+ / Box2D\",\"desktop_recording\":false,\"characters\":[";
    unsigned row=0;
    for(const auto character:cat::kAvailableCharacters) {
        native_manual_motion_tests::Host host;host.Reset(character,true);
        if(!host.artworkReady)return 861;
        g.cursorPlay=g.workInterference=false;g.saveErrorShown=false;g.nutritionReady=true;
        g.nutrition.fullness=g.pet.hunger=45;
        g.brain=std::make_unique<cat::behavior::Brain>(g.pet.bornUtc,42);
        g.directory=L"render-self-test/fish-review/fixture-"+std::to_wstring(static_cast<unsigned>(character));
        CreateDirectoryW(g.directory.c_str(),nullptr);
        g.savePath=g.directory+L"/pet.ini";g.backupPath=g.directory+L"/pet.bak";
        g.nutritionPath=g.directory+L"/nutrition.ini";g.nutritionBackupPath=g.directory+L"/nutrition.bak";
        CareFood(cat::feeding::FoodKind::Fish);host.Rebase();
        if(!g.fishMeal||g.motion!=Motion::Eat||g.pet.feedCount!=1||g.pet.snackCount||g.saveErrorShown)return 862;
        const auto* sprite=LoadedCareSprite(Clip::Fish,character);
        if(!sprite||std::string_view(sprite->layout->sourcePath).find("assets/video-actions/")!=0||
                sprite->layout->frameCount<120||sprite->layout->program.stepCount!=sprite->layout->frameCount)return 863;
        const auto duration=cat::sprite_sequence::ProgramDuration(sprite->layout->program);
        if(std::abs(static_cast<double>(duration)-sprite->layout->frameCount*1000./24)>2)return 863;
        const auto saved=host.Saved();const double origin=g.physics.CurrentPose().x;
        std::vector<bool> visited(sprite->layout->frameCount,false);unsigned distinct=0,sample=0;
        bool grooming=false;const auto start=host.now;
        for(unsigned tick=0;tick<1000;++tick) {
            if(tick)host.Frame(start+static_cast<ULONGLONG>(tick)*1000/60-host.now);
            const auto physical=g.physics.RenderPose();const auto pose=PrimaryDisplayedPose(host.now,physical);
            if(!physical.grounded||std::abs(physical.x-origin)>.05||physical.lift>.1)return 864;
            if(pose.spriteClip==Clip::Fish) {
                const auto index=SpriteFrameFor(pose,*sprite->layout);
                if(index>=visited.size()||!pose.fishMeal)return 865;
                const bool firstDrawing=!visited[index];
                if(firstDrawing){visited[index]=true;++distinct;}
                {
                    Graphics graphics(actor.image.get());graphics.Clear(Color(0,0,0,0));
                    graphics.SetInterpolationMode(InterpolationModeHighQualityBicubic);
                    DrawPetStyle(graphics,character,cat::Outfit::None,pose,
                        cat::appearance::VisualModifiers(g.nutrition,character),130,220,110);
                    graphics.Flush(FlushIntentionSync);if(graphics.GetLastStatus()!=Ok)return 866;
                }
                unsigned painted=0;
                for(int y=0;y<actor.height;++y)for(int x=0;x<actor.width;++x) {
                    const auto* pixel=actor.pixels+(static_cast<std::size_t>(y)*actor.width+x)*4;
                    if(pixel[0]>pixel[3]||pixel[1]>pixel[3]||pixel[2]>pixel[3])return 866;
                    if(pixel[3]>16) {
                        ++painted;if(x==0||y==0||x==actor.width-1||y==actor.height-1)return 866;
                    }
                }
                if(painted<500)return 866;
                if(firstDrawing) {
                    wchar_t path[180]{};swprintf(path,180,L"render-self-test/fish-review/c%u-frame-%03u.png",
                        static_cast<unsigned>(character),index);
                    if(!SaveNativeWalkingReview(*actor.image,path))return 868;
                }
                if(sample<8&&index>=((sprite->layout->frameCount-1)*sample/7)) {
                    Graphics graphics(board.image.get());graphics.DrawImage(actor.image.get(),static_cast<int>(sample*340),static_cast<int>(row*240));++sample;
                }
            }
            if(g.motion==Motion::Groom&&distinct==visited.size()) {grooming=true;break;}
        }
        context="fish character="+std::to_string(static_cast<unsigned>(character))+" visited="+std::to_string(distinct)+
            " source="+std::to_string(sprite->layout->frameCount)+" groom="+std::to_string(grooming);
        if(distinct!=visited.size()||sample!=8||!grooming||host.Saved()!=saved)return 867;
        if(row)report<<',';
        report<<"{\"character\":"<<static_cast<unsigned>(character)<<",\"source_frames\":"<<distinct
            <<",\"duration_ms\":"<<duration<<",\"source_fps\":24,\"ground_root_fixed\":true,\"feed_count\":1,\"groom_after_meal\":true}";
        ++row;
    }
    report<<"]}\n";
    return SaveNativeWalkingReview(*board.image,L"render-self-test/fish-review/fish-board.png")&&
        WriteBytes(L"render-self-test/fish-review/report.json",report.str())?0:868;
}
}
int NativeFishReview(HINSTANCE instance) {
    g.instance=instance;ULONG_PTR token=0;GdiplusStartupInput startup;
    if(GdiplusStartup(&token,&startup,nullptr)!=Ok)return 859;
    std::string context="source-video fish review";
    const int result=LoadAtlas()?native_fish_review::Run(context):860;
    WriteBytes(L"render-self-test/fish-review-report.txt","exit="+std::to_string(result)+" "+context+"\n");
    ReleaseAtlases();ReleaseCarePresentation();g.brain.reset();GdiplusShutdown(token);return result;
}
