#include "../src/cat_recline_transition.hpp"
#include "../src/cat_animation.hpp"

extern "C" int recline_preview(const std::uint32_t* source,int width,int height,std::uint32_t* destination,
    int canvasWidth,int canvasHeight,int character,int frame,const double* geometry) {
    using namespace cat::recline;
    const auto c=static_cast<cat::Character>(character);
    const Geometry size{geometry[0],geometry[1]};
    Mesh mesh;
    if(!Build(c,static_cast<int>(geometry[2]),{geometry[3],geometry[4]},
        static_cast<int>(geometry[5]),{geometry[6],geometry[7]},geometry[8],mesh))return -1;
    return Raster(source,width,height,destination,canvasWidth,canvasHeight,size,mesh,
        frame==static_cast<int>(geometry[5]),{geometry[9],geometry[10]},geometry[11]!=0)?1:-2;
}
extern "C" int recline_preview_close(const std::uint32_t* source,int width,int height,std::uint32_t* destination,
    int character,int frame,const std::uint32_t* closed,int closedWidth,int closedHeight,double blink) {
    const auto c=static_cast<cat::Character>(character);
    return cat::recline::CloseEyes(source,width,height,destination,c,frame,blink,
        {closed,closedWidth,closedHeight,cat::species::ClosedFaceFor(c)});
}
extern "C" void recline_preview_landmarks(int character,int frame,double* output) {
    const auto f=cat::recline::FaceFor(static_cast<cat::Character>(character),frame);
    output[0]=f.leftEye.x;output[1]=f.leftEye.y;output[2]=f.rightEye.x;output[3]=f.rightEye.y;
    output[4]=f.mouth.x;output[5]=f.mouth.y;
}
extern "C" void recline_preview_pose(unsigned elapsed,double* output) {
    const auto pose=cat::animation::Evaluate(cat::animation::Motion::BellyRoll,elapsed,100000+elapsed,1);
    output[0]=pose.frame;output[1]=pose.blendFrame;output[2]=pose.blend;
}
extern "C" double recline_preview_blink(int first,int second,double blend) {return cat::recline::TurnBlink(first,second,blend);}
extern "C" int recline_preview_skin(const std::uint32_t* source,int width,int height,std::uint32_t* destination,
    int frame,unsigned elapsed,int sleeping) {
    auto pose=sleeping?cat::animation::EvaluateSleep(frame==24?1:3,elapsed,100000+elapsed,1):
        cat::animation::Evaluate(cat::animation::Motion::BellyRoll,elapsed,100000+elapsed,1);
    auto joints=pose.joints;
    for(auto value:std::array<double*,11>{{&joints.leftEar,&joints.rightEar,&joints.headNod,&joints.frontLeft,&joints.frontRight,
        &joints.tailRaise,&joints.breath,&joints.crouch,&joints.pawReachX,&joints.pawReachY,&joints.pawAnchorX}})
        *value=std::lround(*value*32)/32.;
    return cat::deform::Warp(source,width,height,width*4,destination,width*4,joints,
        frame==4?cat::deform::Shape::Sitting:frame==24?cat::deform::Shape::ReclineSide:cat::deform::Shape::ReclineBack);
}
#include <chrono>
#include <vector>
extern "C" double recline_preview_benchmark(const std::uint32_t* a,int aw,int ah,const std::uint32_t* b,int bw,int bh,
    int character,const double* geometry,const std::uint32_t* closed,int closedWidth,int closedHeight,int repetitions) {
    using namespace cat::recline;
    const auto c=static_cast<cat::Character>(character);
    std::vector<std::uint32_t> pa(static_cast<std::size_t>(aw)*ah),pb(static_cast<std::size_t>(bw)*bh),
        outA(420*320),outB(420*320);
    Mesh mesh;
    const Geometry ga{geometry[0],geometry[1]},gb{geometry[2],geometry[3]};
    const auto start=std::chrono::steady_clock::now();
    for(int i=0;i<repetitions;++i){
        const double t=(i%9+1)/10.;
        if(!Build(c,24,ga,25,gb,t,mesh)||
           !CloseEyes(a,aw,ah,pa.data(),c,24,TurnBlink(24,25,t),{closed,closedWidth,closedHeight,cat::species::ClosedFaceFor(c)})||
           !CloseEyes(b,bw,bh,pb.data(),c,25,TurnBlink(24,25,t),{closed,closedWidth,closedHeight,cat::species::ClosedFaceFor(c)})||
           !Raster(pa.data(),aw,ah,outA.data(),420,320,ga,mesh,false,{210,280})||
           !Raster(pb.data(),bw,bh,outB.data(),420,320,gb,mesh,true,{210,280}))return -1;
        const unsigned weight=static_cast<unsigned>(std::lround(t*255));
        const auto src=reinterpret_cast<const unsigned char*>(outB.data());auto dst=reinterpret_cast<unsigned char*>(outA.data());
        for(std::size_t k=0;k<420*320*4;++k)dst[k]=(dst[k]*(255-weight)+src[k]*weight+127)/255;
    }
    return std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count()/repetitions;
}
