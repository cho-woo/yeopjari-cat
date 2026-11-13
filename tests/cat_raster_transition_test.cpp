#include "cat_raster_transition.hpp"
#include <cassert>
#include <iostream>
#include <vector>

int main() {
    constexpr int W=96,H=96;using Image=std::vector<unsigned char>;
    Image a(W*H*4),b(a.size()),out(a.size());
    auto block=[](Image& p,int shift) {
        for(int y=24;y<70;++y)for(int x=22;x<66;++x) {
            auto* q=&p[(y*W+x+shift)*4];q[0]=160;q[1]=190;q[2]=210;q[3]=255;
            if(y>=37&&y<47&&((x>=32&&x<38)||(x>=50&&x<56)))q[0]=q[1]=q[2]=15;
        }
    };
    block(a,0);block(b,6);
    cat::raster_transition::RegisteredMorph morph;
    assert(!morph.Render(a.data(),b.data(),out.data(),W,H,.5));
    assert(morph.Prepare(a.data(),b.data(),W,H));
    assert(morph.Render(a.data(),b.data(),out.data(),W,H,0)&&out==a);
    assert(morph.Render(a.data(),b.data(),out.data(),W,H,1)&&out==b);
    assert(morph.RenderTarget(a.data(),b.data(),out.data(),W,H,0)&&out==a);
    assert(morph.RenderTarget(a.data(),b.data(),out.data(),W,H,1)&&out==b);
    cat::raster_transition::RegisteredMorph::Translate(a.data(),out.data(),W,H,6,0);assert(out==b);
    for(unsigned i=1;i<20;++i) {
        assert(morph.Render(a.data(),b.data(),out.data(),W,H,i/20.));
        for(std::size_t p=0;p<out.size();p+=4) {
            assert(out[p]<=out[p+3]&&out[p+1]<=out[p+3]&&out[p+2]<=out[p+3]);
        }
        assert(out[(50*W+45)*4+3]==255); // Opaque torso stays opaque.
        assert(morph.RenderTarget(a.data(),b.data(),out.data(),W,H,i/20.));
        assert(out[(50*W+45)*4+3]==255);
    }
    assert(!morph.Render(a.data(),b.data(),out.data(),W-1,H,.5));
    morph.Reset();assert(!morph.Render(a.data(),b.data(),out.data(),W,H,.5));
    Image blank(a.size());assert(!morph.Prepare(blank.data(),blank.data(),W,H));
    // A supported gesture can move the painted torso farther than the local
    // patch search. It still has one continuously travelling opaque actor.
    Image far(a.size());
    for(int y=0;y<H;++y)for(int x=0;x<W;++x) {
        const int nx=x+20,ny=y-20;
        if(nx<W&&ny>=0)for(int c=0;c<4;++c)far[(ny*W+nx)*4+c]=a[(y*W+x)*4+c];
    }
    assert(morph.Prepare(a.data(),far.data(),W,H));
    for(unsigned i=1;i<20;++i) {
        assert(morph.RenderTarget(a.data(),far.data(),out.data(),W,H,i/20.));
        double mass=0,cx=0,cy=0;
        for(int y=0;y<H;++y)for(int x=0;x<W;++x) {const auto alpha=out[(y*W+x)*4+3];mass+=alpha;cx+=x*alpha;cy+=y*alpha;}
        assert(mass>0);
        assert(std::abs(cx/mass-(43.5+i))<1.5);
        assert(std::abs(cy/mass-(46.5-i))<1.5);
    }
    std::cout<<"Registered boundary morph: exact endpoints, continuous attached patches, opaque torso, alpha bounds and reset passed\n";
}
