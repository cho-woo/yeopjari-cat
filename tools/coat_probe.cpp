// Portable review helper: exercise the exact runtime material on BGRA buffers.
#include "../src/cat_coat.hpp"
#include <iostream>
#include <vector>
int main(int argc,char** argv) {
    if(argc!=5)return 1;
    const int w=std::stoi(argv[1]),h=std::stoi(argv[2]);
    if(w<=0||h<=0||w>16384||h>16384||1ull*w*h>32*1024*1024)return 2;
    std::vector<std::uint32_t> pixels(static_cast<std::size_t>(w)*h);
    if(!std::cin.read(reinterpret_cast<char*>(pixels.data()),pixels.size()*4))return 3;
    cat::coat::Apply(pixels.data(),w,h,static_cast<cat::sprite_sequence::Clip>(std::stoi(argv[3])),std::stoi(argv[4]));
    std::cout.write(reinterpret_cast<const char*>(pixels.data()),pixels.size()*4);
}
