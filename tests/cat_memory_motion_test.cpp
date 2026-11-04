#include "../src/cat_memory_motion.hpp"
#include <iostream>
#include <limits>
#include <stdexcept>
#define CHECK(x) do {if (!(x)) throw std::runtime_error(#x); } while(0)
int main() {
    CHECK(!cat::memory::ApproachTarget(200,210,96,1824));
    CHECK(!cat::memory::ApproachTarget(200,190,96,1824));
    CHECK(!cat::memory::ApproachTarget(200,267,96,1824));
    CHECK(!cat::memory::ApproachTarget(200,80,96,1824));
    CHECK(!cat::memory::ApproachTarget(200,400,300,100));
    CHECK(!cat::memory::ApproachTarget(200,std::numeric_limits<double>::quiet_NaN(),96,1824));
    for (double center : {96.0, 200.0, 960.0, 1824.0})
        for (double cursor = 96; cursor <= 1824; cursor += 13) {
            auto target = cat::memory::ApproachTarget(center,cursor,96,1824);
            if (target) {
                CHECK((*target-center)*(cursor-center)>0);
                CHECK(std::abs(cursor-*target) >= 55);
                CHECK(std::abs(cursor-*target) < std::abs(cursor-center));
                CHECK(*target>=96 && *target<=1824);
            }
        }
    std::cout << "Memory approach: close-hand, direction, stand-off and bounds scenarios passed\n";
}
