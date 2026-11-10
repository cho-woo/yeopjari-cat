#pragma once

#include <algorithm>
#include <cmath>

namespace cat::idle_proxy {
struct Size {int width=0,height=0;};
// Source registration/destination coordinates stay at the full original
// resolution. Only the reusable local raster grid follows the visible size.
inline Size Plan(int sourceWidth,int sourceHeight,double displayedHeight) {
    if(sourceWidth<1||sourceHeight<1||!std::isfinite(displayedHeight)||displayedHeight<=0)return {};
    const double desired=std::clamp(std::ceil(displayedHeight*1.5/8.)*8.,1.,static_cast<double>(sourceHeight));
    const int height=static_cast<int>(desired);
    const int width=std::clamp(static_cast<int>(std::lround(sourceWidth*desired/sourceHeight)),1,sourceWidth);
    return {width,height};
}
} // namespace cat::idle_proxy
