#pragma once
#include "cat_gait.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

namespace cat::articulated {
struct Point { double x=0,y=0; };
inline Point operator+(Point a,Point b){return {a.x+b.x,a.y+b.y};}
inline Point operator-(Point a,Point b){return {a.x-b.x,a.y-b.y};}
inline Point operator*(Point a,double b){return {a.x*b,a.y*b};}
inline double Length(Point a){return std::hypot(a.x,a.y);}
inline double Smooth(double x){x=std::clamp(x,0.,1.);return x*x*(3-2*x);}
struct SolvedLeg { Point root,knee,ankle,paw,targetPaw; double upperLength=0,lowerLength=0; bool clamped=false; };
class Rig {
public:
 bool Prepare(const std::uint32_t* src,int w,int h,int stride){
  ready_=false;
  if(!src||w<32||h<32||w>2048||h>2048||stride<w*4||stride>65536)return false;
  w_=w;h_=h;pad_=static_cast<int>(std::ceil(w*.25));
  source_.resize(w*h);body_.resize(w*h);out_.resize(Width()*h);
  for(int y=0;y<h;++y)std::memcpy(source_.data()+y*w,reinterpret_cast<const unsigned char*>(src)+y*stride,w*4);
  // Preserve the entire head, ears, back and tail. Beneath it a rounded belly
  // plate occludes the separately articulated roots and far-side thighs.
  for(int y=0;y<h;++y)for(int x=0;x<w;++x){
   double u=(x+.5)/w,v=(y+.5)/h;
   const double ellipse=.68+.19*std::sqrt(std::max(0.,1-std::pow((u-.38)/.27,2)));
   const double chest=.84-.11*Smooth((u-.63)/.13);
   const double front=ellipse*(1-Smooth((u-.48)/.12))+chest*Smooth((u-.48)/.12);
   const double edge=std::min(front,.69+.18*Smooth((u-.12)/.14));
   auto pixel=source_[y*w+x];
   const double coverage=1-Smooth((v-edge+.008)/.016);
   // Missing hidden belly pixels are copied from the same fur column above;
   // no face pixel is synthesized and no rectangular source patch is drawn.
   if(v>.70&&v<edge&&u>.19&&u<.54&&(pixel>>24)<200){
    int sample=y;
    while(sample>static_cast<int>(h*.68)&&(source_[sample*w+x]>>24)<230)--sample;
    if((source_[sample*w+x]>>24)>=230)pixel=source_[sample*w+x];
   }
   body_[y*w+x]=Multiply(pixel,coverage);
  }
  for(int i=0;i<2;++i){auto& leg=leg_[i];leg.pixels.resize(w*h);
   for(int y=0;y<h;++y)for(int x=0;x<w;++x){
    const double u=(x+.5)/w,v=(y+.5)/h;
    double mask;
    if(i==0)mask=Smooth((u-.115)/.025)*(1-Smooth((u-.29)/.035))*Smooth((v-.69)/.035);
    else {double left=.49+.14*Smooth((v-.72)/.23);double right=.69+.13*Smooth((v-.72)/.23);
     mask=Smooth((u-left)/.025)*(1-Smooth((u-right)/.025))*Smooth((v-.775)/.035);}
    leg.pixels[y*w+x]=Multiply(source_[y*w+x],mask);
   }
  }
  for(int i=0;i<2;++i){
   int bottom=-1;for(int y=0;y<h;++y)for(int x=0;x<w;++x)if((leg_[i].pixels[y*w+x]>>24)>153)bottom=std::max(bottom,y);
   if(bottom<0)return false;
   int left=w,right=0;for(int y=std::max(0,bottom-3);y<=bottom;++y)for(int x=0;x<w;++x)if((leg_[i].pixels[y*w+x]>>24)>153){left=std::min(left,x);right=std::max(right,x);}
   soles_[i]={(left+right+1)/2.0,static_cast<double>(bottom+1)};
  }
  ready_=true;return true;
 }
 bool Raster(const cat::gait::Feet& feet,double widthBody,double heightBody){
  if(!ready_||!std::isfinite(widthBody)||!std::isfinite(heightBody)||widthBody<10||heightBody<10||widthBody>4096||heightBody>4096)return false;
  for(const auto& f:feet)if(!std::isfinite(f.x)||!std::isfinite(f.y)||std::abs(f.x)>60||f.y>0||f.y < -12||(f.stance&&f.y!=0))return false;
  std::fill(out_.begin(),out_.end(),0);
  maxReachError_=0;
  for(int i:{1,3})if(!DrawLeg(i,feet[i],widthBody,heightBody))return false;
  if(!DrawLeg(2,feet[2],widthBody,heightBody))return false;
  // The original belly/chest plate covers all four roots. Near limbs emerge
  // from beneath the silhouette rather than cutting a moving V into the neck.
  for(int y=0;y<h_;++y)for(int x=0;x<w_;++x)out_[y*Width()+x+pad_]=Over(body_[y*w_+x],out_[y*Width()+x+pad_]);
  if(!DrawLeg(0,feet[0],widthBody,heightBody))return false;
  // Copy the registered head/back exactly, eliminating all seam overdraw.
  for(int y=0;y<static_cast<int>(h_*.68);++y)std::memcpy(out_.data()+y*Width()+pad_,source_.data()+y*w_,w_*4);
  return true;
 }
 int Width()const{return w_+2*pad_;}int Height()const{return h_;}int PaddingX()const{return pad_;}
 const std::uint32_t* Pixels()const{return out_.data();}
 double ReachError()const{return maxReachError_;}
 const std::array<SolvedLeg,4>& Legs()const{return solutions_;}
private:
 struct Layer{std::vector<std::uint32_t> pixels;};
 std::array<Layer,2> leg_;
 std::array<Point,2> soles_{};
 std::array<SolvedLeg,4> solutions_{};
 std::vector<std::uint32_t> source_,body_,out_;
 int w_=0,h_=0,pad_=0;bool ready_=false;double maxReachError_=0;
 static std::uint32_t Multiply(std::uint32_t p,double a){a=std::clamp(a,0.,1.);std::uint32_t q=0;for(int s=0;s<32;s+=8)q|=static_cast<unsigned>(std::lround(((p>>s)&255)*a))<<s;return q;}
 static std::uint32_t Over(std::uint32_t f,std::uint32_t b){unsigned inv=255-(f>>24);std::uint32_t o=0;for(int s=0;s<32;s+=8)o|=std::min(255u,((f>>s)&255)+(((b>>s)&255)*inv+127)/255)<<s;return o;}
 static Point Rotate(Point p,double angle){return {p.x*std::cos(angle)-p.y*std::sin(angle),p.x*std::sin(angle)+p.y*std::cos(angle)};}
 static double Angle(Point p){return std::atan2(p.y,p.x);}
 std::uint32_t Sample(const std::vector<std::uint32_t>& pixels,double x,double y)const{
  x-=.5;y-=.5;int x0=static_cast<int>(std::floor(x)),y0=static_cast<int>(std::floor(y));double fx=x-x0,fy=y-y0;
  auto at=[&](int a,int b){return a<0||b<0||a>=w_||b>=h_?0u:pixels[b*w_+a];};
  std::array<std::uint32_t,4> p{{at(x0,y0),at(x0+1,y0),at(x0,y0+1),at(x0+1,y0+1)}};
  std::array<double,4> weights{{(1-fx)*(1-fy),fx*(1-fy),(1-fx)*fy,fx*fy}};std::uint32_t q=0;
  for(int s=0;s<32;s+=8){double v=0;for(int i=0;i<4;++i)v+=((p[i]>>s)&255)*weights[i];q|=static_cast<unsigned>(std::lround(v))<<s;}return q;
 }
 void Triangle(Point a,Point b,Point c,Point sa,Point sb,Point sc,const std::vector<std::uint32_t>& pixels,double tint){
  double den=(b.y-c.y)*(a.x-c.x)+(c.x-b.x)*(a.y-c.y);if(std::abs(den)<1e-8)return;
  int left=std::max(0,static_cast<int>(std::floor(std::min({a.x,b.x,c.x}))));int right=std::min(Width(),static_cast<int>(std::ceil(std::max({a.x,b.x,c.x}))));
  int top=std::max(0,static_cast<int>(std::floor(std::min({a.y,b.y,c.y}))));int bottom=std::min(h_,static_cast<int>(std::ceil(std::max({a.y,b.y,c.y}))));
  for(int y=top;y<bottom;++y)for(int x=left;x<right;++x){Point p{x+.5,y+.5};double u=((b.y-c.y)*(p.x-c.x)+(c.x-b.x)*(p.y-c.y))/den;double v=((c.y-a.y)*(p.x-c.x)+(a.x-c.x)*(p.y-c.y))/den;double z=1-u-v;
   if(u<0||v<0||z<0)continue;
   Point source=sa*u+sb*v+sc*z;auto q=Sample(pixels,source.x,source.y);
   if(tint<1){unsigned alpha=q&0xff000000u;std::uint32_t rgb=0;for(int s=0;s<24;s+=8)rgb|=static_cast<unsigned>(((q>>s)&255)*tint)<<s;q=alpha|rgb;}
   out_[y*Width()+x]=Over(q,out_[y*Width()+x]);
  }
 }
 bool DrawLeg(int index,const cat::gait::Foot& foot,double widthBody,double heightBody){
  if(!std::isfinite(foot.x)||!std::isfinite(foot.y)||foot.y>0||foot.y < -12)return false;
  const bool fore=index>=2,farSide=(index&1)!=0;const int sourceIndex=fore?1:0;
  Point root=fore?Point{.60*w_,.735*h_}:Point{.245*w_,.75*h_};
  Point joint=fore?Point{.625*w_,.85*h_}:Point{.185*w_,.86*h_};
  Point ankle=fore?Point{.69*w_,.932*h_}:Point{.205*w_,.93*h_};
  const Point sole=soles_[sourceIndex];
  const double size=farSide?.86:1;
  Point targetRoot=root;
  targetRoot.y+=h_-2-sole.y;
  if(farSide){targetRoot.x+=(fore?-.10:.16)*w_;targetRoot.y+=(h_-2-targetRoot.y)*(1-size)+2;}
  Point targetSole{targetRoot.x+foot.x/widthBody*w_,h_-2+foot.y/heightBody*h_};
  Point targetAnkle=targetSole-(sole-ankle)*size;
  double upper=Length(joint-root)*size,lower=Length(ankle-joint)*size;
  Point direction=targetAnkle-targetRoot;double distance=Length(direction),reach=upper+lower;
  const double minimum=std::abs(upper-lower)+.01;
  const double reachable=std::clamp(distance,minimum,reach-.01);
  const bool clamped=reachable!=distance;
  if(clamped){
   maxReachError_=std::max(maxReachError_,std::abs(distance-reachable)*heightBody/h_);
   if(distance<1e-9)direction={0,1};else direction=direction*(1/distance);
   targetAnkle=targetRoot+direction*reachable;direction=targetAnkle-targetRoot;distance=reachable;
  }
  double projection=(upper*upper-lower*lower+distance*distance)/(2*std::max(.01,distance));
  double bend=std::sqrt(std::max(0.,upper*upper-projection*projection));Point unit=direction*(1/std::max(.01,distance));
  const double side=fore?1:-1;Point targetJoint=targetRoot+unit*projection+Point{-unit.y,unit.x}*(side*bend);
  solutions_[index]={targetRoot,targetJoint,targetAnkle,targetAnkle+(sole-ankle)*size,targetSole,upper,lower,clamped};
  double angleUpper=Angle(targetJoint-targetRoot)-Angle(joint-root),angleLower=Angle(targetAnkle-targetJoint)-Angle(ankle-joint);
  auto transform=[&](Point p){double nearKnee=Smooth((p.y-joint.y+8)/16);double nearPaw=Smooth((p.y-ankle.y+4)/8);
   Point a=targetRoot+Rotate((p-root)*size,angleUpper),b=targetJoint+Rotate((p-joint)*size,angleLower),c=targetAnkle+(p-ankle)*size;
   return (a*(1-nearKnee)+b*nearKnee)*(1-nearPaw)+c*nearPaw+Point{static_cast<double>(pad_),0};};
  // Piecewise rigid bones with a narrow skin blend at real joints. The paw
  // itself translates as a rigid painted patch, without shear or scale pulse.
  constexpr int cols=12,rows=25;double left=(fore?.47:.10)*w_,right=(fore?.84:.34)*w_,top=(fore?.66:.68)*h_;
  for(int y=0;y<rows;++y)for(int x=0;x<cols;++x){Point a{left+(right-left)*x/cols,top+(h_-top)*y/rows};Point b{left+(right-left)*(x+1)/cols,a.y};Point c{a.x,top+(h_-top)*(y+1)/rows};Point d{b.x,c.y};
   Triangle(transform(a),transform(b),transform(c),a,b,c,leg_[sourceIndex].pixels,farSide?.86:1);
   Triangle(transform(b),transform(d),transform(c),b,d,c,leg_[sourceIndex].pixels,farSide?.86:1);
  }
  return true;
 }
};
}
