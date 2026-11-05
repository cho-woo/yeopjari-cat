#pragma once

#include "cat_species.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>

namespace cat::recline {
using Point = deform::Point;
inline bool Frame(int frame) { return frame==4 || frame==24 || frame==25; }
inline double TurnBlink(int first,int second,double blend) {
    if(!((first==24&&second==25)||(first==25&&second==24))||!std::isfinite(blend))return 0;
    blend=std::clamp(blend,0.0,1.0);
    return deform::Smooth(4*blend*(1-blend));
}
// Measured in alpha-cropped, unmirrored source art. The line between the eyes
// defines head orientation; no upright head is pasted onto a reclining neck.
inline expression::Landmarks FaceFor(Character character,int frame) {
    if(frame==4)return species::FaceFor(character);
    constexpr std::array<expression::Landmarks,10> side{{
        {{.760,.535},{.910,.468},{.857,.645}},
        {{.748,.526},{.900,.457},{.851,.628}},
        {{.667,.637},{.883,.588},{.817,.747}},
        {{.677,.597},{.894,.550},{.815,.729}},
        {{.650,.718},{.864,.661},{.783,.820}},
        {{.660,.621},{.870,.580},{.797,.735}},
        {{.594,.600},{.825,.478},{.784,.730}},
        {{.661,.714},{.883,.669},{.812,.813}},
        {{.681,.678},{.885,.613},{.817,.814}},
        {{.647,.629},{.870,.588},{.793,.758}}
    }};
    constexpr std::array<expression::Landmarks,10> back{{
        {{.669,.443},{.795,.596},{.704,.636}},
        {{.659,.447},{.788,.603},{.702,.640}},
        {{.670,.375},{.770,.586},{.688,.572}},
        {{.647,.389},{.737,.577},{.652,.585}},
        {{.660,.367},{.764,.537},{.677,.553}},
        {{.650,.362},{.748,.550},{.659,.555}},
        {{.661,.243},{.769,.435},{.672,.499}},
        {{.590,.464},{.689,.634},{.598,.623}},
        {{.655,.356},{.762,.546},{.679,.566}},
        {{.664,.385},{.775,.573},{.690,.577}}
    }};
    const auto index=IsValid(character)?static_cast<std::size_t>(character):0u;
    return frame==24?side[index]:back[index];
}
struct Geometry { double width=0,height=0; };
struct FaceTriangle { Point left,right,mouth; };
inline Point World(Point uv,Geometry size) {return {(uv.x-.5)*size.width,(uv.y-1)*size.height};}
inline FaceTriangle FaceTriangleFor(Character character,int frame,Geometry size) {
    const auto face=FaceFor(character,frame);
    return {World(face.leftEye,size),World(face.rightEye,size),World(face.mouth,size)};
}
inline std::array<Point,2> EarsFor(Character character,int frame) {
    constexpr std::array<std::array<Point,2>,10> sitting{{
        {{{.17781,.06694},{.81915,.01230}}}, {{{.17286,.06314},{.86431,.01195}}},
        {{{.33209,.07000},{.80970,.02333}}}, {{{.33459,.09184},{.79323,.02381}}},
        {{{.32963,.07278},{.83333,.02215}}}, {{{.35714,.09926},{.80827,.02574}}},
        {{{.50775,.03929},{.63178,.03214}}}, {{{.24336,.04333},{.74779,.02333}}},
        {{{.33459,.08741},{.77820,.03147}}}, {{{.33712,.08993},{.79924,.03237}}}
    }};
    constexpr std::array<std::array<Point,2>,10> side{{
        {{{.55714,.12293},{.85038,.01519}}}, {{{.54662,.13435},{.85789,.01247}}},
        {{{.46333,.07386},{.85667,.03977}}}, {{{.47635,.07303},{.86149,.03933}}},
        {{{.43667,.12500},{.82333,.03804}}}, {{{.45364,.07558},{.86424,.05233}}},
        {{{.31457,.19880},{.67219,.06627}}}, {{{.30,.12},{.54452,.04070}}},
        {{{.46333,.11310},{.85000,.05357}}}, {{{.45973,.07927},{.86913,.04268}}}
    }};
    constexpr std::array<std::array<Point,2>,10> back{{
        {{{.72333,.01847},{.98222,.32528}}}, {{{.71702,.01836},{.98085,.32627}}},
        {{{.79635,.04830},{.98455,.40057}}}, {{{.79462,.04830},{.98159,.42898}}},
        {{{.77824,.03514},{.97383,.41351}}}, {{{.78073,.03125},{.97626,.42330}}},
        {{{.79167,.05030},{.86425,.12722}}}, {{{.79380,.03333},{.97978,.35641}}},
        {{{.77520,.02895},{.97684,.40263}}}, {{{.76685,.02850},{.97709,.39637}}}
    }};
    const auto c=IsValid(character)?static_cast<std::size_t>(character):0;
    return frame==4?sitting[c]:frame==24?side[c]:back[c];
}
struct Triangle { int a=0,b=0,c=0; };
struct Mesh {
    std::array<Point,16> first{},second{},common{};
    std::array<Triangle,25> triangles{};
    int count=0,vertices=11;
};
inline Point Blend(Point a,Point b,double t) {return {a.x+(b.x-a.x)*t,a.y+(b.y-a.y)*t};}
inline double Area(Point a,Point b,Point c) {return deform::detail::Cross(a,b,c);}
inline double MinimumArea(const Mesh& mesh,Triangle tri) {
    const auto area=[&](double t){return Area(Blend(mesh.first[tri.a],mesh.second[tri.a],t),
        Blend(mesh.first[tri.b],mesh.second[tri.b],t),Blend(mesh.first[tri.c],mesh.second[tri.c],t));};
    const double first=area(0),middle=area(.5),last=area(1);
    const double a=2*(first+last-2*middle),b=last-first-a;
    double result=std::min(first,last);
    if(a>1e-12){const double t=-b/(2*a);if(t>0&&t<1)result=std::min(result,area(t));}
    return result;
}
struct Kernel { std::array<Point,12> points{};int count=0;Point centre; };
inline Kernel QuadKernel(const std::array<Point,16>& points,std::array<int,4> ids) {
    Kernel shape;shape.count=4;
    double left=points[ids[0]].x,right=left,top=points[ids[0]].y,bottom=top;
    for(int id:ids){left=std::min(left,points[id].x);right=std::max(right,points[id].x);top=std::min(top,points[id].y);bottom=std::max(bottom,points[id].y);}
    shape.points={{{left,top},{right,top},{right,bottom},{left,bottom}}};
    for(int edge=0;edge<4&&shape.count;++edge){
        Kernel next;const auto a=points[ids[edge]],b=points[ids[(edge+1)%4]];
        for(int i=0;i<shape.count;++i){
            const auto p=shape.points[i],q=shape.points[(i+1)%shape.count];
            const double pa=Area(a,b,p),qa=Area(a,b,q);
            if(pa>=0)next.points[next.count++]=p;
            if((pa<0)!=(qa<0)){const double t=pa/(pa-qa);next.points[next.count++]=Blend(p,q,t);}
        }
        shape=next;
    }
    for(int i=0;i<shape.count;++i){shape.centre.x+=shape.points[i].x/shape.count;shape.centre.y+=shape.points[i].y/shape.count;}
    return shape;
}
inline bool Build(Character character,int first,Geometry a,int second,Geometry b,double weight,Mesh& mesh) {
    if(!IsValid(character)||!Frame(first)||!Frame(second)||!std::isfinite(weight)||
        !std::isfinite(a.width)||!std::isfinite(a.height)||!std::isfinite(b.width)||!std::isfinite(b.height)||
        a.width<1||a.height<1||b.width<1||b.height<1||a.width>8192||a.height>8192||b.width>8192||b.height>8192)return false;
    Mesh result;result.vertices=11;
    // The full eye and mouth region is one affine quadrilateral, so an iris
    // cannot straddle a head/body triangle with a different deformation.
    const auto points=[&](std::array<Point,16>& out,int frame,Geometry size){
        const auto face=FaceTriangleFor(character,frame,size);
        const Point centre{(face.left.x+face.right.x)*.5,(face.left.y+face.right.y)*.5};
        const Point axis{face.right.x-face.left.x,face.right.y-face.left.y};
        const Point lower{face.mouth.x-centre.x,face.mouth.y-centre.y};
        const auto point=[&](double u,double v){return Point{centre.x+u*axis.x+v*lower.x,centre.y+u*axis.y+v*lower.y};};
        out[5]=point(-1.0,-1.5);out[6]=point(1.0,-1.5);out[7]=point(1.0,1.35);out[8]=point(-1.0,1.35);
        const auto ears=EarsFor(character,frame);out[9]=World(ears[0],size);out[10]=World(ears[1],size);
    };
    points(result.first,first,a);points(result.second,second,b);
    double halfWidth=std::max(a.width,b.width)*.5,height=std::max(a.height,b.height);
    for(const auto& source:{result.first,result.second})for(int i=5;i<11;++i){
        halfWidth=std::max(halfWidth,std::abs(source[i].x));height=std::max(height,-source[i].y);
        if(source[i].y>=-.001)return false;
    }
    halfWidth*=5.0;height*=1.25;
    const std::array<Point,5> outer{{{-halfWidth,-height},{halfWidth,-height},{halfWidth,0},{0,0},{-halfWidth,0}}};
    std::copy(outer.begin(),outer.end(),result.first.begin());std::copy(outer.begin(),outer.end(),result.second.begin());
    const auto add=[&](Triangle tri){if(MinimumArea(result,tri)<=1e-6)return false;result.triangles[result.count++]=tri;return true;};
    const auto quad=[&](int ia,int ib,int ic,int id){
        const std::array<Triangle,2> ac{{{ia,ib,ic},{ia,ic,id}}},bd{{{ia,ib,id},{ib,ic,id}}};
        const double firstMin=std::min(MinimumArea(result,ac[0]),MinimumArea(result,ac[1]));
        const double secondMin=std::min(MinimumArea(result,bd[0]),MinimumArea(result,bd[1]));
        if(std::max(firstMin,secondMin)<=1e-6){
            const std::array<int,4> ids{{ia,ib,ic,id}};
            const auto ka=QuadKernel(result.first,ids),kb=QuadKernel(result.second,ids);
            if(ka.count<3||kb.count<3||result.vertices>=16)return false;
            const int centre=result.vertices;
            for(int ai=-1;ai<ka.count;++ai)for(int bi=-1;bi<kb.count;++bi){
                result.first[centre]=ai<0?ka.centre:Blend(ka.centre,ka.points[ai],.5);
                result.second[centre]=bi<0?kb.centre:Blend(kb.centre,kb.points[bi],.5);
                bool valid=true;
                for(int i=0;i<4;++i)valid=valid&&MinimumArea(result,{ids[i],ids[(i+1)%4],centre})>1e-6;
                if(valid){for(int i=0;i<4;++i)result.triangles[result.count++]={ids[i],ids[(i+1)%4],centre};++result.vertices;return true;}
            }
            return false;
        }
        for(auto tri:firstMin>=secondMin?ac:bd)result.triangles[result.count++]=tri;
        return true;
    };
    if(!add({5,6,7})||!add({5,7,8}))return false;
    // The ear tips register separately above the affine face patch; the top
    // ring is still a connected source mesh, never a pasted replacement head.
    if(character==Character::Dog) {if(!quad(0,1,6,5))return false;}
    else if(!quad(0,1,10,9)||!add({0,9,5})||!quad(9,10,6,5)||!add({10,1,6}))return false;
    if(!quad(1,2,7,6)||!add({2,3,7})||!quad(3,4,8,7)||!quad(4,0,5,8))return false;
    weight=std::clamp(weight,0.0,1.0);
    for(int i=0;i<result.vertices;++i)result.common[i]=Blend(result.first[i],result.second[i],weight);
    mesh=result;return true;
}
inline std::uint32_t Sample(const std::uint32_t* source,int width,int height,Point uv) {
    if(uv.x<0||uv.x>1||uv.y<0||uv.y>1)return 0;
    return deform::detail::Sample(source,width,height,width*4,uv);
}
// The whole source is inverse-rasterized through a validated shared topology.
// Corresponding eyes/mouth/ears meet exactly; neck/body pixels stay connected,
// and there is no separately cut/pasted head or fixed-time texture switch.
inline bool Raster(const std::uint32_t* source,int width,int height,
                   std::uint32_t* destination,int canvasWidth,int canvasHeight,
                   Geometry size,const Mesh& mesh,bool second,Point origin,bool flip=false) {
    if(!source||!destination||source==destination||width<1||height<1||width>4096||height>4096||
        canvasWidth<1||canvasHeight<1||canvasWidth>4096||canvasHeight>4096||
        size.width<=0||size.height<=0||!std::isfinite(size.width)||!std::isfinite(size.height)||
        !std::isfinite(origin.x)||!std::isfinite(origin.y)||std::abs(origin.x)>32768||std::abs(origin.y)>32768||
        mesh.count<1||mesh.count>25||mesh.vertices<1||mesh.vertices>16)return false;
    for(int i=0;i<mesh.vertices;++i)for(const auto point:{mesh.first[i],mesh.second[i],mesh.common[i]})
        if(!std::isfinite(point.x)||!std::isfinite(point.y)||std::abs(point.x)>100000||std::abs(point.y)>100000)return false;
    for(int i=0;i<mesh.count;++i){const auto triangle=mesh.triangles[i];
        for(int index:{triangle.a,triangle.b,triangle.c})if(index<0||index>=mesh.vertices)return false;
        if(std::abs(Area(mesh.common[triangle.a],mesh.common[triangle.b],mesh.common[triangle.c]))<1e-8)return false;
    }
    std::fill_n(destination,static_cast<std::size_t>(canvasWidth)*canvasHeight,0);
    const auto& native=second?mesh.second:mesh.first;
    const auto placed=[&](Point p){return Point{origin.x+(flip?-p.x:p.x),origin.y+p.y};};
    for(int i=0;i<mesh.count;++i){
        const auto tri=mesh.triangles[i];
        const auto a=placed(mesh.common[tri.a]),b=placed(mesh.common[tri.b]),c=placed(mesh.common[tri.c]);
        const double area=Area(a,b,c);
        if(std::abs(area)<1e-8)return false;
        const int x0=std::clamp(static_cast<int>(std::floor(std::min({a.x,b.x,c.x}))),0,canvasWidth);
        const int x1=std::clamp(static_cast<int>(std::ceil(std::max({a.x,b.x,c.x}))),0,canvasWidth);
        const int y0=std::clamp(static_cast<int>(std::floor(std::min({a.y,b.y,c.y}))),0,canvasHeight);
        const int y1=std::clamp(static_cast<int>(std::ceil(std::max({a.y,b.y,c.y}))),0,canvasHeight);
        for(int y=y0;y<y1;++y)for(int x=x0;x<x1;++x){
            const Point p{x+.5,y+.5};
            const double wa=Area(b,c,p)/area,wb=Area(c,a,p)/area,wc=1-wa-wb;
            if(wa < -1e-10||wb < -1e-10||wc < -1e-10)continue;
            const Point point{native[tri.a].x*wa+native[tri.b].x*wb+native[tri.c].x*wc,
                              native[tri.a].y*wa+native[tri.b].y*wb+native[tri.c].y*wc};
            destination[y*canvasWidth+x]=Sample(source,width,height,{point.x/size.width+.5,point.y/size.height+1});
        }
    }
    return true;
}

// Close only the registered eye regions of a reclining source. The existing
// closed-eye texture supplies species fur/lids; its eye axis rotates with the
// native reclining head. Body, neck, ears and silhouette remain source pixels.
inline bool CloseEyes(const std::uint32_t* source,int width,int height,std::uint32_t* destination,
                      Character character,int frame,double blink,const expression::ClosedEyes& closed) {
    if(!source||!destination||source==destination||!closed.pixels||closed.width<1||closed.height<1||
        width<1||height<1||width>4096||height>4096||closed.width>4096||closed.height>4096||
        (frame!=24&&frame!=25)||!IsValid(character)||!std::isfinite(blink))return false;
    blink=std::clamp(blink,0.0,1.0);
    std::memcpy(destination,source,static_cast<std::size_t>(width)*height*4);
    if(blink==0)return true;
    const auto face=FaceFor(character,frame);
    const std::array<Point,2> eyes{{{face.leftEye.x*width,face.leftEye.y*height},{face.rightEye.x*width,face.rightEye.y*height}}};
    const std::array<Point,2> lids{{{closed.landmarks.leftEye.x*closed.width,closed.landmarks.leftEye.y*closed.height},
                                 {closed.landmarks.rightEye.x*closed.width,closed.landmarks.rightEye.y*closed.height}}};
    const double distance=std::hypot(eyes[1].x-eyes[0].x,eyes[1].y-eyes[0].y);
    const double lidDistance=std::hypot(lids[1].x-lids[0].x,lids[1].y-lids[0].y);
    if(distance<1||lidDistance<1)return false;
    const Point axis{(eyes[1].x-eyes[0].x)/distance,(eyes[1].y-eyes[0].y)/distance};
    const Point lidAxis{(lids[1].x-lids[0].x)/lidDistance,(lids[1].y-lids[0].y)/lidDistance};
    const double ratio=lidDistance/distance,rx=.48*distance,ry=.48*distance;
    const double radius=std::max(rx,ry);
    for(std::size_t eye=0;eye<2;++eye) {
        const int left=std::max(0,static_cast<int>(std::floor(eyes[eye].x-radius)));
        const int right=std::min(width,static_cast<int>(std::ceil(eyes[eye].x+radius)));
        const int top=std::max(0,static_cast<int>(std::floor(eyes[eye].y-radius)));
        const int bottom=std::min(height,static_cast<int>(std::ceil(eyes[eye].y+radius)));
        for(int y=top;y<bottom;++y)for(int x=left;x<right;++x) {
            const double dx=x+.5-eyes[eye].x,dy=y+.5-eyes[eye].y;
            const double u=dx*axis.x+dy*axis.y,v=-dx*axis.y+dy*axis.x;
            const double ellipse=u*u/(rx*rx)+v*v/(ry*ry);
            if(ellipse>=1||(source[y*width+x]>>24)<245)continue;
            const double edge=deform::Smooth((1-ellipse)/.25);
            const double aperture=(1-blink)*.95;
            const double cover=(blink>.98?1:deform::Smooth((std::abs(v/ry)-aperture)/.14))*edge;
            const Point sample{(lids[eye].x+ratio*(u*lidAxis.x-v*lidAxis.y))/closed.width,
                               (lids[eye].y+ratio*(u*lidAxis.y+v*lidAxis.x))/closed.height};
            const auto lid=Sample(closed.pixels,closed.width,closed.height,sample);
            const unsigned alpha=lid>>24;
            if(alpha>245)destination[y*width+x]=expression::Over(source[y*width+x],
                ((lid>>16)&255)*255/alpha,((lid>>8)&255)*255/alpha,(lid&255)*255/alpha,cover);
        }
    }
    return true;
}
} // namespace cat::recline
