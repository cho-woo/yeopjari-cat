// Linux/portable visual review of the exact model loader and skeletal sampler.
// This CPU rasterizer is a review aid, NOT a substitute for Windows D3D tests.
// Build: g++ -std=c++17 -O2 -Wall -Wextra -Werror -I src tools/cat_3d_asset_review.cpp -o build/cat_3d_asset_review
// Usage: build/cat_3d_asset_review model.bin output-dir [clip-name] [sample-count]

#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>
#include "cat_3d_model.hpp"

namespace {
using Vec3 = std::array<double, 3>;
using RGB = std::array<unsigned char, 3>;
using Vertex = cat::three_d::RenderVertex;
constexpr int kWidth = 400, kHeight = 320, kSamples = 2;
constexpr double kYaw = -.8;

double Dot(const Vec3& a, const Vec3& b) { return a[0]*b[0] + a[1]*b[1] + a[2]*b[2]; }
Vec3 Unit(Vec3 v) {
    const double inverse = 1 / std::sqrt(std::max(1e-12, Dot(v, v)));
    for (auto& x : v) x *= inverse;
    return v;
}
double Srgb(double x) {
    x = std::clamp(x, 0.0, 1.0);
    return x <= .0031308 ? 12.92*x : 1.055*std::pow(x, 1/2.4)-.055;
}
double Edge(double ax, double ay, double bx, double by, double px, double py) {
    return (px-ax)*(by-ay) - (py-ay)*(bx-ax);
}

std::vector<RGB> Raster(const std::vector<Vertex>& mesh, const std::vector<std::uint32_t>& indices,
                        double pixelsPerUnit) {
    const int width=kWidth*kSamples, height=kHeight*kSamples;
    std::vector<Vec3> rgb(std::size_t(width)*height, Vec3{.94,.95,.96});
    std::vector<double> depth(std::size_t(width)*height, std::numeric_limits<double>::infinity());
    const Vec3 right{std::cos(kYaw),std::sin(kYaw),0}, view{std::sin(kYaw),-std::cos(kYaw),0};
    const Vec3 key=Unit({-.5,-.8,1.15}), fill=Unit({.8,-.4,.5});
    const Vec3 half=Unit({key[0]+view[0],key[1]+view[1],key[2]+view[2]});
    struct Projected {double x,y,z;};
    std::vector<Projected> points;
    points.reserve(mesh.size());
    for(const auto& v:mesh) {
        const Vec3 p{v.position[0],v.position[1],v.position[2]};
        points.push_back({width*.5+Dot(p,right)*pixelsPerUnit*kSamples,
                          (kHeight-20)*kSamples-p[2]*pixelsPerUnit*kSamples,-Dot(p,view)});
    }
    for(std::size_t i=0;i<indices.size();i+=3) {
        const auto ia=indices[i],ib=indices[i+1],ic=indices[i+2];
        const auto& a=points[ia]; const auto& b=points[ib]; const auto& c=points[ic];
        const double area=Edge(a.x,a.y,b.x,b.y,c.x,c.y);
        if(std::abs(area)<1e-8) continue;
        const double minX=std::min({a.x,b.x,c.x}), maxX=std::max({a.x,b.x,c.x});
        const double minY=std::min({a.y,b.y,c.y}), maxY=std::max({a.y,b.y,c.y});
        if(maxX<0||maxY<0||minX>=width||minY>=height)continue;
        const int x0=int(std::clamp(std::floor(minX),0.0,double(width-1)));
        const int y0=int(std::clamp(std::floor(minY),0.0,double(height-1)));
        const int x1=int(std::clamp(std::ceil(maxX),0.0,double(width-1)));
        const int y1=int(std::clamp(std::ceil(maxY),0.0,double(height-1)));
        for(int y=y0;y<=y1;++y) for(int x=x0;x<=x1;++x) {
            const double w0=Edge(b.x,b.y,c.x,c.y,x+.5,y+.5)/area;
            const double w1=Edge(c.x,c.y,a.x,a.y,x+.5,y+.5)/area;
            const double w2=1-w0-w1;
            if(w0<0||w1<0||w2<0)continue;
            const double z=w0*a.z+w1*b.z+w2*c.z;
            const std::size_t at=std::size_t(y)*width+x;
            if(z>=depth[at])continue;
            depth[at]=z;
            Vec3 n{},color{};
            for(int j=0;j<3;++j) {
                n[j]=w0*mesh[ia].normal[j]+w1*mesh[ib].normal[j]+w2*mesh[ic].normal[j];
                color[j]=w0*mesh[ia].color[j]+w1*mesh[ib].color[j]+w2*mesh[ic].color[j];
            }
            n=Unit(n);
            const double roughness=std::clamp(w0*mesh[ia].roughness+w1*mesh[ib].roughness+w2*mesh[ic].roughness,.08,1.0);
            const double diffuse=.47+.40*std::max(0.0,Dot(n,key))+.14*std::max(0.0,Dot(n,fill));
            const double spec=std::pow(std::max(0.0,Dot(n,half)),120*(1-roughness)+8*roughness)*(1-roughness)*.33;
            const double rim=std::pow(1-std::clamp(Dot(n,view),0.0,1.0),3)*std::clamp(n[2]*.6+.4,0.0,1.0)*roughness*.035;
            const double alpha=std::clamp(w0*mesh[ia].color[3]+w1*mesh[ib].color[3]+w2*mesh[ic].color[3],0.0,1.0);
            constexpr double tint[3]={1,.95,.88};
            for(int j=0;j<3;++j)rgb[at][j]=Srgb(color[j]*diffuse+tint[j]*spec+color[j]*rim)*alpha+rgb[at][j]*(1-alpha);
        }
    }
    std::vector<RGB> result(std::size_t(kWidth)*kHeight);
    for(int y=0;y<kHeight;++y)for(int x=0;x<kWidth;++x) {
        for(int channel=0;channel<3;++channel) {
            double sum=0;
            for(int yy=0;yy<kSamples;++yy)for(int xx=0;xx<kSamples;++xx)
                sum+=rgb[std::size_t(y*kSamples+yy)*width+x*kSamples+xx][channel];
            result[std::size_t(y)*kWidth+x][channel]=static_cast<unsigned char>(std::lround(std::clamp(sum/(kSamples*kSamples),0.0,1.0)*255));
        }
    }
    // A fixed contact guide marks the world z=0 support plane. It is a review
    // overlay and is not part of either the asset or the native app renderer.
    for(int x=0;x<kWidth;++x)result[std::size_t(kHeight-20)*kWidth+x]=RGB{160,175,180};
    return result;
}

bool Save(const std::filesystem::path& path,const std::vector<RGB>& rgb,int w,int h) {
    std::ofstream file(path,std::ios::binary);
    file<<"P6\n"<<w<<' '<<h<<"\n255\n";
    for(const auto& pixel:rgb)file.write(reinterpret_cast<const char*>(pixel.data()),3);
    return bool(file);
}
}

