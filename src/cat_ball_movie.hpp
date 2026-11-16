#pragma once
#include "cat_embedded_movie.hpp"
#include "ball_pounce.generated.hpp"

namespace cat::ball_movie {
// Cat pixels only. The ball remains in its independent continuous Box2D world.
class Player {
public:
    bool Prepare(HINSTANCE instance,unsigned coat) {
        const auto* selected=Find(coat);
        if(!selected){Reset();return false;}
        if(asset_!=selected) {
            bitmaps_.clear();asset_=selected;
            loader_.Prepare(instance,{selected->resource,selected->width,selected->height,selected->frames,selected->durationMs});
        }
        if(!loader_.Ready())return false;
        if(bitmaps_.empty())for(const auto& frame:loader_.Get()->frames) {
            auto bitmap=std::make_unique<Gdiplus::Bitmap>(asset_->width,asset_->height,asset_->width*4,
                PixelFormat32bppPARGB,reinterpret_cast<BYTE*>(const_cast<std::uint32_t*>(frame.pixels.data())));
            if(bitmap->GetLastStatus()!=Gdiplus::Ok){bitmaps_.clear();return false;}
            bitmaps_.push_back(std::move(bitmap));
        }
        return true;
    }
    bool Failed(){return loader_.Failed();}
    void Reset(){bitmaps_.clear();loader_.Reset();asset_=nullptr;}
    bool Draw(Gdiplus::Graphics& graphics,std::uint64_t ms,float center,float floor,double height,
            double bodyWidth,double bodyHeight,bool flip) {
        unsigned index=0;
        if(!asset_||bitmaps_.empty()||!loader_.Sample(ms,&index)||index>=bitmaps_.size())return false;
        const double sx=height/asset_->referenceHeight*bodyWidth,sy=height/asset_->referenceHeight*bodyHeight;
        const auto state=graphics.Save();
        if(flip){graphics.TranslateTransform(center*2,0);graphics.ScaleTransform(-1,1);}
        const Gdiplus::RectF dest(float(center-asset_->anchorX*sx),float(floor-asset_->anchorY*sy),
            float(asset_->width*sx),float(asset_->height*sy));
        const auto result=graphics.DrawImage(bitmaps_[index].get(),dest,0,0,asset_->width,asset_->height,Gdiplus::UnitPixel);
        graphics.Restore(state);return result==Gdiplus::Ok;
    }
private:
    cat::movie::Loader loader_;const Asset* asset_=nullptr;
    std::vector<std::unique_ptr<Gdiplus::Bitmap>> bitmaps_;
};
}
