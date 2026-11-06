#include "native_care_fixture.hpp"
#include <array>
#include <cassert>
#include <iostream>

int main() {
    using namespace cat::sprite_sequence;
    for(unsigned count:{64u,128u,256u}) {
        auto layout=cat::native_care_test::CapacityLayout(count);
        assert(cat::sprite_assets::Valid(layout));
        assert(layout.program.outro.first==count&&layout.program.outro.count==0);
        Options options;options.sheetFrames=count;options.stage=Stage::Looping;
        for(unsigned exposure=0;exposure<count*3;++exposure) {
            const auto frame=SampleProgram(Clip::Groom,layout.program,
                exposure*cat::native_care_test::ExposureMs+18,options);
            assert(frame.frame==exposure%count);
        }
        // Regression for native exit371: an empty outro still follows the
        // full loop in the contiguous program, rather than pointing at0.
        layout.program.outro.first=0;
        assert(!ValidProgram(layout.program,count)&&!cat::sprite_assets::Valid(layout));
    }
    std::array<bool,256> colors{};
    for(unsigned frame=0;frame<256;++frame) {
        const auto color=cat::native_care_test::FrameColor(frame);
        const auto red=(color>>16)&255;
        assert(!colors[red]);colors[red]=true;
    }
    assert(!cat::sprite_assets::Valid(cat::native_care_test::CapacityLayout(0)));
    assert(!cat::sprite_assets::Valid(cat::native_care_test::CapacityLayout(257)));
    std::cout<<"Native64/128/256 PNG-cache fixture: valid contiguous ranges, repeated frame selection and unique256colors passed\n";
}