int main(int argc,char** argv) {
    if(argc<3||argc>5) {
        std::cerr<<"usage: cat_3d_asset_review model.bin output-dir [clip-name] [sample-count]\n";
        return 2;
    }
    int sampleCount=5;
    if(argc==5) {
        try {
            std::size_t used=0;
            sampleCount=std::stoi(argv[4],&used);
            if(used!=std::string(argv[4]).size())throw std::invalid_argument("trailing count text");
        } catch(const std::exception&) {std::cerr<<"sample count must be an integer\n";return 2;}
    }
    if(sampleCount<2||sampleCount>240) { std::cerr<<"sample count must be 2..240\n"; return 2; }
    cat::three_d::Model model;
    std::string error;
    if(!model.LoadFile(std::filesystem::path(argv[1]),error)) { std::cerr<<error<<'\n'; return 1; }
    const std::filesystem::path out(argv[2]);
    std::filesystem::create_directories(out);
    std::vector<std::size_t> clips;
    for(std::size_t i=0;i<model.Clips().size();++i)
        if(argc<4||model.Clips()[i].name==argv[3])clips.push_back(i);
    if(clips.empty()) { std::cerr<<"clip not found\n"; return 2; }
    if(clips.size()*sampleCount>240) {
        std::cerr<<"contact sheet limit is 240 frames; select a single clip for dense sampling\n";
        return 2;
    }
    const double height=model.Bounds().maximum[2]-std::min(0.0f,model.Bounds().minimum[2]);
    const double pixelsPerUnit=std::min(150.0,(kHeight-50)/height);
    std::vector<Vertex> mesh;
    std::ofstream manifest(out/"frames.csv");
    manifest<<"clip,sample,seconds,minZ,maxZ,preview\n";
    const int columns=std::min(sampleCount,5);
    const int rows=static_cast<int>(clips.size())*((sampleCount+columns-1)/columns);
    const int sheetWidth=columns*kWidth,sheetHeight=rows*kHeight;
    std::vector<RGB> sheet(std::size_t(sheetWidth)*sheetHeight,RGB{240,242,245});
    int sheetRow=0;
    for(const auto clip:clips) {
        const auto& name=model.Clips()[clip].name;
        for(int sample=0;sample<sampleCount;++sample) {
            const double time=double(sample)/(sampleCount-1)*model.Duration(clip);
            if(!model.Sample(clip,time,mesh,&error)) {std::cerr<<error<<'\n';return 1;}
            const auto bounds=cat::three_d::BoundsOf(mesh);
            const std::string filename=name+"-"+std::to_string(sample)+".ppm";
            const auto rgb=Raster(mesh,model.Indices(),pixelsPerUnit);
            if(!Save(out/filename,rgb,kWidth,kHeight)) {std::cerr<<"PPM write failed\n";return 1;}
            manifest<<name<<','<<sample<<','<<time<<','<<bounds.minimum[2]<<','<<bounds.maximum[2]<<','<<filename<<'\n';
            const int row=sheetRow+sample/columns,col=sample%columns;
            for(int y=0;y<kHeight;++y)std::copy_n(rgb.begin()+std::size_t(y)*kWidth,kWidth,
                sheet.begin()+std::size_t(row*kHeight+y)*sheetWidth+col*kWidth);
        }
        sheetRow+=(sampleCount+columns-1)/columns;
        std::cout<<"reviewed "<<name<<'\n';
    }
    if(!Save(out/"contact-sheet.ppm",sheet,sheetWidth,sheetHeight)) return 1;
    std::ofstream info(out/"README.txt");
    info<<"CPU rasterizer of actual cat::three_d::Model::Sample output.\n"
        <<"Linear RGB lighting follows the D3D shader except subpixel coat noise is omitted.\n"
        <<"This does not validate Windows shader execution, alpha readback, or native performance.\n"
        <<"No GroundThreeDVertices correction is applied: the guide is raw model world z=0.\n"
        <<"Rows follow frames.csv. Camera yaw=-0.8, pitch=0, pixelsPerUnit="<<pixelsPerUnit<<".\n";
    return manifest&&info?0:1;
}
