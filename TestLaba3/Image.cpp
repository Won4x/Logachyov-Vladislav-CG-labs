#include "image.h"
#include <fstream>

Image::Image(int w, int h) : width(w), height(h) {
    pixels.resize(w * h);
}

void Image::Clear(const Vec3f& color) {
    for (auto& p : pixels)
        p = color;
}

void Image::SetPixel(int x, int y, const Vec3f& color) {
    if (x < 0 || y < 0 || x >= width || y >= height) return;
    pixels[x + y * width] = color;
}

void Image::SavePPM(const char* filename) {
    std::ofstream out(filename);
    out << "P3\n" << width << " " << height << "\n255\n";
    for (auto& p : pixels) {
        out << int(p.x * 255) << " "
            << int(p.y * 255) << " "
            << int(p.z * 255) << "\n";
    }
}
