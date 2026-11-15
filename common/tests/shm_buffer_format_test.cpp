#include "wayland/ShmBuffer.hpp"
#include <cassert>
#include <iostream>

int main() {
    assert(qypr::ShmBuffer::cairoFormatForWlShmFormat(WL_SHM_FORMAT_ARGB8888) ==
           CAIRO_FORMAT_ARGB32);
    assert(qypr::ShmBuffer::cairoFormatForWlShmFormat(WL_SHM_FORMAT_XRGB8888) ==
           CAIRO_FORMAT_RGB24);
    assert(qypr::ShmBuffer::cairoFormatForWlShmFormat(WL_SHM_FORMAT_RGB565) ==
           CAIRO_FORMAT_INVALID);
    std::cout << "ShmBuffer format mapping tests passed\n";
}
