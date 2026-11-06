#pragma once
#include "cat_gait.hpp"
#include "cat_walk_dynamics.hpp"
#include "cat_tail_pose.hpp"
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
  source_.resize(w*h);body_.resize(w*h);out_.resize(Width()*Height());
  for(auto& cache:tailCache_){cache.valid=false;cache.pixels.resize(Width()*h);}
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
  for(int y=0;y<static_cast<int>(h*.68);++y)
   std::memcpy(body_.data()+y*w,source_.data()+y*w,w*4);
  runningBody_.resize(w*h);tail_.resize(w*h);
  for(int y=0;y<h;++y)for(int x=0;x<w;++x) {
   const auto parts=cat::tail_pose::Split(body_[y*w+x],(x+.5)/w,(y+.5)/h);
   runningBody_[y*w+x]=parts.body;tail_[y*w+x]=parts.tail;
  }
  bodyBounds_=PixelBounds(body_,w,h,&bodyRows_);tailBounds_=PixelBounds(tail_,w,h);
  for(int i=0;i<2;++i){auto& leg=leg_[i];leg.pixels.resize(w*h);
   for(int y=0;y<h;++y)for(int x=0;x<w;++x){
    const double u=(x+.5)/w,v=(y+.5)/h;
    double mask;
    if(i==0)mask=Smooth((u-.115)/.025)*(1-Smooth((u-.29)/.035))*Smooth((v-.69)/.035);
    else {double left=.49+.14*Smooth((v-.72)/.23);double right=.69+.13*Smooth((v-.72)/.23);
     mask=Smooth((u-left)/.025)*(1-Smooth((u-right)/.025))*Smooth((v-.775)/.035);}
    // Exclude neighbouring belly/far-leg fur from the calf. A wide row
    // strip rotated that unrelated patch below the heel when the hock bent.
    // Release this constraint at the painted paw so its round toes stay whole.
    const double jointY=i==0?.86:.85,rootY=i==0?.75:.735,ankleY=i==0?.93:.932;
    const double rootX=i==0?.245:.60,jointX=i==0?.185:.625,ankleX=i==0?.205:.69;
    const double upper=std::clamp((v-rootY)/(jointY-rootY),0.,1.);
    const double lower=std::clamp((v-jointY)/(ankleY-jointY),0.,1.);
    const double center=v<jointY?rootX+(jointX-rootX)*upper:jointX+(ankleX-jointX)*lower;
    const double radius=v<jointY?(i==0?.085:.075)+((i==0?.050:.045)-(i==0?.085:.075))*upper
                                 :(i==0?.050:.045)+(.030-(i==0?.050:.045))*lower;
    const double tube=1-Smooth((std::abs(u-center)-radius)/.018);
    const double paw=Smooth((v-.925)/.025);
    mask*=tube*(1-paw)+paw;
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
 bool Raster(const cat::gait::Feet& feet,double widthBody,double heightBody,
             const cat::walk_dynamics::Frame& dynamics={}){
  if(!ready_||!std::isfinite(widthBody)||!std::isfinite(heightBody)||widthBody<10||heightBody<10||widthBody>4096||heightBody>4096)return false;
  for(const auto& f:feet)if(!std::isfinite(f.x)||!std::isfinite(f.y)||std::abs(f.x)>60||f.y>0||f.y < -12||(f.stance&&f.y!=0))return false;
  if(!cat::walk_dynamics::Finite(dynamics) || dynamics.pivotU<0 || dynamics.pivotU>1 ||
     dynamics.pivotV<0 || dynamics.pivotV>1.5 || std::abs(dynamics.torsoX)>4||
     std::abs(dynamics.torsoY)>8||std::abs(dynamics.pitchDegrees)>5 ||
     std::abs(dynamics.spineStretch)>.08 || dynamics.tailDegrees < -70 || dynamics.tailDegrees>15 ||
     dynamics.airborneWeight<0 || dynamics.airborneWeight>1)return false;
  for(const auto& offset:dynamics.rootOffsets)
   if(!std::isfinite(offset.x)||!std::isfinite(offset.y)||std::abs(offset.x)>4||std::abs(offset.y)>4)return false;
  dynamics_=dynamics;
  std::fill(out_.begin(),out_.end(),0);
  maxReachError_=0;
  for(int i:{1,3})if(!DrawLeg(i,feet[i],widthBody,heightBody))return false;
  if(!DrawLeg(2,feet[2],widthBody,heightBody))return false;
  // The original belly/chest plate covers all four roots. Near limbs emerge
  // from beneath the silhouette rather than cutting a moving V into the neck.
  if(cat::walk_dynamics::Neutral(dynamics_)) {
   for(int y=0;y<h_;++y)for(int x=0;x<w_;++x)out_[y*Width()+x+pad_]=Over(body_[y*w_+x],out_[y*Width()+x+pad_]);
  } else {
   // Rigidly move the torso before solving limbs. Paw goals stay in the
   // ground frame, so a shoulder/hip shift cannot drag a supporting foot.
   const double radians=dynamics_.pitchDegrees*cat::walk_dynamics::kPi/180;
   const double cosine=std::cos(radians),sine=std::sin(radians);
   const Point pivot{widthBody*dynamics_.pivotU,heightBody*dynamics_.pivotV};
   const auto* torso=dynamics_.tailDegrees==0?nullptr:&CachedTorso(dynamics_.tailDegrees,widthBody,heightBody);
   auto bounds=torso?torso->bounds:bodyBounds_;
   if(torso){bounds.left-=pad_;bounds.right-=pad_;}
   const auto destination=BodyBounds(bounds,widthBody,heightBody,cosine,sine);
   const auto& rows=torso?torso->rows:bodyRows_;
   const bool stretched=dynamics_.spineStretch!=0;
   for(int y=destination.top;y<destination.bottom;++y) {
    // For a destination scanline the inverse rigid Y is affine in X;
    // lower-spine deformation affects only X. Union the source alpha spans
    // touched by that Y interval and conservatively project their rectangle.
    const double targetY=(y+.5)*heightBody/h_-pivot.y-dynamics_.torsoY;
    const auto sourceYAt=[&](int x){
     const double targetX=(x-pad_+.5)*widthBody/w_-pivot.x-dynamics_.torsoX;
     return (pivot.y-targetX*sine+targetY*cosine)*h_/heightBody;
    };
    const double firstY=sourceYAt(destination.left),lastY=sourceYAt(destination.right-1);
    const int first=std::max(0,static_cast<int>(std::floor(std::min(firstY,lastY)-.5))-1);
    const int last=std::min(h_,static_cast<int>(std::ceil(std::max(firstY,lastY)+.5))+1);
    int sourceLeft=torso?Width():w_,sourceRight=0;
    for(int row=first;row<last;++row){sourceLeft=std::min(sourceLeft,rows[row][0]);sourceRight=std::max(sourceRight,rows[row][1]);}
    if(sourceLeft>=sourceRight)continue;
    const int padding=torso?pad_:0;
    const auto span=BodyBounds({sourceLeft-padding,first,sourceRight-padding,last},widthBody,heightBody,cosine,sine);
    const int left=std::max(destination.left,span.left),right=std::min(destination.right,span.right);
    for(int x=left;x<right;++x) {
    const Point target{(x-pad_+.5)*widthBody/w_-pivot.x-dynamics_.torsoX,
                       (y+.5)*heightBody/h_-pivot.y-dynamics_.torsoY};
    const Point unrotated{pivot.x+target.x*cosine+target.y*sine,
                          pivot.y-target.x*sine+target.y*cosine};
    const Point source{(stretched?cat::walk_dynamics::UnstretchX(unrotated.x,unrotated.y,dynamics_,widthBody,heightBody):pivot.x+(unrotated.x-pivot.x))*w_/widthBody,
                       unrotated.y*h_/heightBody};
    const auto pixel=torso?SampleSize(torso->pixels,Width(),h_,source.x+pad_,source.y):Sample(body_,source.x,source.y);
    if(pixel>>24)out_[y*Width()+x]=Over(pixel,out_[y*Width()+x]);
    }
   }
  }
  if(!DrawLeg(0,feet[0],widthBody,heightBody))return false;
  // Copy the registered head/back exactly, eliminating all seam overdraw.
  if(cat::walk_dynamics::Neutral(dynamics_))
   for(int y=0;y<static_cast<int>(h_*.68);++y)std::memcpy(out_.data()+y*Width()+pad_,source_.data()+y*w_,w_*4);
  return true;
 }
 int Width()const{return w_+2*pad_;}int Height()const{return h_+8;}int PaddingX()const{return pad_;}
 int SourceHeight()const{return h_;}int GroundInsetPixels()const{return 10;}
 const std::uint32_t* Pixels()const{return out_.data();}
 double ReachError()const{return maxReachError_;}
 const std::array<SolvedLeg,4>& Legs()const{return solutions_;}
private:
 struct Bounds {int left=0,top=0,right=0,bottom=0;};
 struct Layer{std::vector<std::uint32_t> pixels;};
 std::array<Layer,2> leg_;
 std::array<Point,2> soles_{};
 std::array<SolvedLeg,4> solutions_{};
 cat::walk_dynamics::Frame dynamics_{};
 std::vector<std::uint32_t> source_,body_,runningBody_,tail_,out_;
 Bounds bodyBounds_{},tailBounds_{};
 std::vector<std::array<int,2>> bodyRows_;
 struct TailCache {std::vector<std::uint32_t> pixels;std::vector<std::array<int,2>> rows;Bounds bounds;double angle=0,ratio=0;bool valid=false;};
 std::array<TailCache,2> tailCache_;
 unsigned nextTailCache_=0;
 int w_=0,h_=0,pad_=0;bool ready_=false;double maxReachError_=0;
 // Exact lround for nonnegative byte-channel arithmetic. Subtracting the
 // integer part avoids the rare (v+.5) half-ULP rounding ambiguity.
 static unsigned RoundChannel(double value){const auto integer=static_cast<unsigned>(value);return integer+static_cast<unsigned>(value-integer>=.5);}
 static std::uint32_t Multiply(std::uint32_t p,double a){a=std::clamp(a,0.,1.);std::uint32_t q=0;for(int s=0;s<32;s+=8)q|=RoundChannel(((p>>s)&255)*a)<<s;return q;}
 static std::uint32_t Over(std::uint32_t f,std::uint32_t b){if(f==0)return b;if((f>>24)==255||b==0)return f;unsigned inv=255-(f>>24);std::uint32_t o=0;for(int s=0;s<32;s+=8)o|=std::min(255u,((f>>s)&255)+(((b>>s)&255)*inv+127)/255)<<s;return o;}
 static Point Rotate(Point p,double angle){return {p.x*std::cos(angle)-p.y*std::sin(angle),p.x*std::sin(angle)+p.y*std::cos(angle)};}
 static double Angle(Point p){return std::atan2(p.y,p.x);}
 static Bounds PixelBounds(const std::vector<std::uint32_t>& pixels,int width,int height,
                           std::vector<std::array<int,2>>* spans=nullptr) {
  Bounds result{width,height,0,0};
  if(spans)spans->assign(height,{{width,0}});
  for(int y=0;y<height;++y){
   int left=width,right=0;
   for(int x=0;x<width;++x)if(pixels[y*width+x]>>24){left=std::min(left,x);right=x+1;}
   if(spans)(*spans)[y]={{left,right}};
   if(left<right){result.left=std::min(result.left,left);result.right=std::max(result.right,right);result.top=std::min(result.top,y);result.bottom=y+1;}
  }
  return result;
 }
 Bounds BodyBounds(Bounds source,double widthBody,double heightBody,double cosine,double sine) const {
  const double pivotX=widthBody*dynamics_.pivotU,pivotY=heightBody*dynamics_.pivotV;
  double x0=(source.left-.5)*widthBody/w_,x1=(source.right+.5)*widthBody/w_;
  const double y0=(source.top-.5)*heightBody/h_,y1=(source.bottom+.5)*heightBody/h_;
  // SpineWeight stays in [0,1]. Enclose every lower-spine deformation in
  // this expanded rectangle before applying the rigid body affine.
  const double expansion=std::abs(dynamics_.spineStretch)*std::max(std::abs(x0-pivotX),std::abs(x1-pivotX));
  x0-=expansion;x1+=expansion;
  double left=1e20,right=-1e20,top=1e20,bottom=-1e20;
  for(double x:{x0,x1})for(double y:{y0,y1}){
   const double dx=x-pivotX,dy=y-pivotY;
   const double targetX=(pivotX+dx*cosine-dy*sine+dynamics_.torsoX)*w_/widthBody+pad_;
   const double targetY=(pivotY+dx*sine+dy*cosine+dynamics_.torsoY)*h_/heightBody;
   left=std::min(left,targetX);right=std::max(right,targetX);top=std::min(top,targetY);bottom=std::max(bottom,targetY);
  }
  return {std::max(0,static_cast<int>(std::floor(left))-1),std::max(0,static_cast<int>(std::floor(top))-1),
          std::min(Width(),static_cast<int>(std::ceil(right))+1),std::min(Height(),static_cast<int>(std::ceil(bottom))+1)};
 }
 const TailCache& CachedTorso(double degrees,double widthBody,double heightBody) {
  const double ratio=widthBody/heightBody;
  for(const auto& cache:tailCache_)
   if(cache.valid && cache.angle==degrees && cache.ratio==ratio)return cache;
  auto& cache=tailCache_[nextTailCache_++%tailCache_.size()];
  const double angle=degrees*cat::walk_dynamics::kPi/180;
  const double cosine=std::cos(angle),sine=std::sin(angle);
  std::fill(cache.pixels.begin(),cache.pixels.end(),0);
  // The cached torso is sampled on its original integer pixel grid. Copying
  // the body is identical to bilinear sampling at each pixel centre.
  for(int y=0;y<h_;++y)std::memcpy(cache.pixels.data()+y*Width()+pad_,runningBody_.data()+y*w_,w_*4);
  const double px=cat::tail_pose::kPivot.x*widthBody,py=cat::tail_pose::kPivot.y*heightBody;
  double left=1e20,right=-1e20,top=1e20,bottom=-1e20;
  for(double x:{(tailBounds_.left-.5)*widthBody/w_,(tailBounds_.right+.5)*widthBody/w_})
  for(double y:{(tailBounds_.top-.5)*heightBody/h_,(tailBounds_.bottom+.5)*heightBody/h_}){
   const double dx=x-px,dy=y-py;
   const double targetX=(px+dx*cosine-dy*sine)*w_/widthBody+pad_;
   const double targetY=(py+dx*sine+dy*cosine)*h_/heightBody;
   left=std::min(left,targetX);right=std::max(right,targetX);top=std::min(top,targetY);bottom=std::max(bottom,targetY);
  }
  const Bounds region{std::max(0,static_cast<int>(std::floor(left))-1),std::max(0,static_cast<int>(std::floor(top))-1),
                      std::min(Width(),static_cast<int>(std::ceil(right))+1),std::min(h_,static_cast<int>(std::ceil(bottom))+1)};
  for(int y=region.top;y<region.bottom;++y)for(int x=region.left;x<region.right;++x) {
   const double u=(x-pad_+.5)/w_,v=(y+.5)/h_;
   const double localX=(u-cat::tail_pose::kPivot.x)*widthBody;
   const double localY=(v-cat::tail_pose::kPivot.y)*heightBody;
   const double sourceU=cat::tail_pose::kPivot.x+(localX*cosine+localY*sine)/widthBody;
   const double sourceV=cat::tail_pose::kPivot.y+(-localX*sine+localY*cosine)/heightBody;
   auto& pixel=cache.pixels[y*Width()+x];pixel=Over(pixel,Sample(tail_,sourceU*w_,sourceV*h_));
  }
  cache.bounds=PixelBounds(cache.pixels,Width(),h_,&cache.rows);
  cache.angle=degrees;cache.ratio=ratio;cache.valid=true;
  return cache;
 }
 std::uint32_t Sample(const std::vector<std::uint32_t>& pixels,double x,double y)const{
  return SampleSize(pixels,w_,h_,x,y);
 }
 static std::uint32_t SampleSize(const std::vector<std::uint32_t>& pixels,int width,int height,double x,double y){
  if(x<-.5 || x>width+.5 || y<-.5 || y>height+.5)return 0;
  x-=.5;y-=.5;int x0=static_cast<int>(std::floor(x)),y0=static_cast<int>(std::floor(y));double fx=x-x0,fy=y-y0;
  auto at=[&](int a,int b){return a<0||b<0||a>=width||b>=height?0u:pixels[b*width+a];};
  std::array<std::uint32_t,4> p{{at(x0,y0),at(x0+1,y0),at(x0,y0+1),at(x0+1,y0+1)}};
  if((p[0]|p[1]|p[2]|p[3])==0)return 0;
  std::array<double,4> weights{{(1-fx)*(1-fy),fx*(1-fy),(1-fx)*fy,fx*fy}};std::uint32_t q=0;
  for(int s=0;s<32;s+=8){double v=0;for(int i=0;i<4;++i)v+=((p[i]>>s)&255)*weights[i];q|=RoundChannel(v)<<s;}return q;
 }
 void Triangle(Point a,Point b,Point c,Point sa,Point sb,Point sc,const std::vector<std::uint32_t>& pixels,double tint){
  double den=(b.y-c.y)*(a.x-c.x)+(c.x-b.x)*(a.y-c.y);if(std::abs(den)<1e-8)return;
  int left=std::max(0,static_cast<int>(std::floor(std::min({a.x,b.x,c.x}))));int right=std::min(Width(),static_cast<int>(std::ceil(std::max({a.x,b.x,c.x}))));
  int top=std::max(0,static_cast<int>(std::floor(std::min({a.y,b.y,c.y}))));int bottom=std::min(Height(),static_cast<int>(std::ceil(std::max({a.y,b.y,c.y}))));
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
  const Point baseRoot=targetRoot;
  const auto moved=cat::walk_dynamics::RootPoint({targetRoot.x*widthBody/w_,targetRoot.y*heightBody/h_},
      static_cast<unsigned>(index),dynamics_,widthBody,heightBody);
  targetRoot={moved.x*w_/widthBody,moved.y*h_/heightBody};
  Point targetSole{baseRoot.x+foot.x/widthBody*w_,h_-2+foot.y/heightBody*h_};
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
  auto transform=[&](Point p){double nearKnee=Smooth((p.y-joint.y+8)/16);double nearPaw=Smooth((p.y-ankle.y+12)/8);
   Point a=targetRoot+Rotate((p-root)*size,angleUpper),b=targetJoint+Rotate((p-joint)*size,angleLower),c=targetAnkle+(p-ankle)*size;
   return (a*(1-nearKnee)+b*nearKnee)*(1-nearPaw)+c*nearPaw+Point{static_cast<double>(pad_),0};};
  // Piecewise rigid bones with a narrow skin blend at real joints. The paw
  // itself translates as a rigid painted patch, without shear or scale pulse.
  constexpr int cols=12,rows=25;double left=(fore?.47:.10)*w_,right=(fore?.84:.34)*w_,top=(fore?.66:.68)*h_;
  std::array<Point,(cols+1)*(rows+1)> sourcePoints{},targetPoints{};
  for(int y=0;y<=rows;++y)for(int x=0;x<=cols;++x){
   const int vertex=y*(cols+1)+x;
   sourcePoints[vertex]={left+(right-left)*x/cols,top+(h_-top)*y/rows};targetPoints[vertex]=transform(sourcePoints[vertex]);
  }
  for(int y=0;y<rows;++y)for(int x=0;x<cols;++x){
   const int a=y*(cols+1)+x,b=a+1,c=a+cols+1,d=c+1;
   Triangle(targetPoints[a],targetPoints[b],targetPoints[c],sourcePoints[a],sourcePoints[b],sourcePoints[c],leg_[sourceIndex].pixels,farSide?.86:1);
   Triangle(targetPoints[b],targetPoints[d],targetPoints[c],sourcePoints[b],sourcePoints[d],sourcePoints[c],leg_[sourceIndex].pixels,farSide?.86:1);
  }
  return true;
 }
};
}
