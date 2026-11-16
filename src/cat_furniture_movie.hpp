#pragma once
#include "cat_embedded_movie.hpp"
#include "furniture_assets.generated.hpp"

namespace cat::furniture {
class Player {
public:
    bool Prepare(HINSTANCE instance,Kind kind,unsigned coat) {
        const auto* selected=Find(kind,coat);
        if(!selected){Reset();return false;}
        if(asset_!=selected) {
            bitmaps_.clear();asset_=selected;
            loader_.Prepare(instance,{selected->resource,selected->width,selected->height,selected->frames,selected->durationMs});
        }
        if(!loader_.Ready())return false;
        if(bitmaps_.empty()) {
            const auto* movie=loader_.Get();
            for(const auto& frame:movie->frames) {
                bitmaps_.push_back(std::make_unique<Gdiplus::Bitmap>(movie->width,movie->height,movie->width*4,
                    PixelFormat32bppPARGB,reinterpret_cast<BYTE*>(const_cast<std::uint32_t*>(frame.pixels.data()))));
                if(bitmaps_.back()->GetLastStatus()!=Gdiplus::Ok){bitmaps_.clear();return false;}
            }
        }
        return true;
    }
    bool Failed(){return loader_.Failed();}
    void Reset(){bitmaps_.clear();loader_.Reset();asset_=nullptr;}
    bool Draw(Gdiplus::Graphics& graphics,std::uint64_t ms,float center,float floor,double desiredHeight,double bodyWidth,double bodyHeight) {
        unsigned index=0;if(!asset_||bitmaps_.empty()||!loader_.Sample(ms,&index)||index>=bitmaps_.size())return false;
        const double sx=desiredHeight/asset_->referenceHeight*bodyWidth,sy=desiredHeight/asset_->referenceHeight*bodyHeight;
        Gdiplus::RectF destination(float(center-asset_->anchorX*sx),float(floor-asset_->anchorY*sy),
            float(asset_->width*sx),float(asset_->height*sy));
        return graphics.DrawImage(bitmaps_[index].get(),destination,0,0,asset_->width,asset_->height,Gdiplus::UnitPixel)==Gdiplus::Ok;
    }
    const cat::movie::Movie* Movie(){return loader_.Get();}
private:
    cat::movie::Loader loader_;const Asset* asset_=nullptr;
    std::vector<std::unique_ptr<Gdiplus::Bitmap>> bitmaps_;
};
}
