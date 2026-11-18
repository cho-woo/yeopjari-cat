// Offline adapter. Rasterize the Windows joint/face animations with the shipping
// renderer; the Android application requires neither a PC nor a native library.
#include "../src/main.cpp"
#include <fcntl.h>
#include <io.h>
int main(int argc,char** argv) {
    if(argc!=3)return 2;
    g.instance=LoadLibraryExA(argv[1],nullptr,LOAD_LIBRARY_AS_DATAFILE);
    if(!g.instance || FAILED(CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED)))return 3;
    ULONG_PTR token=0;GdiplusStartupInput startup;
    if(GdiplusStartup(&token,&startup,nullptr)!=Ok || !LoadAtlas())return 4;
    const auto coat=static_cast<cat::Character>(atoi(argv[2]));
    if(!cat::CharacterAvailable(coat))return 5;
    _setmode(_fileno(stdout),_O_BINARY);
    using M=cat::animation::Motion;
    struct Item {const char* name;M motion;unsigned frames;};
    const Item actions[]={{"idle-breathe",M::Idle,108},{"watch",M::Watch,72},{"ear-flick",M::EarFlick,44},
        {"tail-raise",M::TailRaise,78},{"loaf",M::Loaf,78},{"peek",M::Peek,72},{"sneeze",M::Sneeze,42},
        {"yawn",M::Yawn,58},{"sleep-talk",M::SleepTalk,60},{"hang",M::Hang,64},{"pride",M::Pride,84},
        {"sniff",M::Sniff,48},{"look-down",M::LookDown,48},{"balance",M::Balance,40},{"startled",M::Startled,32}};
    Surface image;if(!image.Create(224,192))return 6;
    for(auto item:actions){
        printf("%s %u\n",item.name,item.frames);
        for(unsigned n=0;n<item.frames;n++){
            const auto ms=1000u*n/24;
            const auto pose=cat::animation::Evaluate(item.motion,ms,ms,1);
            Graphics graphics(image.image.get());graphics.Clear(Color(0,0,0,0));
            graphics.SetInterpolationMode(InterpolationModeHighQualityBicubic);
            graphics.SetSmoothingMode(SmoothingModeAntiAlias);graphics.SetPixelOffsetMode(PixelOffsetModeHighQuality);
            DrawPetStyle(graphics,coat,cat::Outfit::None,pose,cat::appearance::VisualModifiers(g.nutrition,coat),112,150,96);
            graphics.Flush(FlushIntentionSync);
            if(fwrite(image.pixels,1,224*192*4,stdout)!=224*192*4)return 7;
        }
    }
    fflush(stdout);
    // GDI+ backs the process-wide artwork cache until process exit.
    return 0;
}
