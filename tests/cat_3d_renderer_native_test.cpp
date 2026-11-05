#include <iostream>
#include "cat_3d_renderer.hpp"
#include "native_3d_renderer_tests.hpp"

int main() {
    std::string report;
    const bool result = cat::render3d::NativeRendererSelfTest(report);
    std::cout << report << '\n';
    return result ? 0 : 1;
}
