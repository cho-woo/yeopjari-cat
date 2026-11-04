#include "cat_interaction_settings.hpp"
#include <cassert>
#include <iostream>

int main() {
    using namespace cat::interaction;
    assert(!Settings{}.workInterference);
    for (bool hunting : {false, true}) for (bool seasonal : {false, true}) {
        const auto bytes = Serialize({hunting, seasonal});
        Settings read;
        assert(Parse(bytes, read));
        assert(read.workInterference == hunting && read.seasonalEvents == seasonal);
        for (std::size_t i = 0; i < bytes.size(); ++i) {
            auto damaged = bytes; damaged[i] ^= 1;
            const auto before = read;
            assert(!Parse(damaged, read));
            assert(read.workInterference == before.workInterference && read.seasonalEvents == before.seasonalEvents);
        }
    }
    Settings read;
    for (const std::string& invalid : {std::string{}, std::string(257, 'x'), Serialize({true,true}) + "work_interference=1\n"})
        assert(!Parse(invalid, read));
    assert(!read.workInterference);
    std::cout << "Interaction settings: round-trip, corruption and fail-closed checks passed\n";
}
